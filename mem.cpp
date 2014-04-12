#include "mem.h"
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
extern "C" {
    #include "m68k/m68k.h"
    #include "Z80/Z80.h"
}
#include "vdp.h"


uint8_t *ROM;
uint8_t RAM[0x10000];
uint8_t ZRAM[0x2000];

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

/********************************************
 * Z80 area access from m68k
 ********************************************/

static unsigned int z80area_mem_r16(unsigned int address)
{
    address &= 0x7FFF;
    return (RdZ80(address) << 8) | RdZ80(address + 1);
}

static unsigned int z80area_mem_r8(unsigned int address)
{
    address &= 0x7FFF;
    return RdZ80(address);
}

static void z80area_mem_w16(unsigned int address, unsigned int value)
{
    address &= 0x7FFF;
    WrZ80(address, value >> 8);
    WrZ80(address, value & 0xFF);
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
    mem_log("MEM", "read8 from expansion area %04x\n", address);
    return 0xFF;
}
static unsigned int exp_mem_r16(unsigned int address)
{
    mem_log("MEM", "read16 from expansion area %04x\n", address);
    return m68k_read_memory_16(m68k_get_reg(NULL, M68K_REG_PC)) & 0xFF00;
}
static void exp_mem_w8(unsigned int address, unsigned int value)
{
    mem_log("MEM", "write8 from expansion area %04x: %04x\n", address, value);
}
static void exp_mem_w16(unsigned int address, unsigned int value)
{
    mem_log("MEM", "write16 from expansion area %04x: %04x\n", address, value);
}

/********************************************
 * I/O
 ********************************************/

static unsigned int io_mem_r8(unsigned int address)
{
    if (address < 0x20)
    {
        mem_log("MEM", "read8 from I/O area: %04x\n", address);
        return 0xFF;
    }

    if (address == 0x1100 || address == 0x1200)
    {
        /* special */
        mem_log("MEM", "read8 from I/O special: %04x\n", address);
        return 0x00;
    }

    return exp_mem_r8(address);
}

static unsigned int io_mem_r16(unsigned int address)
{
    mem_log("MEM", "read16 from I/O area: %04x\n", address);
    return 0xFFFF;
}

static void io_mem_w8(unsigned int address, unsigned int value)
{
    mem_log("MEM", "write8 to I/O area %04x: %04x\n", address, value);
}
static void io_mem_w16(unsigned int address, unsigned int value)
{
    if (address < 0x20)
    {
        mem_log("MEM", "write16 to I/O area %04x: %04x\n", address, value);
        return;
    }

    exp_mem_w16(address, value);
}


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
            return GET_MEMFUNC_PAIR(t)->read16(address & 0xFFFF);
        return GET_MEMFUNC_PAIR(t)->read8(address & 0xFFFF);
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
           GET_MEMFUNC_PAIR(t)->write16(address & 0xFFFF, value);
        else
           GET_MEMFUNC_PAIR(t)->write8(address & 0xFFFF, value);
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
        GET_MEMFUNC_PAIR(t)->write8(Addr & 0xFFF, Value);
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
        return GET_MEMFUNC_PAIR(t)->read8(Addr & 0xFFF);
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

int load_rom(const char *fn)
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
    ROM = (uint8_t*)malloc(len);
    fread(ROM, 1, len, f);
    fclose(f);
    return len;
}


static memfunc_pair VDP = { vdp_mem_r8, vdp_mem_r16, vdp_mem_w8, vdp_mem_w16 };
static memfunc_pair IO = { io_mem_r8, io_mem_r16, io_mem_w8, io_mem_w16 };
static memfunc_pair EXP = { exp_mem_r8, exp_mem_r16, exp_mem_w8, exp_mem_w16 };
static memfunc_pair Z80AREA = { z80area_mem_r8, z80area_mem_r16, z80area_mem_w8, z80area_mem_w16 };

void mem_init(int romsize)
{
    romsize /= 65536;
    for (int i=0;i<romsize;++i)
        m68k_memtable[i] = ROM + i*65536;
    m68k_memtable[0xA0] = MEMFUN_PAIR(&Z80AREA);
    m68k_memtable[0xA1] = MEMFUN_PAIR(&IO);
    for (int i=0xA2;i<0xC0;i++)
        m68k_memtable[i] = MEMFUN_PAIR(&EXP);
    m68k_memtable[0xC0] = MEMFUN_PAIR(&VDP);
    m68k_memtable[0xC8] = MEMFUN_PAIR(&VDP);
    m68k_memtable[0xD0] = MEMFUN_PAIR(&VDP);
    m68k_memtable[0xD8] = MEMFUN_PAIR(&VDP);
    for (int i=0xE0;i<0x100;++i)
        m68k_memtable[i] = RAM;

    z80_memtable[0x0] = ZRAM;
    z80_memtable[0x1] = ZRAM + 0x1000;
    z80_memtable[0x2] = ZRAM;
    z80_memtable[0x3] = ZRAM + 0x1000;
}

void mem_log(const char *subs, const char *fmt, ...)
{
    extern int framecounter;
    va_list va;

    fprintf(stdout, "[%s][PC=%06x](%04d) ", subs, m68k_get_reg(NULL, M68K_REG_PC), framecounter);
    va_start(va, fmt);
    vfprintf(stdout, fmt, va);
    va_end(va);
}
