
/**************************************
 * Battery-backed RAM
 **************************************/

uint8_t *backup_ram_shadow;
bool backup_ram_present;
bool backup_ram_enabled;
uint8_t BACKUP_RAM[0x2000];

void backup_ram_switch_w8(unsigned int address, unsigned int value)
{
    if (address == 0xA130F1)
    {
        backup_ram_enabled = BIT(value, 0);
        return;
    }

    io_mem_w8(address, value);
}

unsigned int backup_ram_r8(unsigned int address)
{
    address &= 0xFFFF;
    if (backup_ram_enabled && address < 0x4000)
        return BACKUP_RAM[address >> 1];
    return backup_ram_shadow[address];
}

unsigned int backup_ram_r16(unsigned int address)
{
    address &= 0xFFFF;
    if (backup_ram_enabled && address < 0x4000)
        return (BACKUP_RAM[address] << 8) | BACKUP_RAM[address+1];
    return (backup_ram_shadow[address] << 8) | backup_ram_shadow[address+1];
}

void backup_ram_w8(unsigned int address, unsigned int value)
{
    address &= 0xFFFF;
    if (backup_ram_enabled && address < 0x4000)
    {
        BACKUP_RAM[address >> 1] = value;
        return;
    }

    assert(!"write to backup RAM when not enabled");
}

static memfunc_pair BACKUP_RAM_ACCESS = {
    backup_ram_r8, backup_ram_r16, backup_ram_w8, NULL
};


static memfunc_pair BACKUP_RAM_SWITCH = {
    io_mem_r8, io_mem_r16, backup_ram_switch_w8, io_mem_w16
};

void backup_ram_init(void)
{
    backup_ram_present = true;
    backup_ram_enabled = true;
    backup_ram_shadow = (uint8_t*)m68k_memtable[0x20];
    m68k_memtable[0x20] = MEMFUN_PAIR(&BACKUP_RAM_ACCESS);
    m68k_memtable[0xA1] = MEMFUN_PAIR(&BACKUP_RAM_SWITCH);
    memset(BACKUP_RAM, 0xFF, sizeof(BACKUP_RAM));  // required by dinodini
}

/**************************************
 * Super Street Fighter 2
 *
 * This game has an additional ROM bankswitch because
 * it's got a 5MB cartidge
 **************************************/

unsigned int ssf2_bankswitch_r8(unsigned int address)
{
    return io_mem_r8(address);
}

unsigned int ssf2_bankswitch_r16(unsigned int address)
{
    return io_mem_r16(address);
}

void ssf2_bankswitch_w8(unsigned int address, unsigned int value)
{
    int base = 0;

    switch (address)
    {
    case 0xA130F3: base = 0x08; break;
    case 0xA130F5: base = 0x10; break;
    case 0xA130F7: base = 0x18; break;
    case 0xA130F9: base = 0x20; break;
    case 0xA130FB: base = 0x28; break;
    case 0xA130FD: base = 0x30; break;
    case 0xA130FF: base = 0x38; break;
    case 0xA130F1:
        if (backup_ram_present)
        {
            backup_ram_switch_w8(address, value);
            return;
        }
        /* fallthrough */
    default:
        io_mem_w8(address, value);
        return;

    }

    mem_log("CARTIDGE", "SSF2 bankswitch: base:%x banknum:%x\n", base, value);

    assert(value < 5*1024*1024 / 512*1024);
    uint8_t *rom = ROM + 512*1024 * value;

    for (int i=0;i<0x8;++i)
        m68k_memtable[base+i] = rom + 0x10000*i;
}

void ssf2_bankswitch_w16(unsigned int address, unsigned int value)
{
    io_mem_w16(address, value);
}


static memfunc_pair SSF2_BANKSWITCH = {
    ssf2_bankswitch_r8, ssf2_bankswitch_r16, ssf2_bankswitch_w8, ssf2_bankswitch_w16
};


void cartidge_init(void)
{
    char name[64], region[64], code[64];

    memset(name, 0, sizeof(name));
    memcpy(name, &ROM[0x120], 0x30);
    fprintf(stderr, "Game name: %s\n", name);

    memset(code, 0, sizeof(code));
    memcpy(code, &ROM[0x180], 0xD);
    fprintf(stderr, "Code: %s\n", code);

    memset(region, 0, sizeof(region));
    memcpy(region, &ROM[0x1F0], 0x10);
    fprintf(stderr, "Region: %s\n", region);

    if (memchr(region, 'J', 4) || memchr(region, 'U', 4) || memchr(region, '1', 4))
    {
        VERSION_OVERSEA = 0;
        VERSION_PAL = 0;
    }
    else if (memchr(region, 'E', 4) || memchr(region, '8', 4) || memchr(region, 'F', 4))
    {
        VERSION_OVERSEA = 1;
        VERSION_PAL = 1;
    }

    if (ROM[0x1B0] == 'R' && ROM[0x1B1] == 'A')
    {
        fprintf(stderr, "RAM definition: start:%06x end:%06x\n",
            m68k_read_memory_32(0x1b4), m68k_read_memory_32(0x1b8));
        backup_ram_init();
    }
    else
    if (memcmp(code, "GM MK-1079 ", 10) == 0 ||   // Sonic3
        memcmp(code, "GM MK-1304 ", 10) == 0 ||   // Warriors of the sun
        memcmp(code, "GM MK-1354 ", 10) == 0)     // Story of thor
    {
        mem_log("CARTIDGE", "Backup RAM\n");
        backup_ram_init();
    }

    // Bankswitcher
    if (memcmp(code, "GM MK-12056", 10) == 0 ||   // Super Street Fighter 2
        memcmp(code, "GM MK-1354 ", 10) == 0)     // Story of thor
    {
        m68k_memtable[0xA1] = MEMFUN_PAIR(&SSF2_BANKSWITCH);
    }

    fprintf(stderr, "Autodetect mode: %s\n", VERSION_PAL ? "PAL" : "NTSC");
}
