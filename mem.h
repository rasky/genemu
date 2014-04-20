
#define BIT(v, idx)       (((v) >> (idx)) & 1)
#define BITS(v, idx, n)   (((v) >> (idx)) & ((1<<(n))-1))
#define FETCH16(mem)      (((mem)[0] << 8) | (mem)[1])

void mem_init(int romsize);
void mem_log(const char *subs, const char *fmt, ...);
int load_bin(const char *fn);
int load_smd(const char *fn);
