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
    void draw_pixel(uint8_t *screen, uint16_t rgb, int pri);
    template <bool fliph>
    void draw_pattern(uint8_t *screen, uint8_t *pattern, uint16_t *palette, int pri);
    void draw_pattern(uint8_t *screen, uint16_t name, int paty);
    void draw_nametable(uint8_t *screen, uint8_t *nt, int numcols, int paty);
    void draw_plane_ab(uint8_t *screen, int line, int ntaddr, int hs, int vs);
    void draw_plane_w(uint8_t *screen, int y);
    void draw_sprites(uint8_t *screen, int line);

    void get_hscroll(int line, int *hscroll_a, int *hscroll_b);

public:
    int screen_offset() { return (SCREEN_WIDTH - screen_width()) / 2; }
    int screen_width() { return BIT(VDP.regs[12], 7) ? 40*8 : 32*8; }
    bool draw_scanline(uint8_t *screen, int line);

} GFX;

#define COLOR_3B_TO_8B(c)  (((c) << 5) | ((c) << 2) | ((c) >> 1))
#define CRAM_R(c)          COLOR_3B_TO_8B(BITS((c), 1, 3))
#define CRAM_G(c)          COLOR_3B_TO_8B(BITS((c), 5, 3))
#define CRAM_B(c)          COLOR_3B_TO_8B(BITS((c), 9, 3))


inline void GFX::draw_pixel(uint8_t *screen, uint16_t rgb, int pri)
{
    if (pri >= screen[3])
    {
        screen[0] = CRAM_R(rgb);
        screen[1] = CRAM_G(rgb);
        screen[2] = CRAM_B(rgb);
        screen[3] = pri;
    }
}

template <bool fliph>
void GFX::draw_pattern(uint8_t *screen, uint8_t *pattern, uint16_t *palette, int pri)
{
    if (fliph)
        pattern += 3;

    for (int x = 0; x < 4; ++x)
    {
        uint8_t pix = *pattern;
        uint8_t pix1 = !fliph ? pix>>4 : pix&0xF;
        uint8_t pix2 = !fliph ? pix&0xF : pix>>4;

        if (pix1) draw_pixel(screen+0, palette[pix1], pri);
        if (pix2) draw_pixel(screen+4, palette[pix2], pri);

        if (fliph)
            pattern--;
        else
            pattern++;
        screen += 8;
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
    uint16_t *palette = VDP.CRAM + pat_palette * 16;

    if (!pat_flipv)
        pattern += paty*4;
    else
        pattern += (7-paty)*4;

    if (!pat_fliph)
        draw_pattern<false>(screen, pattern, palette, pat_pri);
    else
        draw_pattern<true>(screen, pattern, palette, pat_pri);
}


void GFX::draw_nametable(uint8_t *screen, uint8_t *nt, int numcols, int paty)
{
    for (int i = 0; i < numcols; ++i)
    {
        draw_pattern(screen, (nt[0] << 8) | nt[1], paty);
        nt += 2;
        screen += 32;
    }
}

void GFX::draw_plane_w(uint8_t *screen, int y)
{
    int addr_w = VDP.get_nametable_W();
    int row = y >> 3;
    int paty = y & 7;

    draw_nametable(screen, VDP.VRAM + addr_w + row*2*screen_width()/8, screen_width()/8, paty);
}

void GFX::draw_plane_ab(uint8_t *screen, int line, int ntaddr, int scrollx, int scrolly)
{
    int ntwidth = BITS(VDP.regs[16], 0, 2);
    int ntheight = BITS(VDP.regs[16], 4, 2);
    int screen_cols = screen_width() / 8;

    assert(ntwidth != 2);  // invalid setting
    assert(ntheight != 2); // invalid setting

    ntwidth  = (ntwidth + 1) * 32;
    ntheight = (ntheight + 1) * 32;

    scrollx %= ntwidth*8;
    scrolly += line;
    scrolly %= ntheight*8;

    int row = scrolly >> 3;
    int paty = scrolly & 7;

    if (scrollx < screen_width())
    {
        int num_cols = (screen_width() - scrollx + 7) / 8;
        draw_nametable(screen + scrollx*4, VDP.VRAM + ntaddr + row*(2*ntwidth), num_cols, paty);
    }

    if (scrollx > 0)
    {
        scrollx -= ntwidth*8;
        int col = (-scrollx) / 8;
        int patx = (-scrollx) & 7;
        assert(col >= 0 && col < ntwidth);
        int num_cols = MIN(ntwidth - col, screen_width()/8 + 1);
        draw_nametable(screen - patx*4, VDP.VRAM + ntaddr + row*(2*ntwidth) + col*2, num_cols, paty);
    }
}

void GFX::draw_sprites(uint8_t *screen, int line)
{
    int indices[64];
    uint8_t *start_table = VDP.VRAM + ((VDP.regs[5] & 0x7F) << 9);
    int sidx;
    int ns;
    int num_pixels;

#if 0
    if (line == 0)
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

            mem_log("SPRITE", "%d (sx:%d, sy:%d sz:%d,%d, name:%04x)\n",
                    sidx, sx, sy, sw*8, sh*8, name);

            if (link == 0) break;
            sidx = link;
        }
    }
#endif

    sidx = 0; ns = 0; num_pixels = 0;
    for (int i = 0; i < 64; ++i)
    {
        uint8_t *table = start_table + sidx*8;
        int sy = ((table[0] & 0x3) << 8) | table[1];
        int sh = BITS(table[2], 0, 2) + 1;
        int link = BITS(table[3], 0, 7);
        int sw = BITS(table[2], 2, 2) + 1;
        int sx = ((table[6] & 0x3) << 8) | table[7];

        // Sprite masking: a sprite with X=0 blocks other lower-priority
        // sprites on the same line.
        if (line == 0 && (sx == 0 || sx == 1))
            mem_log("SPRITE", "Unimplemented: sprite masking\n");

        sy -= 128;
        if (line >= sy && line < sy+sh*8)
        {
            indices[ns++] = sidx;
            num_pixels += sw*8;

            // Enforce sprite limits per scanline (depend on the resolution)
            if (screen_width() == 320)
            {
                if (ns >= 20 || num_pixels >= 320)
                    break;
            }
            else
            {
                if (ns >= 16 || num_pixels >= 256)
                    break;
            }

            // Sprite masking: a sprite on column 0 masks
            // any lower-priority sprite (ex: sunsetriders)
            if (sx == 0)
                break;
        }

        if (link == 0) break;
        sidx = link;
    }

    for (int i=ns-1;i>=0;i--)
    {
        uint8_t *table = start_table + indices[i]*8;
        int sy = ((table[0] & 0x3) << 8) | table[1];
        int sh = BITS(table[2], 0, 2) + 1;
        uint16_t name = (table[4] << 8) | table[5];
        int flipv = BITS(name, 12, 1);
        int fliph = BITS(name, 11, 1);
        int sw = BITS(table[2], 2, 2) + 1;
        int sx = ((table[6] & 0x3) << 8) | table[7];

        sy -= 128;
        sx -= 128;

        assert(line >= sy && line < sy+sh*8);

        int row = (line - sy) >> 3;
        int paty = (line - sy) & 7;

        if (flipv)
            row = sh - row - 1;

        if (sx > -sw*8 && sx < screen_width())
        {
            name += row;
            if (fliph)
                name += sh*(sw-1);
            for (int p=0;p<sw;p++)
            {
                draw_pattern(screen + (sx+p*8)*4, name, paty);
                if (!fliph)
                    name += sh;
                else
                    name -= sh;
            }
        }
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

bool GFX::draw_scanline(uint8_t *screen, int line)
{
    int winh = VDP.regs[17] & 0x1F;
    int winhright = VDP.regs[17] >> 7;

    int winv = VDP.regs[18] & 0x1F;
    int winvdown = VDP.regs[18] >> 7;
    bool linew;

    if (BITS(VDP.regs[12], 1, 2) != 0)
        assert(!"interlace mode");

    if (line >= 224)
        return false;

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

    // Display enable
    memset(screen, 0, SCREEN_WIDTH*4);
    if (BIT(VDP.regs[0], 0))
        return true;

    // Center display if horizontal resolution is smaller
    screen += screen_offset() * 4;

    uint16_t backdrop_color = VDP.CRAM[BITS(VDP.regs[7], 0, 6)];
    for (int x=0;x<screen_width();x++)
    {
        screen[x*4+3] = 0;
        draw_pixel(screen + x*4, backdrop_color, 0);
    }

    // Plaen/sprite disable, show only backdrop
    if (!BIT(VDP.regs[1], 6))
        return true;

    int hsa, hsb;
    get_hscroll(line, &hsa, &hsb);

    if (BIT(VDP.regs[11], 2))
        assert(!"vertical scrolling per column");

    int vsa, vsb;
    vsa = VDP.VSRAM[0] & 0x3FF;
    vsb = VDP.VSRAM[1] & 0x3FF;

    // Plane B
    if (!keystate[SDLK_b])
        draw_plane_ab(screen, line, VDP.get_nametable_B(), hsb, vsb);

    // Plane A or W
    linew = false;
    if (winv) {
        if (winvdown && line >= winv*8)
        {
            assert(!"winv down scroll");
            linew = true;
        }
        else if (!winvdown && line <= winv*8)
        {
            linew = true;
        }
    }

    if (!linew && !keystate[SDLK_a])
        draw_plane_ab(screen, line, VDP.get_nametable_A(), hsa, vsa);

    // Sprites
    if (!keystate[SDLK_s])
        draw_sprites(screen, line);

    if (linew && !keystate[SDLK_w])
        draw_plane_w(screen, line);

    // Copy backdrop on area outside the current resolution,
    // so we overwrite any pattern overflow.
    for (int x=0;x<screen_offset();x++)
    {
        screen[-x*4+3] = 0;
        draw_pixel(screen + -x*4, backdrop_color, 0);
    }
    for (int x=screen_width();x<SCREEN_WIDTH;x++)
    {
        screen[x*4+3] = 0;
        draw_pixel(screen + x*4, backdrop_color, 0);
    }

    return true;
}

void gfx_draw_scanline(uint8_t *screen, int line)
{
    // Overflow is the maximum size we can draw outside to avoid
    // wasting time and code in clipping. The maximum object is a 4x4 sprite,
    // so 32 pixels (on both side) is enough.
    enum { OVERFLOW = 32 };

    uint8_t buffer[(SCREEN_WIDTH+OVERFLOW*2)*4];
    if (GFX.draw_scanline(buffer+OVERFLOW*4, line))
        memcpy(screen, buffer+OVERFLOW*4, SCREEN_WIDTH*4);
}
