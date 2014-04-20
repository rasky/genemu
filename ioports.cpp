#include "mem.h"
#include <SDL.h>
extern "C" {
    #include "hw.h"
}

class IoPort
{
private:
    uint8_t data;
    uint8_t ctrl;

    uint8_t txdata;
    uint8_t rxdata;
    uint8_t sctrl;

protected:
    virtual uint8_t connected_lines() = 0;
    virtual void write_lines(uint8_t) = 0;
    virtual uint8_t read_lines() = 0;

public:
    void init(void)
    {
        data = 0x7F;
        ctrl = 0;
        txdata = 0xFF;
        rxdata = 0x00;
        sctrl = 0;
    }

    void write_ctrl(uint8_t value)
    {
        ctrl = value;
    }
    uint8_t read_ctrl()
    {
        return ctrl;
    }

    void write_data(uint8_t value)
    {
        data = value;
        write_lines(data & ctrl & connected_lines());
    }
    uint8_t read_data()
    {
        // Computer mask of bits marked as input
        // and currently connected to some external hardware
        uint8_t mask = ~ctrl & connected_lines();
        uint8_t value = read_lines();

        // Unconnected bits and output bits simply returns the
        // any latched value.
        return (value & mask) | (data & ~mask);
    }
};

class Gamepad : public IoPort
{
private:
    int _TH;
    int _km;

protected:
    uint8_t connected_lines()
    {
        return 0x7F;
    }

    void write_lines(uint8_t value)
    {
        _TH = BIT(value, 6);
    }

    uint8_t read_lines()
    {
        if (_km > 0)
            return 0xFF;

        uint8_t ret;
        int *km = keymaps[_km];

        if (_TH)
        {
            if (keystate[km[0]])
                ret |= (1 << 0);
            if (keystate[km[1]])
                ret |= (1 << 1);
            if (keystate[km[2]])
                ret |= (1 << 2);
            if (keystate[km[3]])
                ret |= (1 << 3);
            if (keystate[km[6]])
                ret |= (1 << 4);
            if (keystate[km[7]])
                ret |= (1 << 5);
        }
        else
        {
            if (keystate[km[0]])
                ret |= (1 << 0);
            if (keystate[km[1]])
                ret |= (1 << 1);
            ret |= (1 << 2);
            ret |= (1 << 3);
            if (keystate[km[5]])
                ret |= (1 << 4);
            if (keystate[km[4]])
                ret |= (1 << 5);
        }

        return ~ret;
    }

public:
    static int keymaps[1][8];

    Gamepad(int km): _km(km) {}
};

int Gamepad::keymaps[1][8] = {
        { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_1, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C }
};

Gamepad PORT_A(0), PORT_B(1), PORT_C(2);


void ioports_init(void)
{
    PORT_A.init();
    PORT_B.init();
    PORT_C.init();
}

uint8_t ioports_read(unsigned int port)
{
    port |= 1;

    switch (port)
    {
    case 0x3:  return PORT_A.read_data();
    case 0x5:  return PORT_B.read_data();
    case 0x7:  return PORT_C.read_data();

    case 0x9:  return PORT_A.read_ctrl();
    case 0xB:  return PORT_B.read_ctrl();
    case 0xD:  return PORT_C.read_ctrl();

    default:
        mem_log("IOPORTS", "Unhandled read8: %02x\n", port);
        return 0xFF;
    }
}

void ioports_write(unsigned int port, uint8_t value)
{
    port |= 1;

    switch (port)
    {
    case 0x3:  PORT_A.write_data(value); return;
    case 0x5:  PORT_B.write_data(value); return;
    case 0x7:  PORT_C.write_data(value); return;

    case 0x9:  PORT_A.write_ctrl(value); return;
    case 0xB:  PORT_B.write_ctrl(value); return;
    case 0xD:  PORT_C.write_ctrl(value); return;

    default:
        mem_log("IOPORTS", "Unhandled write8: %02x\n", port);
    }
}
