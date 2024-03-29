#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "vdp.h"
#include "gfx.h"
#include "mem.h"
#include "cpu.h"
extern "C" {
    #include "m68k/m68k.h"
}
extern int framecounter;
extern int VERSION_PAL;

#define REG0_HVLATCH          BIT(regs[0], 1)
#define REG0_LINE_INTERRUPT   BIT(regs[0], 4)
#define REG1_PAL              BIT(regs[1], 3)
#define REG1_DMA_ENABLED      BIT(regs[1], 4)
#define REG1_VBLANK_INTERRUPT BIT(regs[1], 5)
#define REG1_DISP_ENABLED     BIT(regs[1], 6)
#define REG2_NAMETABLE_A      (BITS(regs[2], 3, 3) << 13)
#define REG3_NAMETABLE_W      (BITS(regs[3], 1, 5) << 11)
#define REG4_NAMETABLE_B      (BITS(regs[4], 0, 3) << 13)
#define REG5_SAT_ADDRESS      ((regs[5] & (mode_h40 ? 0x7E : 0x7F)) << 9)
#define REG5_SAT_SIZE         (mode_h40 ? (1<<10) : (1<<9))
#define REG10_LINE_COUNTER    BITS(regs[10], 0, 8)
#define REG12_MODE_H40        BIT(regs[12], 0)
#define REG15_DMA_INCREMENT   regs[15]
#define REG19_DMA_LENGTH      (regs[19] | (regs[20] << 8))
#define REG21_DMA_SRCADDR_LOW (regs[21] | (regs[22] << 8))
#define REG23_DMA_SRCADDR_HIGH ((regs[23] & 0x7F) << 16)
#define REG23_DMA_TYPE        BITS(regs[23], 6, 2)

class VDP VDP;

// Return 9-bit accurate hcounter
int VDP::hcounter(void)
{
    int mclk = m68k_cycles_run() * M68K_FREQ_DIVISOR;
    int pixclk;

    // Accurate 9-bit hcounter emulation, from timing posted here:
    // http://gendev.spritesmind.net/forum/viewtopic.php?p=17683#17683
    if (REG12_MODE_H40)
    {
        pixclk = mclk * 420 / VDP_CYCLES_PER_LINE;
        pixclk += 0xD;
        if (pixclk >= 0x16D)
            pixclk += 0x1C9 - 0x16D;
    }
    else
    {
        pixclk = mclk * 342 / VDP_CYCLES_PER_LINE;
        pixclk += 0xB;
        if (pixclk >= 0x128)
            pixclk += 0x1D2 - 0x128;
    }

    return pixclk & 0x1FF;
}

int VDP::vcounter(void)
{
    int vc = _vcounter;

    if (VERSION_PAL && mode_pal && vc >= 0x10B)
        vc += 0x1D2 - 0x10B;
    else if (VERSION_PAL && !mode_pal && vc >= 0x103)
        vc += 0x1CA - 0x103;
    else if (!VERSION_PAL && vc >= 0xEB)
        vc += 0x1E5 - 0xEB;
    assert(vc < 0x200);
    return vc;
}

bool VDP::vblank(void)
{
    int vc = vcounter();

    if (!REG1_DISP_ENABLED)
        return true;

    if (mode_pal)
        return (vc >= 0xF0 && vc < 0x1FF);
    else
        return (vc >= 0xE0 && vc < 0x1FF);
}

void VDP::register_w(int reg, uint8_t value)
{
    // Mode4 is not emulated yet. Anyway, access to registers > 0xA is blocked.
    if (!BIT(regs[0x1], 2) && reg > 0xA) return;

    regs[reg] = value;
    mem_log("VDP", "reg:%02d <- %02x\n", reg, value);

    // Writing a register clear the first command word
    // (see sonic3d intro wrong colors, and vdpfifotesting)
    code_reg &= ~0x3;
    address_reg &= ~0x3FFF;

    // Here we handle only cases where the register write
    // has an immediate effect on VDP.
    switch (reg)
    {
        case 0:
            if (REG0_HVLATCH && !hvcounter_latched)
            {
                hvcounter_latch = hvcounter_r16();
                hvcounter_latched = true;
            }
            else if (!REG0_HVLATCH && hvcounter_latched)
                hvcounter_latched = false;
            break;
    }

}


void VDP::push_fifo(uint16_t value)
{
    fifo[3] = fifo[2];
    fifo[2] = fifo[1];
    fifo[1] = fifo[0];
    fifo[0] = value;
}

void VDP::VRAM_W(uint16_t address, uint8_t value)
{
    VRAM[address] = value;

    // Update internal SAT cache if it was modified
    // This cache is needed for Castlevania Bloodlines (level 6-2)
    if (address >= REG5_SAT_ADDRESS && address_reg < REG5_SAT_ADDRESS + REG5_SAT_SIZE)
        SAT_CACHE[address - REG5_SAT_ADDRESS] = value;
}


void VDP::data_port_w16(uint16_t value)
{
    command_word_pending = false;

    push_fifo(value);

    switch (code_reg & 0xF)
    {
    case 0x1:
        mem_log("VDP", "Direct VRAM write: addr:%x increment:%d value:%04x A0=%x vc:%x hc:%x\n",
                address_reg, REG15_DMA_INCREMENT, value, m68k_get_reg(NULL, M68K_REG_A0), vcounter(), hcounter());
        VRAM_W((address_reg    ) & 0xFFFF, value >> 8);
        VRAM_W((address_reg ^ 1) & 0xFFFF, value & 0xFF);
        address_reg += REG15_DMA_INCREMENT;
        break;
    case 0x3:
        mem_log("VDP", "Direct CRAM write: addr:%x increment:%d value:%04x vc:%x hc:%x\n",
                address_reg, REG15_DMA_INCREMENT, value, vcounter(), hcounter());
        CRAM[(address_reg >> 1) & 0x3F] = value;
        address_reg += REG15_DMA_INCREMENT;
        break;
    case 0x5:
        mem_log("VDP", "Direct VSRAM write: addr:%x increment:%d value:%04x vc:%x hc:%x\n",
                address_reg, REG15_DMA_INCREMENT, value, vcounter(), hcounter());
        VSRAM[(address_reg >> 1) & 0x3F] = value;
        address_reg += REG15_DMA_INCREMENT;
        break;

    case 0x0:
    case 0x4:
    case 0x8:
        // Write operation after setting up read: ignored (ex: ecco2, aladdin)
        break;

    case 0x9:
        // invalid, ignore (vdpfifotesting)
        break;

    default:
        mem_err("VDP", "invalid data port write16: code:%02x\n", code_reg);
        assert(!"data port w not handled");
    }

    // When a DMA fill is pending, the value sent to the
    // data port is the actual fill value and triggers the fill
    if (dma_fill_pending)
    {
        dma_fill_pending = false;
        dma_fill(value);
        return;
    }
}

uint16_t VDP::data_port_r16(void)
{
    enum { CRAM_BITMASK = 0x0EEE, VSRAM_BITMASK = 0x07FF, VRAM8_BITMASK = 0x00FF };
    uint16_t value;

    command_word_pending = false;
    mem_log("VDP", "data port r16: code:%x, addr:%x\n", code_reg, address_reg);

    switch (code_reg & 0xF)
    {
    case 0x0:
        // No byteswapping here, see vdpfifotesting
        value =  VRAM[(address_reg    ) & 0xFFFE] << 8;
        value |= VRAM[(address_reg | 1) & 0xFFFF];
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0xFFFF;
        return value;

    case 0x4:
        if (((address_reg >> 1) & 0x3F) >= 0x28)
            value = VSRAM[0];
        else
            value = VSRAM[(address_reg >> 1) & 0x3F];
        value = (value & VSRAM_BITMASK) | (fifo[3] & ~VSRAM_BITMASK);
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0x7F;
        return value;

    case 0x8:
        value = CRAM[(address_reg >> 1) & 0x3F];
        value = (value & CRAM_BITMASK) | (fifo[3] & ~CRAM_BITMASK);
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0x7F;
        return value;

    case 0xC: // undocumented 8-bit VRAM access, see vdpfifotesting
        value = VRAM[(address_reg ^ 1) & 0xFFFF];
        value = (value & VRAM8_BITMASK) | (fifo[3] & ~VRAM8_BITMASK);
        address_reg += REG15_DMA_INCREMENT;
        address_reg &= 0xFFFF;
        return value;

    default:
        fprintf(stderr, "[VDP][PC=%06x](%04d) invalid data port write16: code:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, code_reg);
        assert(!"data port r not handled");
        return 0xFF;
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
    #define STATUS_PAL             (1<<0)

    uint16_t status = status_reg;
    int hc = hcounter();
    int vc = vcounter();

    // TODO: FIFO not emulated
    status |= STATUS_FIFO_EMPTY;

    // VBLANK bit
    if (vblank())
        status |= STATUS_VBLANK;

    // HBLANK bit (see Nemesis doc, as linked in hcounter())
    if (REG12_MODE_H40)
    {
        if (hc < 0xA || hc >= 0x166)
            status |= STATUS_HBLANK;
    }
    else
    {
        if (hc < 0x9 || hc >= 0x126)
            status |= STATUS_HBLANK;
    }

    if (sprite_overflow)
        status |= STATUS_SPRITEOVERFLOW;
    if (sprite_collision)
        status |= STATUS_SPRITECOLLISION;

    if (mode_pal)
        status |= STATUS_PAL;

    // reading the status clears the pending flag for command words
    command_word_pending = false;

    return status;
}

uint16_t VDP::hvcounter_r16(void)
{
    if (hvcounter_latched)
        return hvcounter_latch;

    int hc = hcounter();
    int vc = vcounter();
    assert(vc < 512);
    assert(hc < 512);

    return ((vc & 0xFF) << 8) | (hc >> 1);
}

void VDP::scanline(uint8_t* screen)
{
    mode_h40 = REG12_MODE_H40;
    mode_pal = REG1_PAL;

    mem_log("VDP", "render scanline %d\n", _vcounter);
    gfx_render_scanline(screen, _vcounter);

    // On these lines, the line counter interrupt is reloaded
    if (_vcounter == 0 || _vcounter >= (mode_pal ? 0xF1 : 0xE1))
    {
        if (REG0_LINE_INTERRUPT)
            mem_log("VDP", "HINTERRUPT counter reloaded: (vcounter: %d, new counter: %d)\n", _vcounter, REG10_LINE_COUNTER);
        line_counter_interrupt = REG10_LINE_COUNTER;
    }

    if (--line_counter_interrupt < 0)
    {
        if (REG0_LINE_INTERRUPT && (_vcounter <= (mode_pal ? 0xF0 : 0xE0)))
        {
            mem_log("VDP", "HINTERRUPT (_vcounter: %d, new counter: %d)\n", _vcounter, REG10_LINE_COUNTER);
            hint_pending = true;
            if (!(status_reg & STATUS_VIRQPENDING))
                CPU_M68K.irq(4);
        }

        line_counter_interrupt = REG10_LINE_COUNTER;
    }

    _vcounter++;
    if (_vcounter == (VERSION_PAL ? 313 : 262))
    {
        _vcounter = 0;
        sprite_overflow = 0;
        sprite_collision = false;
    }

    if (_vcounter == (mode_pal ? 0xF0 : 0xE0))   // vblank begin
    {
        if (REG1_VBLANK_INTERRUPT)
        {
            status_reg |= STATUS_VIRQPENDING;
            CPU_M68K.irq(6);
        }
        CPU_Z80.set_irq_line(true);
    }
    if (_vcounter == (mode_pal ? 0xF1 : 0xE1))
    {
        // The Z80 IRQ line stays asserted for one line
        CPU_Z80.set_irq_line(false);
    }
}

unsigned int VDP::num_scanlines(void)
{
    return VERSION_PAL ? 313 : 262;
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
            dma_copy();
            break;
    }
}

void VDP::dma_fill(uint16_t value)
{
    /* FIXME: should be done in parallel and non blocking */
    int length = REG19_DMA_LENGTH;

    // This address is not required for fills,
    // but it's still updated by the DMA engine.
    uint16_t src_addr_low = REG21_DMA_SRCADDR_LOW;

    if (length == 0)
        length = 0xFFFF;

    mem_log("VDP", "(V=%x,H=%x) DMA %s fill: dst:%04x, length:%d, increment:%d, value=%02x\n",
        vcounter(), hcounter(),
        (code_reg&0xF)==1 ? "VRAM" : ( (code_reg&0xF)==3 ? "CRAM" : "VSRAM"),
        address_reg, length, REG15_DMA_INCREMENT, value>>8);

    switch (code_reg & 0xF)
    {
    case 0x1:
        do {
            VRAM_W((address_reg ^ 1) & 0xFFFF, value >> 8);
            address_reg += REG15_DMA_INCREMENT;
            src_addr_low++;
        } while (--length);
        break;
    case 0x3:  // undocumented and buggy, see vdpfifotesting
        do {
            CRAM[(address_reg >> 1) & 0x3F] = fifo[3];
            address_reg += REG15_DMA_INCREMENT;
            src_addr_low++;
        } while (--length);
        break;
    case 0x5:  // undocumented and buggy, see vdpfifotesting:
        do {
            VSRAM[(address_reg >> 1) & 0x3F] = fifo[3];
            address_reg += REG15_DMA_INCREMENT;
            src_addr_low++;
        } while (--length);
        break;
    default:
        mem_log("VDP", "invalid code_reg:%x during DMA fill\n", code_reg);
    }

    // Clear DMA length at the end of transfer
    regs[19] = regs[20] = 0;

    // Update DMA source address after end of transfer
    regs[21] = src_addr_low & 0xFF;
    regs[22] = src_addr_low >> 8;
}

void VDP::dma_m68k()
{
    int length = REG19_DMA_LENGTH;
    uint16_t src_addr_low = REG21_DMA_SRCADDR_LOW;
    uint32_t src_addr_high = REG23_DMA_SRCADDR_HIGH;

    // Special case for length = 0 (ex: sonic3d)
    if (length == 0)
        length = 0xFFFF;

    mem_log("VDP", "(V=%x,H=%x) DMA M68k->%s copy: src:%04x, dst:%04x, length:%d, increment:%d\n",
        vcounter(), hcounter(),
        (code_reg&0xF)==1 ? "VRAM" : ( (code_reg&0xF)==3 ? "CRAM" : "VSRAM"),
        (src_addr_high | src_addr_low) << 1, address_reg, length, REG15_DMA_INCREMENT);

    do {
        unsigned int value = m68k_read_memory_16((src_addr_high | src_addr_low) << 1);
        push_fifo(value);

        switch (code_reg & 0xF) {
            case 0x1:
                VRAM_W((address_reg    ) & 0xFFFF, value >> 8);
                VRAM_W((address_reg ^ 1) & 0xFFFF, value & 0xFF);
                break;
            case 0x3:
                CRAM[(address_reg >> 1) & 0x3F] = value;
                break;
            case 0x5:
                VSRAM[(address_reg >> 1) & 0x3F] = value;
                break;
            default:
                mem_log("VDP", "invalid code_reg:%x during DMA fill\n", code_reg);
                break;
        }

        address_reg += REG15_DMA_INCREMENT;
        src_addr_low += 1;

    } while (--length);

    // Update DMA source address after end of transfer
    regs[21] = src_addr_low & 0xFF;
    regs[22] = src_addr_low >> 8;

    // Clear DMA length at the end of transfer
    regs[19] = regs[20] = 0;
}

void VDP::dma_copy()
{
    int length = REG19_DMA_LENGTH;
    uint16_t src_addr_low = REG21_DMA_SRCADDR_LOW;

    assert(length != 0);
    mem_log("VDP", "DMA copy: src:%04x dst:%04x len:%x\n", src_addr_low, address_reg&0xFFFF, length);

    do {
        uint16_t value = VRAM[src_addr_low ^ 1];
        VRAM_W((address_reg ^ 1) & 0xFFFF, value);

        address_reg += REG15_DMA_INCREMENT;
        src_addr_low++;
    } while (--length);

    // Update DMA source address after end of transfer
    regs[21] = src_addr_low & 0xFF;
    regs[22] = src_addr_low >> 8;

    // Clear DMA length at the end of transfer
    regs[19] = regs[20] = 0;

}

int VDP::get_nametable_A() { return REG2_NAMETABLE_A; }
int VDP::get_nametable_W() { return REG3_NAMETABLE_W; }
int VDP::get_nametable_B() { return REG4_NAMETABLE_B; }

int m68k_int_ack(int irq)
{
    return VDP.irq_acked(irq);
}

void VDP::reset()
{
    command_word_pending = false;
    address_reg = 0;
    code_reg = 0;
    _vcounter = 0;
    status_reg = 0x3C00;
    line_counter_interrupt = 0;
    hvcounter_latched = false;
    m68k_set_int_ack_callback(m68k_int_ack);
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

        // unused register, see vdpfifotesting
        case 0x18:
            return;
        // debug register
        case 0x1C:
            return;

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

        // unused register, see vdpfifotesting
        case 0x18: return 0xFFFF;

        // debug register
        case 0x1C: return 0xFFFF;

        default:
            fprintf(stdout, "[VDP][PC=%06x](%04d) unhandled read16 IO:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F);
            assert(!"unhandled vdp_mem_r16");
            return 0xFF;
    }
}

int VDP::irq_acked(int irq)
{
    if (irq == 6)
    {
        status_reg &= ~STATUS_VIRQPENDING;
        return hint_pending ? 4 : 0;
    }
    else if (irq == 4)
    {
        hint_pending = false;
        return 0;
    }
    assert(0);
}

void vdp_scanline(uint8_t *screen)
{
    VDP.scanline(screen);
}

void vdp_init(void)
{
    VDP.reset();
}

