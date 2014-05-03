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

extern int VERSION_PAL;
extern int framecounter;

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

class VDP VDP;

// Return 9-bit accurate hcounter
int VDP::hcounter(void)
{
    int mclk = CPU_M68K.clock() % VDP_CYCLES_PER_LINE;
    int pixclk;

    // Accurate 9-bit hcounter emulation, from timing posted here:
    // http://gendev.spritesmind.net/forum/viewtopic.php?p=17683#17683
    if (mode_h40)
    {
        pixclk = mclk * 420 / VDP_CYCLES_PER_LINE;
        enum { SPLIT_POINT = 13+320+14+2 };
        pixclk += 0xD;
        if (pixclk >= SPLIT_POINT)
            pixclk = pixclk - SPLIT_POINT + 0x1C9;
    }
    else
    {
        pixclk = mclk * 342 / VDP_CYCLES_PER_LINE;
        enum { SPLIT_POINT = 13+256+14+2 };
        pixclk += 0xB;
        if (pixclk >= SPLIT_POINT)
            pixclk = pixclk - SPLIT_POINT + 0x1D2;
    }

    return pixclk & 0x1FF;
}

int VDP::vcounter(void)
{
    int vc = _vcounter;

    if (VERSION_PAL && mode_v40 && vc >= 0x10B)
        vc += 0x1D2 - 0x10B;
    else if (VERSION_PAL && !mode_v40 && vc >= 0x103)
        vc += 0x1CA - 0x103;
    else if (!VERSION_PAL && vc >= 0xEB)
        vc += 0x1E5 - 0xEB;
    assert(vc < 0x200);
    return vc;
}

bool VDP::hblank(void)
{
    int hc = hcounter();

    if (mode_h40)
        return (hc < 0xB || hc >= 0x166);
    else
        return (hc < 0xA || hc >= 0x126);
}

bool VDP::vblank(void)
{
    int vc = vcounter();

    if (!REG1_DISP_ENABLED)
        return true;

    if (mode_v40)
        return (vc >= 0xF0 && vc < 0x1FF);
    else
        return (vc >= 0xE0 && vc < 0x1FF);
}

void VDP::register_w(int reg, uint8_t value)
{
    // Mode4 is not emulated yet. Anyway, access to registers > 0xA is blocked.
    if (!BIT(regs[0x1], 2) && reg > 0xA) return;

    uint8_t oldvalue = regs[reg];
    regs[reg] = value;
    fprintf(stdout, "[VDP][PC=%06x](%04d) reg:%02d <- %02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, reg, value);

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

        case 1:
            if (BIT((oldvalue ^ value), 6))
            {
                // Change in display enable: update access slot frequency
                update_access_slot_freq();

                if (!REG1_DISP_ENABLED && hblank())
                    display_disabled_hblank = true;
            }
            break;
    }

}

static const int fifo_delay[2][2] =
{
    // These timings are correct for CRAM and VSRAM
    /* MODE H32 */
    { 16, 161 },
    /* MODE H40 */
    { 18, 198 },

    // For VRAM, timing should be something like:
    // { 16, ??? },
    // { 18, 205 },
};

bool VDP::fifo_empty()
{
    uint64_t now = CPU_M68K.clock();
    return now >= access_slot_time(fifoclock[0]);
}

bool VDP::fifo_full()
{
    uint64_t now = CPU_M68K.clock();
    return now < access_slot_time(fifoclock[3]);
}

void VDP::fifo_wait_empty()
{
    if (!fifo_empty())
        CPU_M68K.burn(access_slot_time(fifoclock[0]));
    assert(fifo_empty());
    assert(!fifo_full());
}

void VDP::fifo_wait_not_full()
{
    if (fifo_full())
        CPU_M68K.burn(access_slot_time(fifoclock[3]));
    assert(!fifo_full());
}

#define MAX(a,b)  ((a)>(b)?(a):(b))
#define PIXELRATE_TO_CLOCKS_F8(n)  ((VDP_CYCLES_PER_LINE << 8) / (n))

void VDP::update_access_slot_freq()
{
    uint64_t cur_slot = access_slot_at(CPU_M68K.clock());

    // mem_log("VDP", "before update asfreq: cur_slot:%lld, clock:%lld, base_access_slot_time:%lld, base_access_slot:%lld\n",
    //     cur_slot, CPU_M68K.clock(), base_access_slot_time, base_access_slot);

    base_access_slot_time = access_slot_time(cur_slot);
    base_access_slot = cur_slot;
    access_slot_freq = PIXELRATE_TO_CLOCKS_F8(fifo_delay[mode_h40][vblank()]);

    // mem_log("VDP", "after update asfreq: cur_slot:%lld, clock:%lld, base_access_slot_time:%lld, base_access_slot:%lld\n",
    //     access_slot_at(CPU_M68K.clock()), CPU_M68K.clock(), base_access_slot_time, base_access_slot);

    assert(access_slot_at(CPU_M68K.clock()) >= cur_slot);

    // assert(curslot == access_slot_at(CPU_M68K.clock()));
    mem_log("VDP", "Update AS freq: num:%lld, basetime:%lld, freq:%d(%f), delay:%d\n",
        base_access_slot, base_access_slot_time,
        access_slot_freq, float(access_slot_freq) / 256.0,
        fifo_delay[mode_h40][vblank()]);
}

uint64_t VDP::access_slot_time(uint64_t numslot)
{
    int64_t s = numslot - base_access_slot;   // NOTE: might become negative
    return ((s * access_slot_freq) >> 8) + base_access_slot_time;
}

uint64_t VDP::access_slot_at(uint64_t when)
{
    when -= base_access_slot_time;
    return ((when << 8) + 255) / access_slot_freq + base_access_slot;
}

void VDP::push_fifo(uint16_t value, int numbytes)
{
    assert(numbytes > 0);

    fifo[3] = fifo[2];
    fifo[2] = fifo[1];
    fifo[1] = fifo[0];
    fifo[0] = value;

    uint64_t cur_slot = access_slot_at(CPU_M68K.clock());
    // printf("Checking fifo: now:%lld(%lld), f3:%lld(%lld), as_freq:%f\n",
    //     CPU_M68K.clock(), cur_slot,
    //     access_slot_time(fifoclock[3]), fifoclock[3],
    //     float(access_slot_freq) / 256.0);
    // printf("start push: clock:%lld cur_slot:%lld\n", CPU_M68K.clock(), cur_slot);

    if (fifoclock[3] > cur_slot)
    {
        mem_log("VDP", "FIFO burn (slot %lld -> %lld)\n", cur_slot, fifoclock[3]);
        CPU_M68K.burn(access_slot_time(fifoclock[3]));
        // assert(access_slot_at(CPU_M68K.clock()) == fifoclock[3]);
        cur_slot = fifoclock[3];
    }

    cur_slot = MAX(fifoclock[0], cur_slot);
    fifoclock[3] = fifoclock[2];
    fifoclock[2] = fifoclock[1];
    fifoclock[1] = fifoclock[0];
    fifoclock[0] = cur_slot+numbytes;
    // printf("push fifo into slot: %lld\n", cur_slot+numbytes);

    // printf("push_fifo f0:%lld(%lld) f1:%lld(%lld) f2:%lld(%lld) f3:%lld(%lld)\n",
    //     access_slot_time(fifoclock[0]), fifoclock[0],
    //     access_slot_time(fifoclock[1]), fifoclock[1],
    //     access_slot_time(fifoclock[2]), fifoclock[2],
    //     access_slot_time(fifoclock[3]), fifoclock[3]);
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

    switch (code_reg & 0xF)
    {
    case 0x1:
        mem_log("VDP", "Direct VRAM write: addr:%x increment:%d vcounter:%d\n",
                address_reg, REG15_DMA_INCREMENT, _vcounter);
        push_fifo(value, 2);
        VRAM_W((address_reg    ) & 0xFFFF, value >> 8);
        VRAM_W((address_reg ^ 1) & 0xFFFF, value & 0xFF);
        address_reg += REG15_DMA_INCREMENT;
        break;
    case 0x3:
        mem_log("VDP", "Direct CRAM write: addr:%x increment:%d vcounter:%d\n",
                address_reg, REG15_DMA_INCREMENT, _vcounter);
        push_fifo(value, 1);
        CRAM[(address_reg >> 1) & 0x3F] = value;
        address_reg += REG15_DMA_INCREMENT;
        break;
    case 0x5:
        mem_log("VDP", "Direct VSRAM write: addr:%x increment:%d vcounter:%d\n",
                address_reg, REG15_DMA_INCREMENT, _vcounter);
        push_fifo(value, 1);
        VSRAM[(address_reg >> 1) & 0x3F] = value;
        address_reg += REG15_DMA_INCREMENT;
        break;

    case 0x0:
    case 0x4:
    case 0x8:
        // Write operation after setting up read: ignored (ex: ecco2, aladdin)
        push_fifo(value, 1);
        break;

    case 0x9:
        // invalid, ignore (vdpfifotesting)
        push_fifo(value, 1);
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
        status_reg |= STATUS_DMAPROGRESS;
        dma_poll();
    }
}

uint16_t VDP::data_port_r16(void)
{
    enum { CRAM_BITMASK = 0x0EEE, VSRAM_BITMASK = 0x07FF, VRAM8_BITMASK = 0x00FF };
    uint16_t value;

    command_word_pending = false;
    mem_log("VDP", "data port r16: code:%x, addr:%x\n", code_reg, address_reg);

    fifo_wait_empty();

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
        fprintf(stdout, "[VDP][PC=%06x](%04d) invalid data port write16: code:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, code_reg);
        assert(!"data port r not handled");
        return 0xFF;
    }


}

static bool buslocked = false;
static uint16_t buslockw = 0;

void VDP::control_port_w(uint16_t value)
{
    if (dma_m68k_running)
    {
        mem_log("VDP", "writing to control port during DMA, buslocked\n");
        assert(!buslocked);
        buslocked = true;
        buslockw = value;
        return;
    }

    if (command_word_pending) {
        // second half of the command word
        code_reg &= ~0x3C;
        code_reg |= (value >> 2) & 0x3C;
        address_reg &= 0x3FFF;
        address_reg |= value << 14;
        command_word_pending = false;
        mem_log("VDP", "command word 2nd: code:%02x addr:%04x (clock=%lld)\n", code_reg, address_reg, CPU_M68K.clock());
        if ((code_reg & (1<<5)) && REG1_DMA_ENABLED)
            dma_start();
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
    mem_log("VDP", "command word 1st: code:%02x addr:%04x (clock=%lld)\n", code_reg, address_reg, CPU_M68K.clock());
}

uint16_t VDP::status_register_r(void)
{
    uint16_t status = status_reg;
    int hc = hcounter();
    int vc = vcounter();

    if (fifo_empty())
        status |= STATUS_FIFO_EMPTY;
    if (fifo_full())
        status |= STATUS_FIFO_FULL;

    // VBLANK/HBLANK bit
    if (vblank())
        status |= STATUS_VBLANK;
    if (hblank())
        status |= STATUS_HBLANK;

    if (sprite_overflow)
        status |= STATUS_SPRITEOVERFLOW;
    if (sprite_collision)
        status |= STATUS_SPRITECOLLISION;
    if (VERSION_PAL)
        status |= STATUS_PAL;

    status &= 0x3FF;
    status |= m68k_read_memory_16(CPU_M68K.PC() & 0xFFFFFF) & 0xFC00;

    mem_log("VDP", "Status read (fifo full:%d empty:%d, status:%02x)\n",
        bool(status & STATUS_FIFO_FULL), bool(status & STATUS_FIFO_EMPTY), status & 0x302);

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

    mem_log("VDP", "HVCounter read: vc=%x, hc=%x, value=%x\n", vc, hc, ((vc & 0xFF) << 8) | (hc >> 1));

    return ((vc & 0xFF) << 8) | (hc >> 1);
}

void VDP::frame_begin(void)
{
    mode_v40 = REG1_PAL;
}

void VDP::frame_end(void) {}

void VDP::scanline_begin(uint8_t *screen)
{
    mode_h40 = REG12_MODE_H40;

    // if (mode_h40)
    // {
    //     if (hcounter() != 0xD)
    //     {
    //         printf("HCOUNTER40 ERROR: hc:%x, mclck:%lld\n", hcounter(), CPU_M68K.clock() % VDP_CYCLES_PER_LINE);
    //         assert(0);
    //     }
    // }
    // else
    // {
    //     assert(hcounter() == 0xB);
    // }

    // On these linese, the line counter interrupt is reloaded
    if  (_vcounter == 0)
    {
        if (REG0_LINE_INTERRUPT)
            mem_log("VDP", "HINTERRUPT counter reloaded: (_vcounter: %d, new counter: %d)\n", _vcounter, REG10_LINE_COUNTER);
        line_counter_interrupt = REG10_LINE_COUNTER;
    }

    dma_poll();

    mem_log("VDP", "render scanline %d\n", _vcounter);
    gfx_render_scanline(screen, _vcounter);

    display_disabled_hblank = false;
}

void VDP::scanline_hblank(uint8_t *screen)
{
    // if (mode_h40)
    // {
    //     if (hcounter() != 0x14A)
    //     {
    //         printf("ERROR HCOUNTER40 %x\n", hcounter());
    //         assert(0);
    //     }
    // }
    // else
    // {
    //     if (hcounter() != 0x10A)
    //     {
    //         printf("ERROR HCOUNTER32 %x\n", hcounter());
    //         assert(0);
    //     }
    // }

    _vcounter++;
    if (_vcounter == (VERSION_PAL ? 313 : 262))
    {
        _vcounter = 0;
        sprite_overflow = 0;
        sprite_collision = false;
    }

    if (_vcounter == (VERSION_PAL ? 0xF0 : 0xE0))
    {
        // vblank flag 0->1 transition
        assert(vblank());
        update_access_slot_freq();
        status_reg |= STATUS_VIRQPENDING;
    }

    if (_vcounter == (VERSION_PAL ? 312 : 261))
    {
        // vblank flag 1->0 transition
        assert(!vblank() || !REG1_DISP_ENABLED);
        update_access_slot_freq();
    }

    // Line counter is decremented before the start of line #0 up
    // to before the start of the first non-visible line, so
    // it's actually executed one time more per frame. This has been
    // semi-verified by outrunners and gunstarheroes.
    if (_vcounter <= (VERSION_PAL ? 0xF0 : 0xE0))
    {
        if (--line_counter_interrupt < 0)
        {
            if (REG0_LINE_INTERRUPT)
            {
                mem_log("VDP", "HINTERRUPT (_vcounter: %d, new counter: %d)\n", _vcounter, REG10_LINE_COUNTER);
                if (!dma_m68k_running)
                    CPU_M68K.irq(4);
            }

            line_counter_interrupt = REG10_LINE_COUNTER;
        }
    }

}

void VDP::scanline_end(uint8_t* screen)
{
    if (status_reg & STATUS_VIRQPENDING)   // vblank begin
    {
        if (REG1_VBLANK_INTERRUPT)
            //if (!dma_m68k_running)
                CPU_M68K.irq(6);
        CPU_Z80.set_irq_line(true);

        status_reg &= ~STATUS_VIRQPENDING;
    }
    if (_vcounter == (VERSION_PAL ? 0xF1 : 0xE1))
    {
        // The Z80 IRQ line stays asserted for one line
        CPU_Z80.set_irq_line(false);
    }
}

unsigned int VDP::scanline_hblank_clocks(void)
{
    // This value is required because the M68K is too slow, and sometimes
    // instructions take too long to complete. We don't want to generate
    // HINT too late otherwise programs don't have time to process it.
    enum { TOLERANCE=0 };

    if (mode_h40)
        return ((0x14A - 0xD - TOLERANCE) * VDP_CYCLES_PER_LINE + 420/2) / 420;
    else
        return ((0x10A - 0xB - TOLERANCE) * VDP_CYCLES_PER_LINE + 342/2) / 342;
}

unsigned int VDP::num_scanlines(void)
{
    return VERSION_PAL ? 313 : 262;
}


/**************************************************************
 * DMA
 **************************************************************/

int PCDMA_DEBUG = 0;

void VDP::dma_start()
{
    if (REG23_DMA_TYPE == 2)
    {
        // VRAM fill will trigger on next data port write
        dma_fill_pending = true;
        return;
    }

    PCDMA_DEBUG = CPU_M68K.PC();
    status_reg |= STATUS_DMAPROGRESS;
    dma_poll();
}

void VDP::dma_poll()
{
    if (!(status_reg & STATUS_DMAPROGRESS))
        return;

    switch (REG23_DMA_TYPE)
    {
        case 0: dma_m68k(); break;
        case 1: dma_m68k(); break;
        case 2: dma_fill(); break;
        case 3: dma_copy(); break;
    }
}

void VDP::dma_fill()
{
    /* FIXME: should be done in parallel and non blocking */
    int length = REG19_DMA_LENGTH;

    // This address is not required for fills,
    // but it's still updated by the DMA engine.
    uint16_t src_addr_low = REG21_DMA_SRCADDR_LOW;

    if (length == 0)
        length = 0xFFFF;

    mem_log("VDP", "DMA fill: src:%04x dst:%04x len:%x\n", src_addr_low, address_reg&0xFFFF, length);

    switch (code_reg & 0xF)
    {
    case 0x1:
        mem_log("VDP", "DMA VRAM fill: address:%04x, increment:%04x, length:%x, value: %04x\n",
            address_reg, REG15_DMA_INCREMENT, length, fifo[0]);
        do {
            VRAM_W((address_reg ^ 1) & 0xFFFF, fifo[0] >> 8);
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
        mem_err("VDP", "invalid code_reg:%x during DMA fill\n", code_reg);
        // assert(0);
    }

    // Clear DMA length at the end of transfer
    regs[19] = regs[20] = 0;

    // Update DMA source address after end of transfer
    regs[21] = src_addr_low & 0xFF;
    regs[22] = src_addr_low >> 8;

    status_reg &= ~STATUS_DMAPROGRESS;
}

void VDP::dma_m68k()
{
    int length = REG19_DMA_LENGTH;
    uint16_t src_addr_low = REG21_DMA_SRCADDR_LOW;
    uint32_t src_addr_high = REG23_DMA_SRCADDR_HIGH;

    // Special case for length = 0 (ex: sonic3d)
    if (length == 0)
        length = 0xFFFF;

    uint64_t endline = (CPU_M68K.clock() / VDP_CYCLES_PER_LINE + 1) * VDP_CYCLES_PER_LINE;

    mem_log("VDP", "(V=%x,H=%x)(clock=%lld, end=%lld, vblank=%d) DMA M68k->%s copy: src:%04x, dst:%04x, length:%d, increment:%d\n",
        vcounter(), hcounter(), CPU_M68K.clock(), endline, vblank(),
        (code_reg&0xF)==1 ? "VRAM" : ( (code_reg&0xF)==3 ? "CRAM" : "VSRAM"),
        (src_addr_high | src_addr_low) << 1, address_reg, length, REG15_DMA_INCREMENT);

    dma_m68k_running = true;
    int count = 0;

    do {
        unsigned int value = m68k_read_memory_16((src_addr_high | src_addr_low) << 1);
        ++count;

        switch (code_reg & 0xF) {
            case 0x1:
                push_fifo(value, 2);
                VRAM_W((address_reg    ) & 0xFFFF, value >> 8);
                VRAM_W((address_reg ^ 1) & 0xFFFF, value & 0xFF);
                break;
            case 0x3:
                push_fifo(value, 1);
                CRAM[(address_reg >> 1) & 0x3F] = value;
                break;
            case 0x5:
                push_fifo(value, 1);
                VSRAM[(address_reg >> 1) & 0x3F] = value;
                break;
            default:
                mem_err("VDP", "invalid code_reg:%x during DMA M68K\n", code_reg);
                assert(0);
                break;
        }

        address_reg += REG15_DMA_INCREMENT;
        src_addr_low += 1;

    } while (--length && CPU_M68K.clock() < endline);

    // Update DMA source address after end of transfer
    regs[21] = src_addr_low & 0xFF;
    regs[22] = src_addr_low >> 8;

    // Update DMA length
    regs[19] = length & 0xFF;
    regs[20] = length >> 8;

    if (!length)
    {
        mem_log("VDP", "DMA M68K finished\n");
        dma_m68k_running = false;
        status_reg &= ~STATUS_DMAPROGRESS;
        // if (PCDMA_DEBUG != CPU_M68K.PC())
        // {
        //     mem_err("VDP", "PCDMA DEBUG:%x ACTUAL:%x\n", PCDMA_DEBUG, CPU_M68K.PC());
        //     assert(0);
        // }

        if (buslocked)
        {
            mem_log("VDP", "Flushing buslocked write: %04x\n", buslockw);
            buslocked = false;
            control_port_w(buslockw);
        }
    }
    else
        mem_log("VDP", "DMA M68K interrupted (end of scanlined) (clock=%lld, xfer word=%d)\n", CPU_M68K.clock(), count);
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

    status_reg &= ~STATUS_DMAPROGRESS;
}

int VDP::get_nametable_A() { return REG2_NAMETABLE_A; }
int VDP::get_nametable_W() { return REG3_NAMETABLE_W; }
int VDP::get_nametable_B() { return REG4_NAMETABLE_B; }

void VDP::reset()
{
    command_word_pending = false;
    address_reg = 0;
    code_reg = 0;
    _vcounter = 0;
    status_reg = 0x3C00;
    line_counter_interrupt = 0;
    hvcounter_latched = false;
    base_access_slot = 0;
    base_access_slot_time = 0;
    access_slot_freq = 1;
    display_disabled_hblank = false;
    update_access_slot_freq();
}

#define M68K_READ_DELAY    (15*7)
#define M68K_WRITE_DELAY   (15*7)

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
    CPU_M68K.burn(CPU_M68K.clock() + M68K_WRITE_DELAY);
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
            fprintf(stderr, "[VDP][PC=%06x](%04d) unhandled write16 IO:%02x val:%04x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F, value);
            //assert(!"unhandled vdp_mem_w16");
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

    CPU_M68K.burn(CPU_M68K.clock() + M68K_READ_DELAY);
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
            fprintf(stderr, "[VDP][PC=%06x](%04d) unhandled read16 IO:%02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, address&0x1F);
            //assert(!"unhandled vdp_mem_r16");
            return 0xFFFF;
    }
}
