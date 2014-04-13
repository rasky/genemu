
extern int Z80_BUSREQ;
extern int Z80_RESET;

#define BIT(v, idx)       (((v) >> (idx)) & 1)
#define BITS(v, idx, n)   (((v) >> (idx)) & ((1<<(n))-1))

void mem_init(int romsize);
void mem_log(const char *subs, const char *fmt, ...);
int load_rom(const char *fn);
