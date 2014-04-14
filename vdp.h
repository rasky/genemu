
#include <stdint.h>

#define VDP_MASTER_FREQ       53693175     // NTSC
//#define VDP_MASTER_FREQ       53203424   // PAL
#define VDP_CYCLES_PER_LINE   3420
#define VDP_HZ                60
#define VDP_SCANLINES         262

#define M68K_FREQ_DIVISOR     7

class VDP
{
    friend class GFX;

private:
    uint8_t VRAM[0x10000];
    uint16_t CRAM[0x40];
    uint16_t VSRAM[0x40];  // only 40 words are really used
    uint8_t regs[0x20];
    uint16_t address_reg;
    uint8_t code_reg;
    uint16_t status_reg;
    int vcounter;
    int line_counter_interrupt;
    bool command_word_pending;
    bool dma_fill_pending;

private:
    void register_w(int reg, uint8_t value);
    int hcounter();
    void dma_trigger();
    void dma_fill(uint16_t value);
    void dma_m68k();

    int get_nametable_A();
    int get_nametable_B();
    int get_nametable_W();

public:
    void scanline(uint8_t *screen);
    void reset();
    uint16_t status_register_r();
    void control_port_w(uint16_t value);
    void data_port_w16(uint16_t value);
};

extern class VDP VDP;

void vdp_init(void);
void vdp_scanline(uint8_t *screen);

void vdp_mem_w8(unsigned int address, unsigned int value);
void vdp_mem_w16(unsigned int address, unsigned int value);

unsigned int vdp_mem_r8(unsigned int address);
unsigned int vdp_mem_r16(unsigned int address);
