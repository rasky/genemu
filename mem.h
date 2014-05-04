
#define BIT(v, idx)       (((v) >> (idx)) & 1)
#define BITS(v, idx, n)   (((v) >> (idx)) & ((1<<(n))-1))
#define FETCH16(mem)      (((mem)[0] << 8) | (mem)[1])


#define FORCE_INLINE      __attribute__((always_inline))
#define MAX(a,b)          ((a)>(b)?(a):(b))
#define MIN(a,b)          ((a)<(b)?(a):(b))

#define DISABLE_LOGGING   0

void mem_init(int romsize);
int load_bin(const char *fn);
int load_smd(const char *fn);
bool mem_apply_gamegenie(const char *gg);

#if DISABLE_LOGGING
	#define mem_log(...)  do {} while(0)
	#define mem_err(...)  do {} while(0)
#else
	void mem_log(const char *subs, const char *fmt, ...);
	void mem_err(const char *subs, const char *fmt, ...);
#endif
