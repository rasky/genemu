#include "mem.h"
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <memory.h>
extern "C" {
    #include "ym2612/ym2612.h"
}
#include "vdp.h"
#include "cpu.h"
#include "ioports.h"

uint8_t *ROM;
uint8_t RAM[0x10000];
uint8_t ZRAM[0x2000];
int Z80_BANK;
extern int activecpu;

inline uint16_t SWAP16(uint16_t a) {
    return (a >> 8) | (a << 8);
}

void *m68k_memtable[256];
void *z80_memtable[16];
typedef unsigned int (*memfunc_r)(unsigned int address);
typedef void         (*memfunc_w)(unsigned int address, unsigned int value);

struct memfunc_pair {
    memfunc_r read8, read16;
    memfunc_w write8, write16;
};

#define MEMFUN_PAIR(x)         ((void*)((unsigned long)(x) | 1))
#define GET_MEMFUNC_PAIR(x)    ((memfunc_pair*)((unsigned long)(x) & ~1))

void mem_z80area(bool active);

/********************************************
 * Z80 area access from m68k
 ********************************************/

static unsigned int z80area_mem_r16(unsigned int address)
{
    address &= 0x7FFF;
    assert(!"68000 word read from z80 area");
    unsigned int value = RdZ80(address);
    return (value << 8) | value;
}

static unsigned int z80area_mem_r8(unsigned int address)
{
    address &= 0x7FFF;
    return RdZ80(address);
}

static void z80area_mem_w16(unsigned int address, unsigned int value)
{
    address &= 0x7FFF;
    // Only MSB is written, because the memory is connected through a 8-bit bus
    // es: gunstarheroes
    WrZ80(address, value >> 8);
}

static void z80area_mem_w8(unsigned int address, unsigned int value)
{
    address &= 0x7FFF;
    WrZ80(address, value & 0xFF);
}


/********************************************
 * Expansion area
 ********************************************/

static unsigned int exp_mem_r8(unsigned int address)
{
    mem_log("MEM", "read8 from expansion area %06x\n", address);
    return 0xFF;
}
static unsigned int exp_mem_r16(unsigned int address)
{
    mem_log("MEM", "read16 from expansion area %06x\n", address);
    return m68k_read_memory_16(CPU_M68K.PC()) & 0xFF00;
}
static void exp_mem_w8(unsigned int address, unsigned int value)
{
    mem_log("MEM", "write8 from expansion area %06x: %04x\n", address, value);
}
static void exp_mem_w16(unsigned int address, unsigned int value)
{
    mem_log("MEM", "write16 from expansion area %06x: %04x\n", address, value);
}

/********************************************
 * I/O
 ********************************************/

static unsigned int io_mem_r8(unsigned int address)
{
    address &= 0xFFFF;

    // Version register
    if ((address & ~1) == 0x0)
        return 0xA0;

    if (address < 0x20)
        return ioports_read(address);

    if (address == 0x1100)
        return 0x00 | (~CPU_Z80.get_busreq_line() & 1);
    if (address == 0x1101)
        return 0x00;

    if (address == 0x1200)
        return 0x00 | (CPU_Z80.get_reset_line() & 1);
    if (address == 0x1201)
        return 0x00;

    return exp_mem_r8(address);
}

static unsigned int io_mem_r16(unsigned int address)
{
    mem_log("MEM", "read16 from I/O area: %04x\n", address);
    return 0xFFFF;
}

static void io_mem_w8(unsigned int address, unsigned int value)
{
    address &= 0xFFFF;

    if (address < 0x20)
    {
        ioports_write(address, value);
        return;
    }

    if (address == 0x1100)
    {
        assert(activecpu == 0);
        CPU_Z80.sync();
        CPU_Z80.set_busreq_line(value & 1);
        mem_z80area(value & 1);
        return;
    }

    if (address == 0x1200)
    {
        assert(activecpu == 0);
        CPU_Z80.sync();
        CPU_Z80.set_reset_line(~value & 1);
        return;
    }

    mem_log("MEM", "write8 to I/O area %04x: %04x\n", address, value);
}
static void io_mem_w16(unsigned int address, unsigned int value)
{
    address &= 0xFFFF;
    if (address < 0x20)
    {
        mem_log("MEM", "write16 to I/O area %04x: %04x\n", address, value);
        return;
    }

    if (address == 0x1100 || address == 0x1200)
    {
        io_mem_w8(address, value >> 8);
        return;
    }

    exp_mem_w16(address, value);
}

/********************************************
 * Z80 Bank
 ********************************************/

unsigned int zbankreg_mem_r8(unsigned int address)
{
    return 0xFF;
}

void zbankreg_mem_w8(unsigned int address, unsigned int value)
{
    address &= 0xFFF;
    if (address < 0x100)
    {
        Z80_BANK >>= 1;
        Z80_BANK |= (value & 1) << 8;
        // mem_log("Z80", "bank points to: %06x\n", Z80_BANK << 15);
        return;
    }
}

unsigned int zbank_mem_r8(unsigned int address)
{
    address &= 0x7FFF;
    address |= (Z80_BANK << 15);

    // mem_log("Z80", "bank read: %06x\n", address);
    return m68k_read_memory_8(address);
}
void zbank_mem_w8(unsigned int address, unsigned int value)
{
    address &= 0x7FFF;
    address |= (Z80_BANK << 15);

    // mem_log("Z80", "bank write %06x: %02x\n", address, value);
    m68k_write_memory_8(address, value);
}

unsigned int zvdp_mem_r8(unsigned int address)
{
    if (address >= 0x7F00 && address < 0x7F20)
        return vdp_mem_r8(address);
    return 0xFF;
}

void zvdp_mem_w8(unsigned int address, unsigned int value)
{
    if (address >= 0x7F00 && address < 0x7F20)
        vdp_mem_w8(address, value);
}

/********************************************
 * YM2612
 ********************************************/

unsigned int ym2612_mem_r8(unsigned int address)
{
    return YM2612Read();
}
void ym2612_mem_w8(unsigned int address, unsigned int value)
{
    address &= 0x3;
    //mem_log("YM2612", "reg write %d: %02x\n", address, value);
    YM2612Write(address, value);
}


/********************************************
 * M68K memory access
 ********************************************/

template<class TYPE>
unsigned int m68k_read_memory(unsigned int address)
{
    assert((address >> 16) < 256);
    void *t = m68k_memtable[address >> 16];
    if (t) {
        if (!((unsigned long)t & 1)) {
            if (sizeof(TYPE) == 2)
                return SWAP16(*(uint16_t*)((uint8_t*)t + (address & 0xFFFF)));
            return *((TYPE*)t + (address & 0xFFFF));
        }
        if (sizeof(TYPE) == 2)
            return GET_MEMFUNC_PAIR(t)->read16(address);
        return GET_MEMFUNC_PAIR(t)->read8(address);
    }
    mem_log("MEM", "unknown read%ld at %06x\n", sizeof(TYPE)*8, address);
    return 0xFFFFFFFF & ((1L<<(sizeof(TYPE)*8)) - 1);
}

template<class TYPE>
void m68k_write_memory(unsigned int address, unsigned int value)
{
    assert((address >> 16) < 256);
    void *t = m68k_memtable[address >> 16];
    if (t) {
        if (!((unsigned long)t & 1)) {
            if (sizeof(TYPE) == 2)
                *(uint16_t*)((uint8_t*)t + (address & 0xFFFF)) = SWAP16(value & 0xFFFF);
            else
                *(uint8_t*)((uint8_t*)t + (address & 0xFFFF)) = value & 0xFF;
            return;
        }
        if (sizeof(TYPE) == 2)
           GET_MEMFUNC_PAIR(t)->write16(address, value);
        else
           GET_MEMFUNC_PAIR(t)->write8(address, value);
        return;
    }
    mem_log("MEM", "unknown write%ld at %06x: %0*x\n",
        sizeof(TYPE)*8, address, (int)sizeof(TYPE)*2, value);
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    return m68k_read_memory<uint8_t>(address);
}
unsigned int  m68k_read_memory_16(unsigned int address)
{
    return m68k_read_memory<uint16_t>(address);
}
unsigned int  m68k_read_memory_32(unsigned int address)
{
    return (m68k_read_memory<uint16_t>(address) << 16) | m68k_read_memory<uint16_t>(address+2);
}
unsigned int  m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}
unsigned int  m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}


void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    m68k_write_memory<uint8_t>(address, value);
}
void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    m68k_write_memory<uint16_t>(address, value);
}
void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_write_memory<uint16_t>(address, value >> 16);
    m68k_write_memory<uint16_t>(address+2, value & 0xFFFF);
}

void WrZ80(register word Addr,register byte Value)
{
    void *t = z80_memtable[Addr >> 12];
    if (t) {
        if (!((unsigned long)t & 1)) {
            *(uint8_t*)((uint8_t*)t + (Addr & 0xFFF)) = Value;
            return;
        }
        GET_MEMFUNC_PAIR(t)->write8(Addr, Value);
        return;
    }
    mem_log("Z80MEM", "unknown write at %04x: %02x\n", Addr, Value);
}
byte RdZ80(register word Addr)
{
    void *t = z80_memtable[Addr >> 12];
    if (t) {
        if (!((unsigned long)t & 1))
            return *(uint8_t*)((uint8_t*)t + (Addr & 0xFFF));
        return GET_MEMFUNC_PAIR(t)->read8(Addr);
    }
    mem_log("Z80MEM", "unknown read at %04x\n", Addr);
    return 0xFF;
}
void OutZ80(register word Port,register byte Value)
{
    Port &= 0xFF;
    mem_log("Z80MEM", "unknown I/O write at Port %04x: %02x\n", Port, Value);
}
byte InZ80(register word Port)
{
    Port &= 0xFF;
    mem_log("Z80MEM", "unknown I/O read at Port %04x\n", Port);
    return 0xFF;
}

void PatchZ80(register Z80 *R) {}

int load_bin(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    if (!f)
    {
        fprintf(stderr, "cannot load ROM: %s\n", fn);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    int len = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Round up len to 16bit
    if (len & 0xFFFF)
        len = (len | 0xFFFF) + 1;

    ROM = (uint8_t*)malloc(len);
    memset(ROM, 0xFF, len);
    fread(ROM, 1, len, f);
    fclose(f);
    return len;
}

int load_smd(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    if (!f)
    {
        fprintf(stderr, "cannot load ROM: %s\n", fn);
        exit(1);
    }

    uint8_t nblocks;
    fread(&nblocks, 1, 1, f);
    if (nblocks == 0)
        nblocks = 128;
    fseek(f, 512, SEEK_SET);

    ROM = (uint8_t*)malloc(nblocks * 16*1024);
    uint8_t *buf = ROM;

    for (int i=0;i<nblocks;++i)
    {
        for (int j=0;j<8*1024;++j)
            buf[j*2+1] = fgetc(f);
        for (int j=0;j<8*1024;++j)
            buf[j*2+0] = fgetc(f);
        buf += 16*1024;
    }
    fclose(f);
    return nblocks*16*1024;
}


static memfunc_pair MVDP = { vdp_mem_r8, vdp_mem_r16, vdp_mem_w8, vdp_mem_w16 };
static memfunc_pair IO = { io_mem_r8, io_mem_r16, io_mem_w8, io_mem_w16 };
static memfunc_pair EXP = { exp_mem_r8, exp_mem_r16, exp_mem_w8, exp_mem_w16 };
static memfunc_pair Z80AREA = { z80area_mem_r8, z80area_mem_r16, z80area_mem_w8, z80area_mem_w16 };
static memfunc_pair ZBANKREG = { zbankreg_mem_r8, NULL, zbankreg_mem_w8, NULL };
static memfunc_pair ZBANK = { zbank_mem_r8, NULL, zbank_mem_w8, NULL };
static memfunc_pair ZVDP = { zvdp_mem_r8, NULL, zvdp_mem_w8, NULL };
static memfunc_pair YM2612 = { ym2612_mem_r8, NULL, ym2612_mem_w8, NULL };

void mem_z80area(bool active)
{
    if (active)
    {
        m68k_memtable[0xA0] = MEMFUN_PAIR(&Z80AREA);
    }
    else
    {
        m68k_memtable[0xA0] = NULL;
    }
}

void mem_init(int romsize)
{
    romsize /= 65536;
    for (int i=0;i<romsize;++i)
        m68k_memtable[i] = ROM + i*65536;
    m68k_memtable[0xA1] = MEMFUN_PAIR(&IO);
    for (int i=0xA2;i<0xC0;i++)
        m68k_memtable[i] = MEMFUN_PAIR(&EXP);
    m68k_memtable[0xC0] = MEMFUN_PAIR(&MVDP);
    m68k_memtable[0xC8] = MEMFUN_PAIR(&MVDP);
    m68k_memtable[0xD0] = MEMFUN_PAIR(&MVDP);
    m68k_memtable[0xD8] = MEMFUN_PAIR(&MVDP);
    for (int i=0xE0;i<0x100;++i)
        m68k_memtable[i] = RAM;

    z80_memtable[0x0] = ZRAM;
    z80_memtable[0x1] = ZRAM + 0x1000;
    z80_memtable[0x2] = ZRAM;
    z80_memtable[0x3] = ZRAM + 0x1000;
    z80_memtable[0x4] = MEMFUN_PAIR(&YM2612);
    z80_memtable[0x5] = MEMFUN_PAIR(&YM2612);
    z80_memtable[0x6] = MEMFUN_PAIR(&ZBANKREG);
    z80_memtable[0x7] = MEMFUN_PAIR(&ZVDP);
    for (int i=0x8;i<0x10;++i)
        z80_memtable[i] = MEMFUN_PAIR(&ZBANK);

    YM2612Init();
    YM2612Config(9);
    YM2612ResetChip();
}

void mem_log(const char *subs, const char *fmt, ...)
{
    extern int framecounter;
    va_list va;

    if (activecpu == 0)
        fprintf(stdout, "[%s][MPC=%06x](%04d) ", subs, CPU_M68K.PC(), framecounter);
    else
        fprintf(stdout, "[%s][ZPC=%06x](%04d) ", subs, CPU_Z80.PC(), framecounter);
    va_start(va, fmt);
    vfprintf(stdout, fmt, va);
    va_end(va);
}
