#include "vdp.h"
#include <stdio.h>
#include <stdint.h>
extern "C" {
    #include "m68k/m68k.h"
}
extern int framecounter;

class VDP
{
private:
    uint8_t VRAM[0x10000];
    uint16_t CRAM[0x40];
    uint16_t VSRAM[0x40];  // only 40 words are really used
    uint8_t regs[0x20];
    uint16_t address_reg;
    uint8_t code_reg;
    int vcounter;
    bool command_word_pending;

private:
    void register_w(int reg, uint8_t value);

public:
    void scanline();
    void reset();
    void control_port_w(uint16_t value);
    void data_port_w16(uint16_t value);

} VDP;


void VDP::register_w(int reg, uint8_t value)
{
    regs[reg] = value;
    fprintf(stdout, "[VDP][PC=%06x](%04d) reg:%02x <- %02x\n", m68k_get_reg(NULL, M68K_REG_PC), framecounter, reg, value);
}

void VDP::data_port_w16(uint16_t value)
{
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

void VDP::reset()
{
    command_word_pending = false;
    address_reg = 0;
    code_reg = 0;
    vcounter = 0;
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
            // seems like a CPU/VDP bug, but the next opcode is returned here,
            // with the 0x40 bit set to 0.
            ret = m68k_read_memory_16(m68k_get_reg(NULL, M68K_REG_PC));
            return ret & ~0x40;
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

