#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "vdp.h"
#include "gfx.h"
#include "mem.h"
extern "C" {
    #include "m68k/m68k.h"
}
extern int framecounter;

#define REG0_LINE_INTERRUPT   BIT(regs[0], 4)
#define REG1_DMA_ENABLED      BIT(regs[1], 4)
#define REG1_VBLANK_INTERRUPT BIT(regs[1], 5)
#define REG2_NAMETABLE_A      (BITS(regs[2], 3, 3) << 13)
#define REG3_NAMETABLE_W      (BITS(regs[3], 1, 5) << 11)
#define REG4_NAMETABLE_B      (BITS(regs[4], 0, 3) << 13)
#define REG10_LINE_COUNTER    BITS(regs[10], 0, 8)
#define REG12_MODE_H40        BIT(regs[12], 7)
#define REG15_DMA_INCREMENT   regs[15]
#define REG19_DMA_LENGTH      (regs[19] | (regs[20] << 8))
#define REG21_DMA_SRC_ADDRESS ((regs[21] | (regs[22] << 8) | ((regs[23] & 0x7F) << 16)) << 1)
#define REG23_DMA_TYPE        BITS(regs[23], 6, 2)

class VDP VDP;

// Return 9-bit accurate hcounter
int VDP::hcounter(void)
{
    int mclk = m68k_cycles_run() * M68K_FREQ_DIVISOR;
    int pixclk = mclk / 10;

    // Accurate 9-bit hcounter emulation, from timing posted here:
    // http://gendev.spritesmind.net/forum/viewtopic.php?p=17683#17683
    if (REG12_MODE_H40)
    {
        enum { SPLIT_POINT = 13+320+14+2 };
        if (pixclk < SPLIT_POINT)
            pixclk += 0xD;
        else
            pixclk = pixclk - SPLIT_POINT + 0x1C9;
    }
    else
    {
        enum { SPLIT_POINT = 13+256+14+2 };
        if (pixclk < SPLIT_POINT)
            pixclk += 0xB;
        else
            pixclk = pixclk - SPLIT_POINT + 0x1D2;
    }

    return pixclk & 0x1FF;
}

void VDP::register_w(int reg, uint8_t value)
{
    regs[reg] = value;
    fprintf(stdout, "[VDP][PC=%06x](%04d) reg:%02d <- %02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, reg, value);

    // Writing a register clear the codereg (sonic3d intro wrong colors)
    code_reg = 0;
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
        mem_log("VDP", "Direct VRAM write: addr:%x increment:%d vcounter:%d\n",
            address_reg, REG15_DMA_INCREMENT, vcounter);
        VRAM[(address_reg    ) & 0xFFFF] = value >> 8;
        VRAM[(address_reg ^ 1) & 0xFFFF] = value & 0xFF;
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0xFFFF;
        break;
    case 0x3:
        CRAM[(address_reg >> 1) & 0x3F] = value;
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0x7F;
        break;
    case 0x5:
        VSRAM[(address_reg >> 1) & 0x3F] = value;
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0x7F;
        break;

    case 0x0:
    case 0x4:
    case 0x8:
        // Write operation after setting up read: ignored (ex: ecco2, aladdin)
        break;

    default:
        fprintf(stdout, "[VDP][PC=%06x](%04d) invalid data port write16: code:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, code_reg);
        assert(!"data port w not handled");
    }
}

uint16_t VDP::data_port_r16(void)
{
    uint16_t value;

    command_word_pending = false;

    switch (code_reg & 0xF)
    {
    case 0x0:
        // Check if byteswap happens when reading or not
        assert((address_reg & 1) == 0);
        value =  VRAM[(address_reg    ) & 0xFFFF] << 8;
        value |= VRAM[(address_reg ^ 1) & 0xFFFF];
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0xFFFF;
        return value;

    case 0x8:
        value = CRAM[(address_reg >> 1) & 0x3F];
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0x7F;
        return value;

    default:
        fprintf(stdout, "[VDP][PC=%06x](%04d) invalid data port write16: code:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, code_reg);
        assert(!"data port r not handled");
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
    if (vcounter >= 224)
        status |= STATUS_VBLANK;

    // HBLANK bit (see Nemesis doc, as linked in hcounter())
    if (REG12_MODE_H40)
    {
        if (hc < 0xA && hc >= 0x166)
            status |= STATUS_HBLANK;
    }
    else
    {
        if (hc < 0x9 && hc >= 0x126)
            status |= STATUS_HBLANK;
    }

    // reading the status clears the pending flag for command words
    command_word_pending = false;

    return status;
}

uint16_t VDP::hvcounter_r16(void)
{
    int hc = hcounter() >> 1;
    int vc = vcounter;

    if (vc >= 0xEA) vc -= 0xEA - 0xE5;
    assert(vc < 256);
    assert(hc < 256);

    return (vc << 8) | hc;
}

void VDP::scanline(uint8_t* screen)
{
    bool hirq = false;

    vcounter++;
    if (vcounter == 262)
        vcounter = 0;

    // On these linese, the line counter interrupt is reloaded
    if (vcounter == 0 || (vcounter >= 225 && vcounter <= 261))
    {
        if (REG0_LINE_INTERRUPT)
            mem_log("VDP", "HINTERRUPT counter reloaded: (vcounter: %d, new counter: %d)\n", vcounter, REG10_LINE_COUNTER);
        line_counter_interrupt = REG10_LINE_COUNTER;
    }

    if (--line_counter_interrupt < 0)
    {
        if (REG0_LINE_INTERRUPT && vcounter <= 224)
        {
            mem_log("VDP", "HINTERRUPT (vcounter: %d, new counter: %d)\n", vcounter, REG10_LINE_COUNTER);
            m68k_set_irq(M68K_IRQ_4);
            hirq = true;
        }

        line_counter_interrupt = REG10_LINE_COUNTER;
    }

    if (vcounter == 224)   // vblank begin
    {
        if (REG1_VBLANK_INTERRUPT)
            m68k_set_irq(M68K_IRQ_6);
    }

    gfx_draw_scanline(screen, vcounter);
}

/**************************************************************
 * DMA
 **************************************************************/

void VDP::dma_trigger()
{
    // Check master DMA enable, otherwise skip
    if (!REG1_DMA_ENABLED)
        return;

    switch (REG23_DMA_TYPE)
    {
        case 0:
        case 1:
            dma_m68k();
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
        mem_log("VDP", "DMA VRAM fill: address:%04x, increment:%04x, length:%x, value: %04x\n",
            address_reg, REG15_DMA_INCREMENT, length, value);
        VRAM[address_reg & 0xFFFF] = value & 0xFF;
        do {
            VRAM[(address_reg ^ 1) & 0xFFFF] = value >> 8;

            address_reg += REG15_DMA_INCREMENT;
        } while (--length);
        break;
    case 0x3:
        assert(!"not implemented: DMA fill CRAM");
        break;
    case 0x5:
        assert(!"not implemented: DMA fill VSRAM");
        break;
    default:
        mem_log("VDP", "invalid code_reg:%x during DMA fill\n", code_reg);
    }
}

void VDP::dma_m68k()
{
    int length = REG19_DMA_LENGTH;
    int src_addr = REG21_DMA_SRC_ADDRESS;

    // Special case for length = 0 (ex: sonic3d)
    if (length == 0)
        length = 0xFFFF;

    switch (code_reg & 0xF)
    {
    case 0x1:
        mem_log("VDP", "DMA M68K->VRAM: src_addr:%06x dst_addr:%x length:%x increment:%d\n",
            src_addr, address_reg, length, REG15_DMA_INCREMENT);
        do {
            int value = m68k_read_memory_16(src_addr);
            src_addr += 2;
            assert(src_addr < 0x01000000);
            VRAM[(address_reg    ) & 0xFFFF] = value >> 8;
            VRAM[(address_reg ^ 1) & 0xFFFF] = value & 0xFF;
            address_reg += REG15_DMA_INCREMENT;
        } while (--length);
        break;
    case 0x3:
        mem_log("VDP", "DMA M68K->CRAM: src_addr:%06x dst_addr:%x length:%x increment:%d\n",
            src_addr, address_reg, length, REG15_DMA_INCREMENT);
        do {
            int value = m68k_read_memory_16(src_addr);
            src_addr += 2;
            assert(src_addr < 0x01000000);
            assert(address_reg < 0x80);
            CRAM[(address_reg >> 1) & 0x3F] = value;
            address_reg += REG15_DMA_INCREMENT;
        } while (--length);
        break;
    case 0x5:
        mem_log("VDP", "DMA M68K->VSRAM: src_addr:%06x dst_addr:%x length:%x increment:%d\n",
            src_addr, address_reg, length, REG15_DMA_INCREMENT);
        do {
            int value = m68k_read_memory_16(src_addr);
            src_addr += 2;
            assert(src_addr < 0x01000000);
            assert(address_reg < 0x80);
            VSRAM[(address_reg >> 1) & 0x3F] = value;
            address_reg += REG15_DMA_INCREMENT;
        } while (--length);
        break;
    default:
        mem_log("VDP", "invalid code_reg:%x during DMA fill\n", code_reg);
    }
}

int VDP::get_nametable_A() { return REG2_NAMETABLE_A; }
int VDP::get_nametable_W() { return REG3_NAMETABLE_W; }
int VDP::get_nametable_B() { return REG4_NAMETABLE_B; }

void VDP::reset()
{
    command_word_pending = false;
    address_reg = 0;
    code_reg = 0;
    vcounter = 0;
    status_reg = 0x3C00;
    line_counter_interrupt = 0;
}

void vdp_mem_w8(unsigned int address, unsigned int value)
{
    switch (address & 0x1F) {
        case 0x11:
        case 0x13:
        case 0x15:
        case 0x17:
            mem_log("SN76489", "write: %02x\n", value);
            return;

        default:
            vdp_mem_w16(address & ~1, (value << 8) | value);
            return;
            //fprintf(stdout, "[VDP][PC=%06x](%04d) unhandled write8 IO:%02x val:%04x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F, value);
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
            assert(!"unhandled vdp_mem_w16");
    }

}

unsigned int vdp_mem_r8(unsigned int address)
{
    unsigned int ret = vdp_mem_r16(address & ~1);
    if (address & 1)
        return ret & 0xFF;
    return ret >> 8;
}

unsigned int vdp_mem_r16(unsigned int address)
{
    unsigned int ret;

    switch (address & 0x1F) {
        case 0x0:
        case 0x2: return VDP.data_port_r16();

        case 0x4:
        case 0x6: return VDP.status_register_r();

        case 0x8:
        case 0xA:
        case 0xC:
        case 0xE: return VDP.hvcounter_r16();

        default:
            fprintf(stdout, "[VDP][PC=%06x](%04d) unhandled read16 IO:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F);
            assert(!"unhandled vdp_mem_r16");
            return 0xFF;
    }
}

void vdp_scanline(uint8_t *screen)
{
    VDP.scanline(screen);
}

void vdp_init(void)
{
    VDP.reset();
}

