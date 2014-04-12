
#define VDP_MASTER_FREQ       53693175     // NTSC
//#define VDP_MASTER_FREQ       53203424   // PAL
#define VDP_CYCLES_PER_LINE   3420
#define VDP_HZ                60
#define VDP_SCANLINES         262

#define M68K_FREQ_DIVISOR     7



void vdp_init(void);
void vdp_scanline(void);

void vdp_mem_w8(unsigned int address, unsigned int value);
void vdp_mem_w16(unsigned int address, unsigned int value);

unsigned int vdp_mem_r8(unsigned int address);
unsigned int vdp_mem_r16(unsigned int address);
