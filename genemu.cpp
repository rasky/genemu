#include "hw.h"
extern "C" {
    #include "ym2612/ym2612.h"
}
#include "vdp.h"
#include "cpu.h"
#include "mem.h"
#include "state.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define MAIN_CPU_FREQ     (VDP_MASTER_FREQ / M68K_FREQ_DIVISOR)
#define SLAVE_CPU_FREQ    (VDP_MASTER_FREQ / Z80_FREQ_DIVISOR)

int framecounter;
uint64_t MASTER_CLOCK;   // VDP_MASTER_FREQ
char romname[2048];


int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: genenum ROM\n");
        return 1;
    }

    int romsize;

    if (strstr(argv[1], ".smd"))
        romsize = load_smd(argv[1]);
    else
        romsize = load_bin(argv[1]);
    strcpy(romname, argv[1]);
    mem_init(romsize);

    for (int i=2;i<argc;++i)
        mem_apply_gamegenie(argv[i]);

#if 0
    uint16_t checksum = 0;
    for (int i=0;i<(romsize-512)/2;i++)
        checksum += m68k_read_memory_16(512+i*2);
    assert(checksum == m68k_read_memory_16(0x18e));
#endif

#if 1
    char buf[256];
    int pc = 0;
    strcpy(buf, argv[1]);
    strcat(buf, ".asm");
    FILE *f = fopen(buf, "w");
    while (pc < romsize) {
        int oplen = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
        fprintf(f, "%06x\t%s\n", pc, buf);
        pc += oplen;
    }
    fclose(f);
#endif

    CPU_M68K.init();
    CPU_Z80.init();

    hw_init();

    MASTER_CLOCK = 0;
    CPU_M68K.reset();
    VDP.reset();

    while (hw_poll())
    {
        VDP.frame_begin();
        int numscanlines = VDP.num_scanlines();

        uint8_t *screen;
        int pitch;
        hw_beginframe(&screen, &pitch);

        int16_t *audio;
        hw_beginaudio(&audio);
        int audio_index = 0;
        int audio_step = (HW_AUDIO_NUMSAMPLES << 16) / numscanlines;

        for (int sl=0;sl<numscanlines;++sl)
        {
            VDP.scanline_begin(screen);

            int hblank_clocks = VDP.scanline_hblank_clocks();

            CPU_M68K.run(MASTER_CLOCK + hblank_clocks);
            CPU_Z80 .run(MASTER_CLOCK + hblank_clocks);
            MASTER_CLOCK += hblank_clocks;

            VDP.scanline_hblank(screen);

            CPU_M68K.run(MASTER_CLOCK + VDP_CYCLES_PER_LINE - hblank_clocks);
            CPU_Z80 .run(MASTER_CLOCK + VDP_CYCLES_PER_LINE - hblank_clocks);
            MASTER_CLOCK += VDP_CYCLES_PER_LINE - hblank_clocks;

            VDP.scanline_end(screen);
            screen += pitch;

            int prev_index = audio_index;
            audio_index += audio_step;
            YM2612Update(audio + ((prev_index+0x8000)>>16)*2, (audio_index-prev_index+0x8000)>>16);
        }

        VDP.frame_end();

        hw_endaudio();
        hw_endframe();
        ++framecounter;

        state_poll();
    }

#if 0
    checksum = 0;
    for (int i=0;i<(romsize-512)/2;i++)
        checksum += m68k_read_memory_16(512+i*2);
    assert(checksum == m68k_read_memory_16(0x18e));
#endif

    return 0;
}


extern "C" void gentrace(void);
void gentrace(void)
{
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);

    char buf[2048];
    int oplen = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
    fprintf(stdout, "%06x\t%s\t\t[A0=%08x]\n", pc, buf, m68k_get_reg(NULL, M68K_REG_A0));
}
