
extern "C" {
    #include "m68k/m68k.h"
    #include "Z80/Z80.h"
}

class CpuM68K
{
    uint64_t _clock;

public:
    void init();
    void reset();
    void run(uint64_t target_cycles);
    void irq(int level);
    uint64_t clock();
    unsigned int PC() { return m68k_get_reg(0, M68K_REG_PC); }
};

class CpuZ80
{
    friend void loadstate(const char *fn);
    friend void savestate(const char *fn);

private:
    Z80 _cpu;
    uint64_t _clock;
    int _cur_timeslice;
    bool _reset_line;
    bool _busreq_line;
    bool _reset_once;
    uint64_t _reset_start;

public:
    void init();
    void reset();
    void run(uint64_t target_cycles);
    void sync();
    uint64_t clock();
    unsigned int PC() { return _cpu.PC.W; }

    bool set_reset_line(bool assert);
    bool get_reset_line() { return _reset_line; };

    void set_busreq_line(bool assert);
    bool get_busreq_line() { return _busreq_line; };

    void set_irq_line(bool assert);
};

extern CpuM68K CPU_M68K;
extern CpuZ80 CPU_Z80;


