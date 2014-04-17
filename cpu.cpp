#include "vdp.h"
#include "mem.h"
#include "cpu.h"

int activecpu;

CpuM68K CPU_M68K;
CpuZ80 CPU_Z80;

void CpuM68K::init(void)
{
    _clock = 0;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
}

void CpuM68K::run(uint64_t target)
{
    ::activecpu = 0;
    //mem_log("M68K", "Running %d cycles (from %ld to %ld)\n", (target - m68k_clock) / M68K_FREQ_DIVISOR, m68k_clock, target);
    m68k_execute((target - _clock) / M68K_FREQ_DIVISOR);
    _clock = target;
}

uint64_t CpuM68K::clock(void)
{
    return _clock + m68k_cycles_run() * M68K_FREQ_DIVISOR;
}

void CpuM68K::reset(void)
{
    m68k_pulse_reset();
}


void CpuZ80::init(void)
{
    _reset_line = false;
    _busreq_line = false;
    _reset_once = false;
    _clock = 0;
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
    mem_log("Z80", "Sync up to: %ld\n", CPU_M68K.clock());
    run(CPU_M68K.clock());
}

void CpuZ80::irq(void)
{
    if (_reset_once)
        IntZ80(&_cpu, INT_IRQ);
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
        if (clock() >= _reset_start + (4*8)*Z80_FREQ_DIVISOR)
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


