#include "vdp.h"
#include "mem.h"
#include "cpu.h"
#include <assert.h>

int activecpu;

CpuM68K CPU_M68K;
CpuZ80 CPU_Z80;

void CpuM68K::init(void)
{
    _clock = 0;
    _delta = 0;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
}

void CpuM68K::run(uint64_t target)
{
    if (target <= _clock)
        return;

    int cycles = (target - (_clock+_delta)) / M68K_FREQ_DIVISOR;
    ::activecpu = 0;
    assert(!BIT(VDP.status_register_r(), 1));   // 68k->VRAM DMA should never be in progress
    _delta = m68k_execute(cycles) - cycles;
    if (_clock < target)
        _clock = target;
}

bool CpuM68K::running()
{
    return m68k_cycles_remaining() > 0;
}

uint64_t CpuM68K::clock(void)
{
    if (running())
        return _clock + m68k_cycles_run() * M68K_FREQ_DIVISOR;
    return _clock;
}

void CpuM68K::burn(uint64_t target)
{
    uint64_t now = clock();
    if (running() && target > now)
    {
        int rem = m68k_cycles_remaining();
        int run = m68k_cycles_run();
        int delta = (target - now) / M68K_FREQ_DIVISOR;
        mem_log("CPU", "Burning from %lld to %lld (delta: %d, run: %d, rem: %d)\n",
            now, target, delta, run, rem);
        m68k_modify_timeslice(-delta);
        assert(m68k_cycles_remaining() == rem-delta);
        assert(m68k_cycles_run() == run+delta);
    }
    else
    {
        int rem = m68k_cycles_remaining();
        int run = m68k_cycles_run();
        mem_log("CPU", "Idle Burning from %lld to %lld (delta: %d, run: %d, rem: %d)\n",
                    now, target, run, rem);
        _clock = target;
    }
}

void CpuM68K::reset(void)
{
    m68k_pulse_reset();
}

void CpuM68K::irq(int level)
{
    m68k_set_irq(level);
}


void CpuZ80::init(void)
{
    _reset_line = false;
    _busreq_line = false;
    _reset_once = false;
    _clock = 0;
}

void CpuZ80::reset(void)
{
    ResetZ80(&_cpu);
}

void CpuZ80::run(uint64_t target)
{
    if (_clock >= target)
        return;
    ::activecpu = 1;
    _cur_timeslice = (target - _clock) / Z80_FREQ_DIVISOR;
    int rem = 0;
    if (_reset_once && !_reset_line && !_busreq_line)
    {
        //mem_log("Z80", "Running %d cycles\n", _cur_timeslice);
        rem = ExecZ80(&_cpu, _cur_timeslice);
    }
    _clock = target - rem*Z80_FREQ_DIVISOR;
    _cur_timeslice = 0;
    ::activecpu = 0;
}

uint64_t CpuZ80::clock(void)
{
    return _clock + (_cur_timeslice - _cpu.ICount)*Z80_FREQ_DIVISOR;
}

void CpuZ80::sync(void)
{
    mem_log("Z80", "Sync up to: %ld (PC: %04x)\n", CPU_M68K.clock(), _cpu.PC.W);
    run(CPU_M68K.clock());
}

void CpuZ80::set_irq_line(bool line)
{
    if (!_reset_once) return;

    if (line)
        _cpu.IRequest = INT_IRQ;
    else
        _cpu.IRequest = INT_NONE;
}

void CpuZ80::set_busreq_line(bool line)
{
    sync();
    _busreq_line = line;
    mem_log("MEM", "Z80 BUSREQ: %d\n", line);
}

bool CpuZ80::set_reset_line(bool line)
{
    if (line == _reset_line)
        return false;
    _reset_line = line;
    mem_log("MEM", "Z80 RESET: %d (CLOCK: %d)\n", line, _clock);
    if (_reset_line)
    {
        // RESET is asserted, save the time
        _reset_start = clock();
    }
    else
    {
        if (clock() >= _reset_start + (1*8)*Z80_FREQ_DIVISOR)
        {
            mem_log("Z80", "Reset triggered (%ld, %ld)\n", clock(), _reset_start);
            ResetZ80(&_cpu);
            _reset_once = true;
            _clock += 20*Z80_FREQ_DIVISOR;
            return true;
        }
        else
            mem_log("Z80", "Reset ignored for too short pulse\n");
    }

    return false;
}


