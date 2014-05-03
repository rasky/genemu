
#include <stdint.h>

#define VDP_MASTER_FREQ       53693175     // NTSC
//#define VDP_MASTER_FREQ       53203424   // PAL
#define VDP_CYCLES_PER_LINE   3420

#define M68K_FREQ_DIVISOR     7
#define Z80_FREQ_DIVISOR      14

class VDP
{
    friend class GFX;
    friend void loadstate(const char *fn);
    friend void savestate(const char *fn);

private:
    uint8_t VRAM[0x10000];
    uint16_t CRAM[0x40];
    uint16_t VSRAM[0x40];  // only 40 words are really used
    uint8_t SAT_CACHE[0x400]; // internal copy of SAT
    uint8_t regs[0x20];
    uint16_t fifo[4];
    uint64_t fifoclock[4];
    uint16_t address_reg;
    uint8_t code_reg;
    uint16_t status_reg;
    int _vcounter;
    int line_counter_interrupt;
    bool command_word_pending;
    bool dma_fill_pending;
    bool hvcounter_latched;
    uint16_t hvcounter_latch;
    int sprite_overflow;
    bool sprite_collision;
    int mode_h40;
    int mode_v40;
    bool dma_m68k_running;
    bool display_disabled_hblank;
    int access_slot_freq;
    uint64_t base_access_slot;
    uint64_t base_access_slot_time;

    uint64_t access_slot_at(uint64_t when);
    uint64_t access_slot_time(uint64_t numslot);
    void update_access_slot_freq(void);

    void VRAM_W(uint16_t address, uint8_t value);

private:
    void register_w(int reg, uint8_t value);
    int hcounter();   // 9-bit accurate horizontal
    int vcounter();   // 9-bit accurate vcounter
    bool hblank();
    bool vblank();
    void dma_start();
    void dma_poll();
    void dma_fill();
    void dma_copy();
    void dma_m68k();

    bool fifo_empty();
    bool fifo_full();
    void fifo_wait_empty();
    void fifo_wait_not_full();
    void push_fifo(uint16_t value, int numbytes);

    int get_nametable_A();
    int get_nametable_B();
    int get_nametable_W();

public:
    void reset();
    void frame_begin();
    void scanline_begin(uint8_t *screen);
    void scanline_hblank(uint8_t *screen);
    void scanline_end(uint8_t *screen);
    void frame_end();
    bool is_dma_m68k_running() { return dma_m68k_running; }

    unsigned int scanline_hblank_clocks();
    unsigned int num_scanlines();

public:
    uint16_t status_register_r();
    void control_port_w(uint16_t value);
    void data_port_w16(uint16_t value);
    uint16_t data_port_r16(void);
    uint16_t hvcounter_r16(void);
};

extern class VDP VDP;

void vdp_mem_w8(unsigned int address, unsigned int value);
void vdp_mem_w16(unsigned int address, unsigned int value);

unsigned int vdp_mem_r8(unsigned int address);
unsigned int vdp_mem_r16(unsigned int address);
