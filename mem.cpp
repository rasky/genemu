#include "mem.h"
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
extern "C" {
    #include "m68k/m68k.h"
}
#include "vdp.h"


uint8_t *ROM;
uint8_t RAM[0x10000];

inline uint16_t SWAP16(uint16_t a) {
    return (a >> 8) | (a << 8);
}
void *memtable[256];
typedef unsigned int (*memfunc_r)(unsigned int address);
typedef void         (*memfunc_w)(unsigned int address, unsigned int value);

struct memfunc_pair {
    memfunc_r read8, read16;
    memfunc_w write8, write16;
};

static memfunc_pair VDP = { vdp_mem_r8, vdp_mem_r16, vdp_mem_w8, vdp_mem_w16 };

#define MEMFUN_PAIR(x)         ((void*)((unsigned long)(x) | 1))
#define GET_MEMFUNC_PAIR(x)    ((memfunc_pair*)((unsigned long)(x) & ~1))

void mem_init(int romsize)
{
    romsize /= 65536;
    for (int i=0;i<romsize;++i)
        memtable[i] = ROM + i*65536;
    memtable[0xC0] = MEMFUN_PAIR(&VDP);
    memtable[0xC8] = MEMFUN_PAIR(&VDP);
    memtable[0xD0] = MEMFUN_PAIR(&VDP);
    memtable[0xD8] = MEMFUN_PAIR(&VDP);
    for (int i=0xE0;i<0x100;++i)
        memtable[i] = RAM;
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

template<class TYPE>
unsigned int m68k_read_memory(unsigned int address)
{
    assert((address >> 16) < 256);
    void *t = memtable[address >> 16];
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
    void *t = memtable[address >> 16];
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

int load_rom(char *fn)
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
