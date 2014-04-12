#include "hw.h"
#include <SDL.h>
extern "C" {
    #include "m68k/m68k.h"
    #include "z80/Z80.h"
}
#include "vdp.h"
#include "mem.h"

#define MAIN_CPU_FREQ     7670000
#define SLAVE_CPU_FREQ    (MAIN_CPU_FREQ/2)
Z80 z80;

int framecounter;

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: genenum ROM\n");
        return 1;
    }

    int romsize = load_rom(argv[1]);
    mem_init(romsize);

    hw_init();
    vdp_init();
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();

    while (hw_poll())
    {
        for (int sl=0;sl<VDP_SCANLINES;++sl)
        {
            m68k_execute(MAIN_CPU_FREQ / VDP_HZ / VDP_SCANLINES);

            // Technically, BUSREQ would pause the Z80 on memory accesses,
            // but we simplify by pausing it altogether
            if (!Z80_BUSREQ && Z80_RESET)
            {
                ExecZ80(&z80, SLAVE_CPU_FREQ / VDP_HZ / VDP_SCANLINES);
                IntZ80(&z80, INT_IRQ);
            }

            vdp_scanline();
        }

        uint8_t *screen;
        int pitch;

        int16_t *audio;
        hw_beginaudio(&audio);
        memset(audio, 0, HW_AUDIO_NUMSAMPLES*2);
        hw_endaudio();

        hw_beginframe(&screen, &pitch);
        //gfx_draw(screen, pitch);
        hw_endframe();
        ++framecounter;
    }

    return 0;
}
