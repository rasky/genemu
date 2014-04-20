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
    void draw_pixel(uint8_t *screen, uint8_t rgb, uint8_t attrs, int draw_command);
    template <bool fliph, int draw_command>
    void draw_pattern(uint8_t *screen, uint8_t *pattern, uint8_t palette, uint8_t attrs);
    template <int draw_command>
    void draw_pattern(uint8_t *screen, uint16_t name, int paty, bool is_sprite);
    void draw_nametable(uint8_t *screen, uint8_t *nt, int numcols, int paty);
    void draw_plane_ab(uint8_t *screen, int line, int ntaddr, uint16_t hs, uint16_t *vsram);
    void draw_plane_w(uint8_t *screen, int y);
    void draw_tiles(uint8_t *screen, int line);
    void draw_sprites(uint8_t *screen, int line);

    void get_hscroll(int line, int *hscroll_a, int *hscroll_b);

private:
    uint8_t* get_offscreen_buffer();
    void mix_offscreen_buffer(uint8_t *screen, uint8_t *buffer, int x, int width);

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

// True if the pixel is coming from a sprite (needed in shadow/highlight mode)
#define PIXATTR_SPRITE     (1 << 6)
// True if the pixel is marked as high-priority (for mixing purposes)
#define PIXATTR_HIPRI      (1 << 7)


void FORCE_INLINE GFX::draw_pixel(uint8_t *screen, uint8_t rgb, uint8_t attrs, int draw_command)
{
    assert(rgb < 0x40);
    assert((attrs & 0x3F) == 0);

    switch (draw_command)
    {
    case DRAW_ALWAYS:
        break;
    case DRAW_NOT_ON_SPRITE:
        if (*screen & PIXATTR_SPRITE)
            return;
        break;
    case DRAW_MAX_PRIORITY:
        if ((attrs & PIXATTR_HIPRI) < (*screen & PIXATTR_HIPRI))
            return;
        break;
    }

    *screen = rgb | attrs;
}

template <bool fliph, int draw_command>
void GFX::draw_pattern(uint8_t *screen, uint8_t *pattern, uint8_t palette, uint8_t attrs)
{
    if (fliph)
        pattern += 3;

    for (int x = 0; x < 4; ++x)
    {
        uint8_t pix = *pattern;
        uint8_t pix1 = !fliph ? pix>>4 : pix&0xF;
        uint8_t pix2 = !fliph ? pix&0xF : pix>>4;

        if (pix1) draw_pixel(screen+0, palette | pix1, attrs, draw_command);
        if (pix2) draw_pixel(screen+1, palette | pix2, attrs, draw_command);

        if (fliph)
            pattern--;
        else
            pattern++;
        screen += 2;
    }
}


template <int draw_command>
void GFX::draw_pattern(uint8_t *screen, uint16_t name, int paty, bool is_sprite)
{
    int pat_idx = BITS(name, 0, 11);
    int pat_fliph = BITS(name, 11, 1);
    int pat_flipv = BITS(name, 12, 1);
    int pat_palette = BITS(name, 13, 2);
    int pat_pri = BITS(name, 15, 1);
    uint8_t *pattern = VDP.VRAM + pat_idx * 32;
    uint8_t palette = pat_palette * 16;

    uint8_t attrs = 0;
    if (pat_pri) attrs |= PIXATTR_HIPRI;
    if (is_sprite) attrs |= PIXATTR_SPRITE;

    if (!pat_flipv)
        pattern += paty*4;
    else
        pattern += (7-paty)*4;

    if (!pat_fliph)
        draw_pattern<false, draw_command>(screen, pattern, palette, attrs);
    else
        draw_pattern<true, draw_command>(screen, pattern, palette, attrs);
}


void GFX::draw_nametable(uint8_t *screen, uint8_t *nt, int numcols, int paty)
{
    for (int i = 0; i < numcols; ++i)
    {
        draw_pattern<DRAW_MAX_PRIORITY>(screen, (nt[0] << 8) | nt[1], paty, false);
        nt += 2;
        screen += 8;
    }
}

void GFX::draw_plane_w(uint8_t *screen, int y)
{
    int addr_w = VDP.get_nametable_W();
    int row = y >> 3;
    int paty = y & 7;
    uint16_t ntwidth = BITS(VDP.regs[16], 0, 2);
    ntwidth  = (ntwidth + 1) * 32;

    draw_nametable(screen, VDP.VRAM + addr_w + row*2*ntwidth, screen_width()/8, paty);
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

        draw_pattern<DRAW_MAX_PRIORITY>(screen, (nt[col*2] << 8) | nt[col*2+1], paty, false);

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
    if (line == 66)
    {
        sidx = 0;
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

            mem_log("SPRITE", "%d (sx:%d, sy:%d sz:%d,%d, name:%04x, link:%02x)\n",
                    sidx, sx, sy, sw*8, sh*8, name, link);

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
                    draw_pattern<DRAW_NOT_ON_SPRITE>(screen + sx + p*8, name, paty, true);
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

void GFX::get_hscroll(int line, int *hscroll_a, int *hscroll_b)
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

    *hscroll_a = FETCH16(table + idx*4 + 0) & 0x3FF;
    *hscroll_b = FETCH16(table + idx*4 + 2) & 0x3FF;
}

uint8_t *GFX::get_offscreen_buffer(void)
{
    enum { PIX_OVERFLOW = 32 };
    static uint8_t buffer[SCREEN_WIDTH + PIX_OVERFLOW*2];
    memset(buffer, 0, sizeof(buffer));
    return buffer + PIX_OVERFLOW;
}

void GFX::mix_offscreen_buffer(uint8_t *screen, uint8_t *buffer, int x, int w)
{
    screen += x;
    buffer += x;

    while (w--)
    {
        if ((*buffer & 0x3F) && (*buffer & PIXATTR_HIPRI) >= (*screen & PIXATTR_HIPRI))
            *screen = *buffer;
        buffer++;
        screen++;
    }
}


void GFX::draw_tiles(uint8_t *screen, int line)
{

#if 0
    if (line == 0) {
        int addr_a = VDP.get_nametable_A();
        int addr_b = VDP.get_nametable_B();
        int addr_w = VDP.get_nametable_W();
        mem_log("GFX", "A(addr:%04x) B(addr:%04x) W(addr:%04x) SPR(addr:%04x)\n", addr_a, addr_b, addr_w, ((VDP.regs[5] & 0x7F) << 9));
        mem_log("GFX", "W(h:%d, right:%d, v:%d, down:%d\n)", winh, winhright, winv, winvdown);

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

    uint16_t backdrop_color = BITS(VDP.regs[7], 0, 6);
    for (int x=0;x<screen_width();x++)
        draw_pixel(screen + x, backdrop_color, 0, DRAW_ALWAYS);

    // Plane/sprite disable, show only backdrop
    if (!BIT(VDP.regs[1], 6))
        return;

    int hsa, hsb;
    get_hscroll(line, &hsa, &hsb);

    int winh = VDP.regs[17] & 0x1F;
    int winhright = VDP.regs[17] >> 7;
    int winv = VDP.regs[18] & 0x1F;
    int winvdown = VDP.regs[18] >> 7;
    bool full_window, partial_window;

    // Plane A or W
    full_window = false;
    if (winv) {
        if (winvdown && line >= winv*8)
        {
            full_window = true;
        }
        else if (!winvdown && line < winv*8)
        {
            full_window = true;
        }
    }
    if (winh*16 >= screen_width())
        full_window = true;

    if (!full_window)
    {
        if (!winhright)
            partial_window = (winh != 0);
        else
            partial_window = true;
    }

    // Plane B
    if (!keystate[SDL_SCANCODE_B])
        draw_plane_ab(screen, line, VDP.get_nametable_B(), hsb, VDP.VSRAM+1);

    if (!full_window && !keystate[SDL_SCANCODE_A])
    {
        uint8_t *buffer = get_offscreen_buffer();
        draw_plane_ab(buffer, line, VDP.get_nametable_A(), hsa, VDP.VSRAM);

        int ax = (!winhright ? winh*16 : 0);
        int aw = screen_width() - winh*16;
        mix_offscreen_buffer(screen, buffer, ax, aw);
    }

    if (full_window && !keystate[SDL_SCANCODE_W])
        draw_plane_w(screen, line);
    else if (partial_window && !keystate[SDL_SCANCODE_W])
    {
        uint8_t *buffer = get_offscreen_buffer();
        draw_plane_w(buffer, line);

        int wx = (!winhright ? 0 : screen_width() - winh*16);
        int ww = winh*16;
        mix_offscreen_buffer(screen, buffer, wx, ww);
    }
}

void GFX::render_scanline(uint8_t *screen, int line)
{
    // Overflow is the maximum size we can draw outside to avoid
    // wasting time and code in clipping. The maximum object is a 4x4 sprite,
    // so 32 pixels (on both side) is enough.
    enum { PIX_OVERFLOW = 32 };
    uint8_t tile_buffer[SCREEN_WIDTH + PIX_OVERFLOW*2];
    uint8_t sprite_buffer[SCREEN_WIDTH + PIX_OVERFLOW*2];

    if (BITS(VDP.regs[12], 1, 2) != 0)
        assert(!"interlace mode");

    if (line >= 224)
        return;

    // Display enable
    memset(screen, 0, SCREEN_WIDTH);
    if (BIT(VDP.regs[0], 0))
        return;

    uint8_t *src1 = tile_buffer+PIX_OVERFLOW;
    uint8_t *src2 = sprite_buffer+PIX_OVERFLOW;
    uint16_t backdrop_color = VDP.CRAM[BITS(VDP.regs[7], 0, 6)];

    memset(src1, 0, SCREEN_WIDTH);
    memset(src2, 0, SCREEN_WIDTH);

    draw_tiles(src1, line);
    draw_sprites(src2, line);

    for (int i=0; i<SCREEN_WIDTH; ++i)
    {
        if (i < screen_offset() || i >= screen_offset() + screen_width())
        {
            *screen++ = CRAM_R(backdrop_color);
            *screen++ = CRAM_G(backdrop_color);
            *screen++ = CRAM_B(backdrop_color);
            *screen++ = 0;
            continue;
        }

        uint8_t pix;
        uint8_t tpix = *src1++;
        uint8_t spix = *src2++;

        if ((spix & 0x3F) && (tpix & PIXATTR_HIPRI) <= (spix & PIXATTR_HIPRI))
            pix = spix;
        else
            pix = tpix;

        uint8_t index = pix & 0x3F;
        uint16_t rgb = VDP.CRAM[index];

        uint8_t r = CRAM_R(rgb);
        uint8_t g = CRAM_G(rgb);
        uint8_t b = CRAM_B(rgb);

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




#if 0
        if (MODE_SHI)
        {
            // 00 -> tile pri0
            // 01 -> tile pri1
            // 10 -> sprite pri0
            // 11 -> sprite pri1

            if (!(*src & PIXATTR_SPRITE))
            {
                // Tile: pri0 -> shadow, pri1 -> normal
                if (!(*src & PIXATTR_HIPRI))
                    SHADOW_COLOR(r,g,b);
            }
            else
            {
                if (index == 0x0E || index == 0x1E || index == 0x2E)
                    /* VDP bug: these indices do nothing */ ;
                else
                {
                    // Sprite pri=0: half-intensity, pri1 -> highlight
                    if (!(*src & PIXATTR_HIPRI)
                        SHADOW_COLOR(r,g,b);
                    else
                        HIGHLIGHT_COLOR(r,g,b);
                }
            }
        }
#endif
