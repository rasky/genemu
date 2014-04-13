#include "vdp.h"
#include "mem.h"
#include <assert.h>
#include <stdio.h>

class GFX
{
private:
    uint8_t stencil[320];

    void draw_nametable(uint8_t *screen, uint8_t *nt, int paty);
    void draw_plane_ab(uint8_t *screen, int ntaddr, int y);
    void draw_plane_w(uint8_t *screen, int y);

public:
    void draw_scanline(uint8_t *screen, int line);

} GFX;

#define COLOR_3B_TO_8B(c)  (((c) << 5) | ((c) << 2) | ((c) >> 1))
#define CRAM_R(c)          COLOR_3B_TO_8B(BITS((c), 1, 3))
#define CRAM_G(c)          COLOR_3B_TO_8B(BITS((c), 5, 3))
#define CRAM_B(c)          COLOR_3B_TO_8B(BITS((c), 9, 3))

void GFX::draw_nametable(uint8_t *screen, uint8_t *nt, int paty)
{
    int i,x;

    for (i = 0; i < 40; ++i)
    {
        uint16_t wnt = (nt[0] << 8) | nt[1];
        int pat_idx = BITS(wnt, 0, 11);
        int pat_fliph = BITS(wnt, 11, 1);
        int pat_flipv = BITS(wnt, 12, 1);
        int pat_palette = BITS(wnt, 13, 2);
        int pat_pri = BITS(wnt, 15, 1);
        uint8_t *pattern = VDP.VRAM + pat_idx * 32;
        uint16_t *palette = VDP.CRAM + pat_palette * 16;

        assert(!pat_fliph);

        if (!pat_flipv)
            pattern += paty*4;
        else
            pattern += (7-paty)*4;

        for (x = 0; x < 4; x++)
        {
            uint8_t pix = pattern[x];

            if (pix >> 4)
            {
                uint16_t rgb = palette[pix >> 4];
                screen[0] = CRAM_R(rgb);
                screen[1] = CRAM_G(rgb);
                screen[2] = CRAM_B(rgb);
                screen[3] = 0;
            }
            screen += 4;

            if (pix & 0xF)
            {
                uint16_t rgb = palette[pix & 0xF];
                screen[0] = CRAM_R(rgb);
                screen[1] = CRAM_G(rgb);
                screen[2] = CRAM_B(rgb);
                screen[3] = 0;
            }
            screen += 4;
        }

        nt += 2;
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
}

void gfx_draw_scanline(uint8_t *screen, int line)
{
    GFX.draw_scanline(screen, line);
}
