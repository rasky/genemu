#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "vdp.h"
#include "mem.h"
extern "C" {
    #include "m68k/m68k.h"
}
extern int framecounter;

#define BIT(v, idx)       (((v) >> (idx)) & 1)
#define BITS(v, idx, n)   (((v) >> (idx)) & ((1<<(n))-1))

#define REG1_DMA_ENABLED      BIT(regs[1], 4)
#define REG15_DMA_INCREMENT   regs[15]
#define REG19_DMA_LENGTH      (regs[19] | (regs[20] << 8))
#define REG23_DMA_TYPE        BITS(regs[23], 6, 2)

class VDP
{
private:
    uint8_t VRAM[0x10000];
    uint16_t CRAM[0x40];
    uint16_t VSRAM[0x40];  // only 40 words are really used
    uint8_t regs[0x20];
    uint16_t address_reg;
    uint8_t code_reg;
    uint16_t status_reg;
    int vcounter;
    bool command_word_pending;
    bool dma_fill_pending;

private:
    void register_w(int reg, uint8_t value);
    int hcounter();
    void dma_trigger();
    void dma_fill(uint16_t value);

public:
    void scanline();
    void reset();
    uint16_t status_register_r();
    void control_port_w(uint16_t value);
    void data_port_w16(uint16_t value);

} VDP;

int VDP::hcounter(void)
{
    int cycles = m68k_cycles_run() * M68K_FREQ_DIVISOR;

    // FIXME: this must be fixed to take into account
    // the real resolution of the screen
    return cycles * 256 / VDP_CYCLES_PER_LINE;
}

void VDP::register_w(int reg, uint8_t value)
{
    regs[reg] = value;
    fprintf(stdout, "[VDP][PC=%06x](%04d) reg:%02d <- %02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, reg, value);
}

void VDP::data_port_w16(uint16_t value)
{
    command_word_pending = false;

    // When a DMA fill is pending, the value sent to the
    // data port is the actual fill value and triggers the fill
    if (dma_fill_pending)
    {
        dma_fill_pending = false;
        dma_fill(value);
        return;
    }

    switch (code_reg & 0xF)
    {
    case 0x1:
        VRAM[(address_reg    ) & 0xFFFF] = value >> 8;
        VRAM[(address_reg ^ 1) & 0xFFFF] = value & 0xFF;
        address_reg += 2;
        address_reg &= 0xFFFF;
        break;
    case 0x3:
        CRAM[address_reg & 0x3E] = value;
        address_reg += 2;
        address_reg &= 0x7F;
        break;
    case 0x5:
        VSRAM[address_reg & 0x3E] = value;
        address_reg += 2;
        address_reg &= 0x7F;
        break;

    default:
        fprintf(stdout, "[VDP][PC=%06x](%04d) invalid data port write16: code:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, code_reg);
    }
}


void VDP::control_port_w(uint16_t value)
{
    if (command_word_pending) {
        // second half of the command word
        code_reg &= ~0x3C;
        code_reg |= (value >> 2) & 0x3C;
        address_reg &= 0x3FFF;
        address_reg |= value << 14;
        command_word_pending = false;
        fprintf(stdout, "[VDP][PC=%06x](%04d) command word 2nd: code:%02x addr:%04x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, code_reg, address_reg);
        if (code_reg & (1<<5))
            dma_trigger();
        return;
    }

    if ((value >> 14) == 2)
    {
        register_w((value >> 8) & 0x1F, value & 0xFF);
        return;
    }

    // Anything else is treated as first half of the "command word"
    // We directly update the code reg and address reg
    code_reg &= ~0x3;
    code_reg |= value >> 14;
    address_reg &= ~0x3FFF;
    address_reg |= value & 0x3FFF;
    command_word_pending = true;
    fprintf(stdout, "[VDP][PC=%06x](%04d) command word 1st: code:%02x addr:%04x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, code_reg, address_reg);
}

uint16_t VDP::status_register_r(void)
{
    #define STATUS_FIFO_EMPTY      (1<<9)
    #define STATUS_FIFO_FULL       (1<<8)
    #define STATUS_VIRQPENDING     (1<<7)
    #define STATUS_SPRITEOVERFLOW  (1<<6)
    #define STATUS_SPRITECOLLISION (1<<5)
    #define STATUS_ODDFRAME        (1<<4)
    #define STATUS_VBLANK          (1<<3)
    #define STATUS_HBLANK          (1<<2)
    #define STATUS_DMAPROGRESS     (1<<1)
    #define STATUS_PAL             (1<<1)

    uint16_t status = status_reg;
    int hc = hcounter();

    // TODO: FIFO not emulated
    status |= STATUS_FIFO_EMPTY;

    // VBLANK bit
    if (vcounter == 224 && hc >= 0xAA)
        status |= STATUS_VBLANK;
    else if (vcounter > 224 && vcounter < 261)
        status |= STATUS_VBLANK;
    else if (vcounter == 261 && hc < 0xAA)
        status |= STATUS_VBLANK;

    // HBLANK bit
    if (hc < 8 && hc >= 228)
        status |= STATUS_HBLANK;

    // reading the status clears the pending flag for command words
    command_word_pending = false;

    return status;
}

void VDP::scanline()
{
    vcounter++;
    if (vcounter == 262)
        vcounter = 0;

    if (vcounter == 224)   // vblank begin
    {
        if (regs[0x1] & (1<<5))
            m68k_set_irq(M68K_IRQ_6);
    }
}

void VDP::dma_trigger()
{
    // Check master DMA enable, otherwise skip
    if (!REG1_DMA_ENABLED)
        return;

    switch (REG23_DMA_TYPE)
    {
        case 0:
        case 1:
            assert(!"not implemented: DMA 68k->VDP");
            break;

        case 2:
            // VRAM fill will trigger on next data port write
            dma_fill_pending = true;
            break;

        case 3:
            assert(!"not implemented: VRAM copy");
            break;
    }
}

void VDP::dma_fill(uint16_t value)
{
    /* FIXME: should be done in parallel and non blocking */
    int length = REG19_DMA_LENGTH;

    if (length == 0)
        length = 0xFFFF;

    switch (code_reg & 0xF)
    {
    case 0x1:
        mem_log("VDP", "DMA VRAM fill: address:%04x, increment:%04x, length:%d, value: %02x\n",
            address_reg, REG15_DMA_INCREMENT, length, value);
        VRAM[address_reg & 0xFFFF] = value;
        do {
            VRAM[(address_reg ^ 1) & 0xFFFF] = value >> 8;

            address_reg += REG15_DMA_INCREMENT;
        } while (--length);
        break;
    case 0x3:
        assert(!"not implemented: DMA fille CRAM");
        break;
    case 0x5:
        assert(!"not implemented: DMA fille VSRAM");
        break;
    default:
        mem_log("VDP", "invalid code_reg:%x during DMA fill\n", code_reg);
    }
}

void VDP::reset()
{
    command_word_pending = false;
    address_reg = 0;
    code_reg = 0;
    vcounter = 0;
    status_reg = 0x3C00;
}

void vdp_mem_w8(unsigned int address, unsigned int value)
{
    switch (address & 0x1F) {
        default:
            fprintf(stdout, "[VDP][PC=%06x](%04d) unhandled write8 IO:%02x val:%04x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F, value);
    }
}

void vdp_mem_w16(unsigned int address, unsigned int value)
{
    switch (address & 0x1F) {
        case 0x0:
        case 0x2: VDP.data_port_w16(value); return;

        case 0x4:
        case 0x6: VDP.control_port_w(value); return;

        default:
            fprintf(stdout, "[VDP][PC=%06x](%04d) unhandled write16 IO:%02x val:%04x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F, value);
    }

}

unsigned int vdp_mem_r8(unsigned int address)
{
    fprintf(stdout, "[VDP][PC=%06x](%04d) unhandled read8 IO:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F);
    return 0xFF;
}

unsigned int vdp_mem_r16(unsigned int address)
{
    unsigned int ret;

    switch (address & 0x1F) {
        case 0x4:
        case 0x6:
            return VDP.status_register_r();
        default:
            fprintf(stdout, "[VDP][PC=%06x](%04d) unhandled read16 IO:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F);
            return 0xFF;
    }
}

void vdp_scanline(void)
{
    VDP.scanline();
}

void vdp_init(void)
{
    VDP.reset();
}

