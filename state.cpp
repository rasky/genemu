#include <stdio.h>
#include <assert.h>
#include "vdp.h"
#include "cpu.h"
#include "hw.h"
#include <SDL.h>

extern "C" {
    #include "m68k/m68k.h"
    #include "m68k/m68kcpu.h"
    #include "ym2612/ym2612.h"
}

extern int Z80_BANK;
extern uint8_t RAM[0x10000];
extern uint8_t ZRAM[0x2000];
extern char romname[2048];

static void pad(FILE *f, int bytes)
{
    uint32_t zero=0;
    for (int i=0;i<bytes;i++)
        fwrite(&zero, 1, 1, f);
}

// Use the Genecyst format
//
void savestate(const char *fn)
{
    uint32_t val;
    FILE *f = fopen(fn, "wb");
    assert(f);

    fprintf(f, "GST");
    assert(ftell(f) == 3);
    pad(f, 3);
    assert(ftell(f) == 6);
    fprintf(f, "\xE0\x40");
    pad(f, 0x80-8);
    assert(ftell(f) == 0x80);

    for (int i=0;i<16;i++)
    {
        val = m68k_get_reg(NULL, (m68k_register_t)i);
        fwrite(&val, 1, 4, f);
    }
    assert(ftell(f) == 0xC0);
    pad(f, 8);
    assert(ftell(f) == 0xC8);
    val = m68k_get_reg(NULL, M68K_REG_PC);
    fwrite(&val, 1, 4, f);
    pad(f, 4);
    assert(ftell(f) == 0xD0);
    val = m68k_get_reg(NULL, M68K_REG_SR);
    fwrite(&val, 1, 2, f);
    val = m68k_get_reg(NULL, M68K_REG_USP);
    fwrite(&val, 1, 4, f);
    val = m68k_get_reg(NULL, M68K_REG_ISP);
    fwrite(&val, 1, 4, f);

    assert(ftell(f) == 0xDA);
    pad(f, 0xFA-0xDA);
    assert(ftell(f) == 0xFA);

    fwrite(VDP.regs, 1, 24, f);
    fwrite(VDP.CRAM, 1, 128, f);
    fwrite(VDP.VSRAM, 1, 80, f);

    pad(f, 2);
    assert(ftell(f) == 0x1E4);

    uint8_t opnregs[512];
    YM2612SaveRegs(opnregs);
    fwrite(opnregs, 1, 512, f);

    assert(ftell(f) == 0x3E4);
    pad(f, 0x404-0x3E4);
    assert(ftell(f) == 0x404);

    fwrite(&CPU_Z80._cpu.AF.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.BC.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.DE.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.HL.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.IX.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.IY.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.PC.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.SP.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.AF1.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.BC1.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.DE1.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.HL1.W, 1, 4, f);
    fwrite(&CPU_Z80._cpu.I, 1, 1, f);
    pad(f, 1);
    fwrite(&CPU_Z80._cpu.IFF, 1, 1, f);
    pad(f, 1);

    fwrite(&CPU_Z80._reset_line, 1, 1, f);
    fwrite(&CPU_Z80._busreq_line, 1, 1, f);
    pad(f, 1);
    pad(f, 1);
    fwrite(&Z80_BANK, 1, 4, f);

    assert(ftell(f) == 0x440);
    pad(f, 0x474-0x440);
    assert(ftell(f) == 0x474);

    fwrite(ZRAM, 1, sizeof(ZRAM), f);
    pad(f, 4);
    fwrite(RAM, 1, sizeof(RAM), f);
    fwrite(VDP.VRAM, 1, sizeof(VDP.VRAM), f);
    assert(ftell(f) == 0x22478);
    fclose(f);
}

void loadstate(const char *fn)
{
    uint32_t val;
    FILE *f = fopen(fn, "rb");
    if (!f) return;

    fseek(f, 0x80, SEEK_SET);
    CPU_M68K.init();
    for (int i=0;i<16;i++)
    {
        fread(&val, 1, 4, f);
        m68k_set_reg((m68k_register_t)i, val);
    }

    fseek(f, 0xC8, SEEK_SET);
    fread(&val, 1, 4, f);
    m68k_set_reg(M68K_REG_PC, val);

    fseek(f, 0xD0, SEEK_SET);
    val = 0; fread(&val, 1, 2, f);
    //m68k_set_reg(M68K_REG_SR, val);
    m68ki_set_sr_noint_nosp(val);

    val = 0; fread(&val, 1, 4, f);
    m68k_set_reg(M68K_REG_USP, val);
    printf("USP: %08x\n", val);
    val = 0; fread(&val, 1, 4, f);
    m68k_set_reg(M68K_REG_ISP, val);
    printf("ISP: %08x\n", val);

    assert(ftell(f) == 0xDA);
    fseek(f, 0xFA, SEEK_SET);

    VDP.reset();
    fread(VDP.regs, 1, 24, f);
    fread(VDP.CRAM, 1, 128, f);
    fread(VDP.VSRAM, 1, 80, f);
    assert(ftell(f) == 0x1E2);

    fseek(f, 0x1E4, SEEK_SET);

    uint8_t opnregs[512];
    fread(opnregs, 1, 512, f);
    YM2612LoadRegs(opnregs);

    assert(ftell(f) == 0x3E4);
    fseek(f, 0x404, SEEK_SET);

    CPU_Z80.reset();
    fread(&CPU_Z80._cpu.AF.W, 1, 4, f);
    fread(&CPU_Z80._cpu.BC.W, 1, 4, f);
    fread(&CPU_Z80._cpu.DE.W, 1, 4, f);
    fread(&CPU_Z80._cpu.HL.W, 1, 4, f);
    fread(&CPU_Z80._cpu.IX.W, 1, 4, f);
    fread(&CPU_Z80._cpu.IY.W, 1, 4, f);
    fread(&CPU_Z80._cpu.PC.W, 1, 4, f);
    fread(&CPU_Z80._cpu.SP.W, 1, 4, f);
    fread(&CPU_Z80._cpu.AF1.W, 1, 4, f);
    fread(&CPU_Z80._cpu.BC1.W, 1, 4, f);
    fread(&CPU_Z80._cpu.DE1.W, 1, 4, f);
    fread(&CPU_Z80._cpu.HL1.W, 1, 4, f);
    fread(&CPU_Z80._cpu.I, 1, 1, f);
    fseek(f, 1, SEEK_CUR);
    fread(&CPU_Z80._cpu.IFF, 1, 1, f);
    fseek(f, 1, SEEK_CUR);

    fread(&CPU_Z80._reset_line, 1, 1, f);
    fread(&CPU_Z80._busreq_line, 1, 1, f);
    fseek(f, 2, SEEK_CUR);
    fread(&Z80_BANK, 1, 4, f);
    CPU_Z80._reset_once = true;

    assert(ftell(f) == 0x440);
    fseek(f, 0x474, SEEK_SET);

    fread(ZRAM, 1, sizeof(ZRAM), f);
    fseek(f, 4, SEEK_CUR);
    fread(RAM, 1, sizeof(RAM), f);
    fread(VDP.VRAM, 1, sizeof(VDP.VRAM), f);
    assert(ftell(f) == 0x22478);
    fclose(f);
}

char* slotname(int slot)
{
    static char savename[2048];
    char *ext;

    strcpy(savename, romname);
    ext = &savename[strlen(savename)-3];
    *ext++ = 'g'; *ext++ = 's';
    *ext++ = '0' + slot;
    return savename;
}

void state_poll()
{
    static const int savekeys[] = {
        SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
        SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9
    };

    if (keystate[SDL_SCANCODE_LCTRL])
    {
        for (int i=0;i<10;i++)
            if (keyreleased[savekeys[i]])
                savestate(slotname(i));
    }
    else if (keystate[SDL_SCANCODE_LSHIFT])
    {
        for (int i=0;i<10;i++)
            if (keyreleased[savekeys[i]])
                loadstate(slotname(i));
    }
}
