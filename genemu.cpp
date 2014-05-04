#include "hw.h"
extern "C" {
    #include "ym2612/ym2612.h"
}
#include "vdp.h"
#include "cpu.h"
#include "gfx.h"
#include "mem.h"
#include "state.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ezOptionParser.hpp"

int framecounter;
uint64_t MASTER_CLOCK;   // VDP_MASTER_FREQ
char romname[2048];
extern int VERSION_PAL;

int main(int argc, const char *argv[])
{
    ez::ezOptionParser opt;

    opt.overview = "GenEmu -- Sega Genesis Emulator";
    opt.syntax = "genemu [OPTIONS] rom";
    opt.add("",0,0,0,"Display usage instructions.", "-h", "--help");
    opt.add("",0,-1,',',"Apply Game Genie codes [format=ABCD-EFGH]", "--gamegenie", "--gg");
    opt.add("",0,1,0,"Force console type [accepted values: PAL or NTSC]", "-m", "--mode", "--type");
    opt.add("",0,-1,',',"Make screenshots on the specified frames and exit", "--screenshots");

    opt.parse(argc, argv);
    if (opt.isSet("-h"))
    {
        std::string usage;
        opt.getUsage(usage, 120);
        std::cout << usage;
        return 0;
    }
    std::vector<std::string*> args;
    if (opt.firstArgs.size() >= 2)
    {
        strcpy(romname, opt.firstArgs[1]->c_str());
    }
    else if (opt.lastArgs.size() >= 1)
        strcpy(romname, opt.lastArgs[0]->c_str());
    else
    {
        std::cerr << "ERROR: no ROM specified\n";
        return 2;
    }

    int romsize;
    if (strstr(romname, ".smd"))
        romsize = load_smd(romname);
    else
        romsize = load_bin(romname);
    mem_init(romsize);

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

    if (opt.isSet("--mode"))
    {
        std::string mode;
        opt.get("--mode")->getString(mode);
        if (mode == "PAL")
        {
            VERSION_PAL = 1;
            std::cerr << "Forced mode: PAL\n";
        }
        else if (mode == "NTSC")
        {
            VERSION_PAL = 0;
            std::cerr << "Forced mode: NTSC\n";
        }
        else
        {
            std::cerr << "ERROR: invalid mode: " << mode << std::endl;
            return 2;
        }
    }

    std::vector<int> ss_frames;
    int ss_idx = 0;

    hw_init(YM2612_FREQ, VERSION_PAL ? 50 : 60);

    if (!opt.isSet("--screenshots"))
    {
        hw_enable_video(true);
        hw_enable_audio(true);
        gfx_enable(true);
    }
    else
    {
        opt.get("--screenshots")->getInts(ss_frames);
    }

    CPU_M68K.init();
    CPU_Z80.init();

    MASTER_CLOCK = 0;
    CPU_M68K.reset();
    VDP.reset();

    while (hw_poll())
    {
        if (ss_idx < ss_frames.size() && framecounter == ss_frames[ss_idx])
        {
            gfx_enable(true);
        }

        int numscanlines = VDP.num_scanlines();

        uint8_t *screen;
        int pitch;
        hw_beginframe(&screen, &pitch);

        int16_t *audio; int nsamples;
        hw_beginaudio(&audio, &nsamples);
        int audio_index = 0;
        int audio_step = (nsamples << 16) / numscanlines;

        for (int sl=0;sl<numscanlines;++sl)
        {
            CPU_M68K.run(MASTER_CLOCK + VDP_CYCLES_PER_LINE);
            CPU_Z80 .run(MASTER_CLOCK + VDP_CYCLES_PER_LINE);

            vdp_scanline(screen);
            screen += pitch;

            int prev_index = audio_index;
            audio_index += audio_step;
            YM2612Update(audio + ((prev_index+0x8000)>>16)*2, (audio_index-prev_index+0x8000)>>16);

            MASTER_CLOCK += VDP_CYCLES_PER_LINE;
        }

        hw_endaudio();
        hw_endframe();

        if (framecounter == 100 && opt.isSet("--gamegenie"))
        {
            std::vector<std::string> codes;
            opt.get("--gamegenie")->getStrings(codes);
            for (int i=0;i<codes.size();++i)
                mem_apply_gamegenie(codes[i].c_str());
        }

        if (ss_idx < ss_frames.size() && framecounter == ss_frames[ss_idx])
        {
            static char ssname[2048];
            sprintf(ssname, "%s.%d.%s.bmp", romname, framecounter, (VERSION_PAL ? "PAL" : "NTSC"));
            std::cerr << "Saving screenshot " << ssname << std::endl;
            hw_save_screenshot(ssname);
            ++ss_idx;
            if (ss_idx == ss_frames.size())
                break;
        }

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
