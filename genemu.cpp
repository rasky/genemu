#include "hw.h"
#include <SDL.h>
extern "C" {
    #include "m68k/m68k.h"
}
#include "vdp.h"
#include "mem.h"

#define MAIN_CPU_FREQ     7670000
#define SLAVE_CPU_FREQ    (MAIN_CPU_FREQ/2)

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
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();

    while (hw_poll())
    {
        m68k_execute(MAIN_CPU_FREQ / 60);

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
