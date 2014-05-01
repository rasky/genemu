#include "vdp.h"
#include "mem.h"
#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <SDL.h>
extern "C" {
    #include "hw.h"
}
#define SCREEN_WIDTH 320
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

class GFX
{
private:
    template <bool fliph>
    void draw_pattern(uint8_t *screen, uint8_t *pattern, uint8_t attrs);
    void draw_pattern(uint8_t *screen, uint16_t name, int paty);
    void draw_plane_ab(uint8_t *screen, int line, int ntaddr, uint16_t hs, uint16_t *vsram);
    void draw_plane_a(uint8_t *screen, int y);
    void draw_plane_b(uint8_t *screen, int y);
    void draw_plane_w(uint8_t *screen, int y);
    void draw_sprites(uint8_t *screen, int line);

    uint8_t* get_hscroll_vram(int line);
    inline bool in_window(int x, int y);
    uint8_t mix(int x, int y, uint8_t back, uint8_t b, uint8_t a, uint8_t s);

public:
    int screen_offset() { return (SCREEN_WIDTH - screen_width()) / 2; }
    int screen_width() { return BIT(VDP.regs[12], 7) ? 40*8 : 32*8; }

    void render_scanline(uint8_t *screen, int line);

} GFX;

#define FORCE_INLINE            __attribute__((always_inline))

#define DRAW_ALWAYS             0    // draw all the pixels
#define DRAW_NOT_ON_SPRITE      1    // draw only if the buffer doesn't contain a pixel from a sprite
#define DRAW_MAX_PRIORITY       2    // draw only if priority >= pixel in the buffer

#define COLOR_3B_TO_8B(c)  (((c) << 5) | ((c) << 2) | ((c) >> 1))
#define CRAM_R(c)          COLOR_3B_TO_8B(BITS((c), 1, 3))
#define CRAM_G(c)          COLOR_3B_TO_8B(BITS((c), 5, 3))
#define CRAM_B(c)          COLOR_3B_TO_8B(BITS((c), 9, 3))

#define MODE_SHI           BITS(VDP.regs[12], 3, 1)

#define SHADOW_COLOR(r,g,b) \
    do { r >>= 1; g >>= 1; b >>= 1; } while (0)
#define HIGHLIGHT_COLOR(r,g,b) \
    do { SHADOW_COLOR(r,g,b); r |= 0x80; g |= 0x80; b |= 0x80; } while(0)

// While we draw the planes, we use bit 0x80 on each pixel to save the
// high-priority flag, so that we can later prioritize.
#define PIXATTR_HIPRI        0x80

// After mixing code, we use free bits 0x80 and 0x40 to indicate the
// shadow/highlight effect to apply on each pixel. Notice that we use
// 0x80 to indicate normal drawing and 0x00 to indicate shadowing,
// which does match exactly the semantic of PIXATTR_HIPRI. This simplifies
// mixing code quite a bit.
#define SHI_NORMAL(x)        ((x) | 0x80)
#define SHI_HIGHLIGHT(x)     ((x) | 0x40)
#define SHI_SHADOW(x)        ((x) & 0x3F)

#define SHI_IS_SHADOW(x)     (!((x) & 0x80))
#define SHI_IS_HIGHLIGHT(x)  ((x) & 0x40)

template <bool fliph>
void GFX::draw_pattern(uint8_t *screen, uint8_t *pattern, uint8_t attrs)
{
    if (fliph)
        pattern += 3;

    for (int x = 0; x < 4; ++x)
    {
        uint8_t pix = *pattern;
        uint8_t pix1 = !fliph ? pix>>4 : pix&0xF;
        uint8_t pix2 = !fliph ? pix&0xF : pix>>4;

        // Never overwrite already-written bytes. This is only
        // useful for sprites; within each plane, we never overdraw anyway.
        if ((screen[0] & 0xF) == 0)
            screen[0] = attrs | pix1;
        if ((screen[1] & 0xF) == 0)
            screen[1] = attrs | pix2;

        if (fliph)
            pattern--;
        else
            pattern++;
        screen += 2;
    }
}


void GFX::draw_pattern(uint8_t *screen, uint16_t name, int paty)
{
    int pat_idx = BITS(name, 0, 11);
    int pat_fliph = BITS(name, 11, 1);
    int pat_flipv = BITS(name, 12, 1);
    int pat_palette = BITS(name, 13, 2);
    int pat_pri = BITS(name, 15, 1);
    uint8_t *pattern = VDP.VRAM + pat_idx * 32;
    uint8_t attrs = (pat_palette << 4) | (pat_pri ? PIXATTR_HIPRI : 0);

    if (!pat_flipv)
        pattern += paty*4;
    else
        pattern += (7-paty)*4;

    if (!pat_fliph)
        draw_pattern<false>(screen, pattern, attrs);
    else
        draw_pattern<true>(screen, pattern, attrs);
}

void GFX::draw_plane_w(uint8_t *screen, int y)
{
    int addr_w = VDP.get_nametable_W();
    int row = y >> 3;
    int paty = y & 7;
    uint16_t ntwidth = (screen_width() == 320 ? 64 : 32);
    uint8_t *nt = VDP.VRAM + addr_w + row*2*ntwidth;

    for (int i = 0; i < screen_width() / 8; ++i)
    {
        draw_pattern(screen, FETCH16(nt), paty);
        nt += 2;
        screen += 8;
    }
}

void GFX::draw_plane_ab(uint8_t *screen, int line, int ntaddr, uint16_t scrollx, uint16_t *vsram)
{
    uint8_t *end = screen + screen_width();
    uint16_t ntwidth = BITS(VDP.regs[16], 0, 2);
    uint16_t ntheight = BITS(VDP.regs[16], 4, 2);
    uint16_t ntw_mask, nth_mask;
    int numcell;
    bool column_scrolling = BIT(VDP.regs[11], 2);

    if (column_scrolling && line==0)
        mem_log("SCROLL", "column scrolling\n");

    assert(ntwidth != 2);  // invalid setting
    assert(ntheight != 2); // invalid setting

    ntwidth  = (ntwidth + 1) * 32;
    ntheight = (ntheight + 1) * 32;
    ntw_mask = ntwidth - 1;
    nth_mask = ntheight - 1;

    // Invert horizontal scrolling (because it goes right, but we need to offset of the first screen pixel)
    scrollx = -scrollx;
    uint8_t col = (scrollx >> 3) & ntw_mask;
    uint8_t patx = scrollx & 7;

    numcell = 0;
    screen -= patx;
    while (screen < end)
    {
         // Calculate vertical scrolling for the current line
        uint16_t scrolly = (*vsram & 0x3FF) + line;
        uint8_t row = (scrolly >> 3) & nth_mask;
        uint8_t paty = scrolly & 7;
        uint8_t *nt = VDP.VRAM + ntaddr + row*(2*ntwidth);

        draw_pattern(screen, FETCH16(nt + col*2), paty);

        col = (col + 1) & ntw_mask;
        screen += 8;
        numcell++;

        // If per-column scrolling is active, increment VSRAM pointer
        if (column_scrolling && (numcell & 1) == 0)
            vsram += 2;
    }
}

void GFX::draw_sprites(uint8_t *screen, int line)
{
    // Plane/sprite disable, show only backdrop
    if (!BIT(VDP.regs[1], 6) || keystate[SDL_SCANCODE_S])
        return;

    uint8_t *start_table = VDP.VRAM + ((VDP.regs[5] & 0x7F) << 9);

    // This is both the size of the table as seen by the VDP
    // *and* the maximum number of sprites that are processed
    // (important in case of infinite loops in links).
    const int SPRITE_TABLE_SIZE     = (screen_width() == 320) ?  80 :  64;
    const int MAX_SPRITES_PER_LINE  = (screen_width() == 320) ?  20 :  16;
    const int MAX_PIXELS_PER_LINE   = (screen_width() == 320) ? 320 : 256;

#if 0
    if (line == 220)
    {
        int sidx = 0;
        for (int i = 0; i < 64; ++i)
        {
            uint8_t *table = start_table + sidx*8;
            int sy = ((table[0] & 0x3) << 8) | table[1];
            int sh = BITS(table[2], 0, 2) + 1;
            uint16_t name = (table[4] << 8) | table[5];
            int flipv = BITS(name, 12, 1);
            int fliph = BITS(name, 11, 1);
            int sw = BITS(table[2], 2, 2) + 1;
            int sx = ((table[6] & 0x3) << 8) | table[7];
            int link = BITS(table[3], 0, 7);
            int pat_idx = BITS(name, 0, 11);

            mem_log("SPRITE", "%d (sx:%d, sy:%d sz:%d,%d, name:%04x, link:%02x, VRAM:%04x)\n",
                    sidx, sx, sy, sw*8, sh*8, name, link, pat_idx*32);

            if (link == 0) break;
            sidx = link;
        }
    }
#endif

    bool masking = false, one_sprite_nonzero = false;
    int sidx = 0, num_sprites = 0, num_pixels = 0;
    for (int i = 0; i < SPRITE_TABLE_SIZE && sidx < SPRITE_TABLE_SIZE; ++i)
    {
        uint8_t *table = start_table + sidx*8;
        int sy = ((table[0] & 0x3) << 8) | table[1];
        int sh = BITS(table[2], 0, 2) + 1;
        int link = BITS(table[3], 0, 7);
        uint16_t name = (table[4] << 8) | table[5];
        int flipv = BITS(name, 12, 1);
        int fliph = BITS(name, 11, 1);
        int sw = BITS(table[2], 2, 2) + 1;
        int sx = ((table[6] & 0x3) << 8) | table[7];

        sy -= 128;
        if (line >= sy && line < sy+sh*8)
        {
            // Sprite masking: a sprite on column 0 masks
            // any lower-priority sprite, but with the following conditions
            //   * it only works from the second visible sprite on each line
            //   * if the previous line had a sprite pixel overflow, it
            //     works even on the first sprite
            // Notice that we need to continue parsing the table after masking
            // to see if we reach a pixel overflow (because it would affect masking
            // on next line).
            if (sx == 0)
            {
                if (one_sprite_nonzero || VDP.sprite_overflow == line-1)
                    masking = true;
            }
            else
                one_sprite_nonzero = true;

            int row = (line - sy) >> 3;
            int paty = (line - sy) & 7;
            if (flipv)
                row = sh - row - 1;

            sx -= 128;
            if (sx > -sw*8 && sx < screen_width() && !masking)
            {
                name += row;
                if (fliph)
                    name += sh*(sw-1);
                for (int p=0;p<sw && num_pixels < MAX_PIXELS_PER_LINE;p++)
                {
                    draw_pattern(screen + sx + p*8, name, paty);
                    if (!fliph)
                        name += sh;
                    else
                        name -= sh;
                    num_pixels += 8;
                }
            }
            else
                num_pixels += sw*8;

            if (num_pixels >= MAX_PIXELS_PER_LINE)
            {
                VDP.sprite_overflow = line;
                break;
            }
            if (++num_sprites >= MAX_SPRITES_PER_LINE)
                break;
        }

        if (link == 0) break;
        sidx = link;
    }
}

uint8_t *GFX::get_hscroll_vram(int line)
{
    int table_offset = VDP.regs[13] & 0x3F;
    int mode = VDP.regs[11] & 3;
    uint8_t *table = VDP.VRAM + (table_offset << 10);
    int idx;

    switch (mode)
    {
    case 0: // Full screen scrolling
        idx = 0;
        break;
    case 1: // First 8 lines
        idx = (line & 7);
        break;
    case 2: // Every row
        idx = (line & ~7);
        break;
    case 3: // Every line
        idx = line;
        break;
    }

    return table + idx*4;
}

void GFX::draw_plane_a(uint8_t *screen, int line)
{
    if (keystate[SDL_SCANCODE_A]) return;
    uint16_t hsa = FETCH16(get_hscroll_vram(line) + 0) & 0x3FF;
    draw_plane_ab(screen, line, VDP.get_nametable_A(), hsa, VDP.VSRAM);
}

void GFX::draw_plane_b(uint8_t *screen, int line)
{
    if (keystate[SDL_SCANCODE_B]) return;
    uint16_t hsb = FETCH16(get_hscroll_vram(line) + 2) & 0x3FF;
    draw_plane_ab(screen, line, VDP.get_nametable_B(), hsb, VDP.VSRAM+1);
}

uint8_t GFX::mix(int x, int y, uint8_t back, uint8_t b, uint8_t a, uint8_t s)
{
    uint8_t tile = 0;

    if (b & 0xF)
        tile = b;
    if ((a & 0xF) && (tile & 0x80) <= (a & 0x80))
        tile = a;
    if ((s & 0xF) && (tile & 0x80) <= (s & 0x80))
    {
        if (MODE_SHI)
            switch (s & 0x3F) {
                case 0x0E:
                case 0x1E:
                case 0x2E: return SHI_NORMAL(s);         // draw sprite, normal
                case 0x3E: return SHI_HIGHLIGHT(tile);   // draw tile, force highlight
                case 0x3F: return SHI_SHADOW(tile);      // draw tile, force shadow
            }
        return s;
    }

    if (MODE_SHI)
        tile |= (b|a) & 0x80;
    return tile;
}

inline bool GFX::in_window(int x, int y)
{
    if (keystate[SDL_SCANCODE_W]) return false;

    int winv = (VDP.regs[18] & 0x1F) * 8;
    bool winvdown = BIT(VDP.regs[18], 7);

    if (winvdown && y >= winv) return true;
    if (!winvdown && y < winv) return true;

    int winh = (VDP.regs[17] & 0x1F) * 16;
    bool winhright = BIT(VDP.regs[17], 7);

    if (winhright && x >= winh) return true;
    if (!winhright && x < winh) return true;

    return false;
}

void GFX::render_scanline(uint8_t *screen, int line)
{
    // Overflow is the maximum size we can draw outside to avoid
    // wasting time and code in clipping. The maximum object is a 4x4 sprite,
    // so 32 pixels (on both side) is enough.
    enum { PIX_OVERFLOW = 32 };
    uint8_t buffer[4][SCREEN_WIDTH + PIX_OVERFLOW*2];

    if (BITS(VDP.regs[12], 1, 2) != 0)
        assert(!"interlace mode");

    if (line >= 224)
        return;

#if 0
    if (line == 0) {
        int winh = VDP.regs[17] & 0x1F;
        int winhright = VDP.regs[17] >> 7;
        int winv = VDP.regs[18] & 0x1F;
        int winvdown = VDP.regs[18] >> 7;
        int addr_a = VDP.get_nametable_A();
        int addr_b = VDP.get_nametable_B();
        int addr_w = VDP.get_nametable_W();
        mem_log("GFX", "A(addr:%04x) B(addr:%04x) W(addr:%04x) SPR(addr:%04x)\n", addr_a, addr_b, addr_w, ((VDP.regs[5] & 0x7F) << 9));
        mem_log("GFX", "W(h:%d, right:%d, v:%d, down:%d\n)", winh, winhright, winv, winvdown);
        mem_log("GFX", "SCROLL: %04x %04x\n", VDP.VSRAM[0], VDP.VSRAM[1]);

        FILE *f;
        f=fopen("vram.dmp", "wb");
        fwrite(VDP.VRAM, 1, 65536, f);
        fclose(f);
        f=fopen("cram.dmp", "wb");
        fwrite(VDP.CRAM, 1, 64*2, f);
        fclose(f);
        f=fopen("vsram.dmp", "wb");
        fwrite(VDP.VSRAM, 1, 64*2, f);
        fclose(f);
    }
#endif


    // Display enable
    memset(screen, 0, SCREEN_WIDTH);
    if (BIT(VDP.regs[0], 0))
        return;

    // Gfx enable
    bool enable = BIT(VDP.regs[1], 6);

    memset(buffer, 0, sizeof(buffer));

    uint16_t back = BITS(VDP.regs[7], 0, 6);
    uint8_t *pb = &buffer[0][PIX_OVERFLOW];
    uint8_t *pa = &buffer[1][PIX_OVERFLOW];
    uint8_t *pw = &buffer[2][PIX_OVERFLOW];
    uint8_t *ps = &buffer[3][PIX_OVERFLOW];

    draw_plane_b(pb+screen_offset(), line);
    draw_plane_a(pa+screen_offset(), line);
    draw_plane_w(pw+screen_offset(), line);
    draw_sprites(ps+screen_offset(), line);

    for (int i=0; i<SCREEN_WIDTH; ++i)
    {
        uint8_t pix = back;

        if (enable && i >= screen_offset() && i < screen_offset() + screen_width())
        {
            uint8_t *aw = in_window(i, line) ? pw : pa;
            pix = mix(i, line, back, pb[i], aw[i], ps[i]);
            if ((pix & 0xF) == 0)
                pix = back;
        }


        uint8_t index = pix & 0x3F;
        uint16_t rgb = VDP.CRAM[index];

        uint8_t r = CRAM_R(rgb);
        uint8_t g = CRAM_G(rgb);
        uint8_t b = CRAM_B(rgb);

        if (MODE_SHI && !keystate[SDL_SCANCODE_H])
        {
            if (SHI_IS_HIGHLIGHT(pix))
                HIGHLIGHT_COLOR(r,g,b);
            else if (SHI_IS_SHADOW(pix))
                SHADOW_COLOR(r,g,b);
        }

        *screen++ = r;
        *screen++ = g;
        *screen++ = b;
        *screen++ = 0;
    }
}


void gfx_render_scanline(uint8_t *screen, int line)
{
    GFX.render_scanline(screen, line);
}
