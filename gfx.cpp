#include "vdp.h"
#include "mem.h"
#include <assert.h>
#include <memory.h>
#include <stdio.h>

#define SCREEN_WIDTH 320

class GFX
{
private:
    void draw_pixel(uint8_t *screen, uint16_t rgb);
    template <bool fliph>
    void draw_pattern(uint8_t *screen, uint8_t *pattern, uint16_t *palette);
    void draw_pattern(uint8_t *screen, uint16_t name, int paty);
    void draw_nametable(uint8_t *screen, uint8_t *nt, int paty);
    void draw_plane_ab(uint8_t *screen, int ntaddr, int y);
    void draw_plane_w(uint8_t *screen, int y);
    void draw_sprites(uint8_t *screen, int line);

public:
    void draw_scanline(uint8_t *screen, int line);

} GFX;

#define COLOR_3B_TO_8B(c)  (((c) << 5) | ((c) << 2) | ((c) >> 1))
#define CRAM_R(c)          COLOR_3B_TO_8B(BITS((c), 1, 3))
#define CRAM_G(c)          COLOR_3B_TO_8B(BITS((c), 5, 3))
#define CRAM_B(c)          COLOR_3B_TO_8B(BITS((c), 9, 3))


inline void GFX::draw_pixel(uint8_t *screen, uint16_t rgb)
{
    screen[0] = CRAM_R(rgb);
    screen[1] = CRAM_G(rgb);
    screen[2] = CRAM_B(rgb);
}

template <bool fliph>
void GFX::draw_pattern(uint8_t *screen, uint8_t *pattern, uint16_t *palette)
{
    if (fliph)
        pattern += 3;

    for (int x = 0; x < 4; ++x)
    {
        uint8_t pix = *pattern;
        uint8_t pix1 = !fliph ? pix>>4 : pix&0xF;
        uint8_t pix2 = !fliph ? pix&0xF : pix>>4;

        if (pix1) draw_pixel(screen+0, palette[pix1]);
        if (pix2) draw_pixel(screen+4, palette[pix2]);

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
        draw_pattern<false>(screen, pattern, palette);
    else
        draw_pattern<true>(screen, pattern, palette);
}


void GFX::draw_nametable(uint8_t *screen, uint8_t *nt, int paty)
{
    for (int i = 0; i < 40; ++i)
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

    draw_nametable(screen, VDP.VRAM + addr_w + row*(2*40), paty);
}

void GFX::draw_plane_ab(uint8_t *screen, int ntaddr, int y)
{
    int row = y >> 3;
    int paty = y & 7;
    int ntwidth = BITS(VDP.regs[16], 0, 2);
    int ntheight = BITS(VDP.regs[16], 4, 2);

    assert(ntwidth != 2);  // invalid setting
    assert(ntheight != 2); // invalid setting

    ntwidth  = (ntwidth + 1) * 32;
    ntheight = (ntheight + 1) * 32;

    draw_nametable(screen, VDP.VRAM + ntaddr + row*(2*ntwidth), paty);
}

void GFX::draw_sprites(uint8_t *screen, int line)
{
    uint8_t *start_table = VDP.VRAM + ((VDP.regs[5] & 0x7F) << 9);
    int sidx = 0;
    int num_visible = 0;
    int num_pixels = 0;

    for (int ns = 0; ns < 64; ++ns)
    {
        uint8_t *table = start_table + sidx*8;
        int sy = ((table[0] & 0x3) << 8) | table[1];
        int sh = BITS(table[2], 0, 2) + 1;
        uint16_t name = (table[4] << 8) | table[5];
        int sw = BITS(table[2], 2, 2) + 1;
        int sx = ((table[6] & 0x3) << 8) | table[7];
        int link = BITS(table[3], 0, 7);

        if (line == 0)
        {
            // mem_log("SPRITE", "(sx:%d, sy:%d sz:%d,%d, name:%04x, link:%02x)\n",
            //     sx, sy, sw,sh, name, link);
        }

        if (!link) break;

        sy -= 128;
        sx -= 128;

        if (line >= sy && line < sy+sh*8)
        {
            int row = (line - sy) >> 3;
            int paty = (line - sy) & 7;

            if (sx > -sw*8 && sx < SCREEN_WIDTH)
            {
                name += row;
                for (int p=0;p<sw;p++)
                {
                    draw_pattern(screen + (sx+p*8)*4, name, paty);
                    num_pixels += 8;
                    if (num_pixels >= 256)
                        return;
                    name += sh;
                }
            }

            // Max 16 sprites per scanline
            if (++num_visible >= 16)
                return;
        }

        sidx = link;
    }

}

void GFX::draw_scanline(uint8_t *screen, int line)
{
    int winh = VDP.regs[17] & 0x1F;
    int winhright = VDP.regs[17] >> 7;

    int winv = VDP.regs[18] & 0x1F;
    int winvdown = VDP.regs[18] >> 7;
    bool linew;

    if (BITS(VDP.regs[12], 1, 2) != 0)
        assert(!"interlace mode");

    if (line >= 224)
        return;

    if (line == 0) {
        int addr_a = VDP.get_nametable_A();
        int addr_b = VDP.get_nametable_B();
        int addr_w = VDP.get_nametable_W();
        mem_log("GFX", "A(addr:%04x) B(addr:%04x) W(addr:%04x)\n", addr_a, addr_b, addr_w);
        mem_log("GFX", "W(h:%d, right:%d, v:%d, down:%d\n)", winh, winhright, winv, winvdown);

        FILE *f;
        f=fopen("vram.dmp", "wb");
        fwrite(VDP.VRAM, 1, 65536, f);
        fclose(f);
        f=fopen("cram.dmp", "wb");
        fwrite(VDP.CRAM, 1, 64*2, f);
        fclose(f);
    }

    // Display enable
    if (BIT(VDP.regs[0], 0))
    {
        memset(screen, 0, SCREEN_WIDTH*4);
        return;
    }

    uint16_t backdrop_color = VDP.CRAM[BITS(VDP.regs[7], 0, 6)];
    for (int x=0;x<SCREEN_WIDTH;x++)
        draw_pixel(screen + x*4, backdrop_color);

    // Plaen/sprite disable, show only backdrop
    if (!BIT(VDP.regs[1], 6))
        return;

    linew = false;
    if (winv) {
        if (winvdown && line >= winv*8)
        {
            assert(!"winv down scroll");
            draw_plane_w(screen, line);
            linew = true;
        }
        else if (!winvdown && line <= winv*8)
        {
            draw_plane_w(screen, line);
            linew = true;
        }
    }

    if (!linew)
        draw_plane_ab(screen, VDP.get_nametable_A(), line);

    draw_plane_ab(screen, VDP.get_nametable_B(), line);

    draw_sprites(screen, line);
}

void gfx_draw_scanline(uint8_t *screen, int line)
{
    uint8_t buffer[(SCREEN_WIDTH+32+32)*4];
    GFX.draw_scanline(buffer+32*4, line);
    memcpy(screen, buffer+32*4, SCREEN_WIDTH*4);
}
