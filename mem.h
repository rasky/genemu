
#define BIT(v, idx)       (((v) >> (idx)) & 1)
#define BITS(v, idx, n)   (((v) >> (idx)) & ((1<<(n))-1))
#define FETCH16(mem)      (((mem)[0] << 8) | (mem)[1])

#define MAX(a,b)          ((a)>(b)?(a):(b))
#define MIN(a,b)          ((a)<(b)?(a):(b))

#define FORCE_INLINE      __attribute__((always_inline))

void mem_init(int romsize);
void mem_log(const char *subs, const char *fmt, ...);
void mem_err(const char *subs, const char *fmt, ...);
int load_bin(const char *fn);
int load_smd(const char *fn);
bool mem_apply_gamegenie(char *gg);
