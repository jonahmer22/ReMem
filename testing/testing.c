#include "../ReMem.h"
#include "../arena/arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>

#define ROUNDS 50000
#define SLOTS  2000

static const size_t SIZES[] = {
    16, 24, 32, 40, 48, 64, 80, 96, 128, 256, 512, 1024, 2048
};
#define NSIZES (sizeof(SIZES)/sizeof(SIZES[0]))

#define SAMPLE_EVERY 50
#define WARMUP_FRAC_NUM 1
#define WARMUP_FRAC_DEN 8

typedef struct {
    uint64_t total_alloc;
    uint64_t total_freed;   // “dropped” for GC/arena modes
    uint64_t peak_rss_kb;
    double   elapsed_s;
} BenchStats;

// ====================
// timing & RSS helpers
// ====================

// returns current time in nanoseconds
static inline uint64_t now_ns(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}

// returns current RSS based off of system RSS
static uint64_t read_rss_kb(void){
#if defined(__linux__)
    FILE *f = fopen("/proc/self/statm","r");
    if(f){
        unsigned long size=0,resident=0;
        if(fscanf(f, "%lu %lu", &size, &resident)==2){
            fclose(f);
            long page_kb = sysconf(_SC_PAGESIZE)/1024;
            return (uint64_t)resident * (uint64_t)page_kb;
        }
        fclose(f);
    }
#endif
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return (uint64_t)(ru.ru_maxrss / 1024); // ru_maxrss = bytes on macOS
#else
    return (uint64_t)ru.ru_maxrss;          // ru_maxrss = KB on Linux
#endif
}

// write some bytes so memory is committed
static inline void touch_bytes(void *p, size_t n){
    if(!p || n==0) return;
    unsigned char *b = (unsigned char*)p;
    const size_t step = 64;
    for(size_t i=0;i<n;i+=step) b[i] = (unsigned char)((i ^ (n>>3)) & 0xFF);
    b[n-1] = (unsigned char)((n ^ 0x5A) & 0xFF);
}

// uniform random in [0, n)
static inline int rnd(int n){ return rand() % n; }

// =============
// workload core
// =============

// Run the test on the GC
static void run_gc_mode(bool freeMemory, BenchStats *st){
    void *slots[SLOTS] = {0};
    size_t sizes[SLOTS]; memset(sizes, 0, sizeof(sizes));

    int stack_top_sentinel;
    if(!gcInit(&stack_top_sentinel, freeMemory)){
        fprintf(stderr, "gcInit failed\n");
        exit(1);
    }

    // warmup
    int warm = (SLOTS*WARMUP_FRAC_NUM)/WARMUP_FRAC_DEN;
    for(int i=0;i<warm;i++){
        size_t sz = SIZES[rnd(NSIZES)];
        void *p = gcAlloc(sz);
        touch_bytes(p, sz);
        slots[i] = p; sizes[i] = sz;
        st->total_alloc += sz;
    }

    uint64_t t0 = now_ns();
    for(int r=0;r<ROUNDS;r++){
        for(int k=0;k<SLOTS/2;k++){
            int idx = rnd(SLOTS);

            if(slots[idx]){
                st->total_freed += sizes[idx]; // conceptually dropped
                slots[idx] = NULL; sizes[idx] = 0;
            }

            size_t sz = SIZES[rnd(NSIZES)];
            void *p = gcAlloc(sz);
            touch_bytes(p, sz);
            slots[idx] = p; sizes[idx] = sz;
            st->total_alloc += sz;
        }

        if((r % SAMPLE_EVERY)==0){
            uint64_t rss = read_rss_kb();
            if(rss > st->peak_rss_kb) st->peak_rss_kb = rss;
        }
    }

    // drop all
    for(int i=0;i<SLOTS;i++){
        if(slots[i]){
            st->total_freed += sizes[i];
            slots[i] = NULL; sizes[i] = 0;
        }
    }
    st->peak_rss_kb = (st->peak_rss_kb > read_rss_kb()) ? st->peak_rss_kb : read_rss_kb();
    st->elapsed_s = (now_ns() - t0) / 1e9;

    gcDestroy();
}

// run the test using only malloc and free
static void run_malloc_mode(BenchStats *st){
    void *slots[SLOTS] = {0};
    size_t sizes[SLOTS]; memset(sizes, 0, sizeof(sizes));

    // warmup
    int warm = (SLOTS*WARMUP_FRAC_NUM)/WARMUP_FRAC_DEN;
    for(int i=0;i<warm;i++){
        size_t sz = SIZES[rnd(NSIZES)];
        void *p = malloc(sz);
        if(!p){ perror("malloc"); exit(1); }
        touch_bytes(p, sz);
        slots[i] = p; sizes[i] = sz;
        st->total_alloc += sz;
    }

    uint64_t t0 = now_ns();
    for(int r=0;r<ROUNDS;r++){
        for(int k=0;k<SLOTS/2;k++){
            int idx = rnd(SLOTS);

            if(slots[idx]){
                st->total_freed += sizes[idx];
                free(slots[idx]);
                slots[idx] = NULL; sizes[idx] = 0;
            }

            size_t sz = SIZES[rnd(NSIZES)];
            void *p = malloc(sz);
            if(!p){ perror("malloc"); exit(1); }
            touch_bytes(p, sz);
            slots[idx] = p; sizes[idx] = sz;
            st->total_alloc += sz;
        }

        if((r % SAMPLE_EVERY)==0){
            uint64_t rss = read_rss_kb();
            if(rss > st->peak_rss_kb) st->peak_rss_kb = rss;
        }
    }

    for(int i=0;i<SLOTS;i++){
        if(slots[i]){
            st->total_freed += sizes[i];
            free(slots[i]);
            slots[i] = NULL; sizes[i] = 0;
        }
    }
    st->peak_rss_kb = (st->peak_rss_kb > read_rss_kb()) ? st->peak_rss_kb : read_rss_kb();
    st->elapsed_s = (now_ns() - t0) / 1e9;
}

// run the test using explicitly the arena
static void run_arena_only_mode(BenchStats *st){
    // churn pointers but never free until the very end
    void *slots[SLOTS] = {0};
    size_t sizes[SLOTS]; memset(sizes, 0, sizeof(sizes));

    Arena *arena = arenaLocalInit();
    if(!arena){ fprintf(stderr,"arenaLocalInit failed\n"); exit(1); }

    // warmup
    int warm = (SLOTS*WARMUP_FRAC_NUM)/WARMUP_FRAC_DEN;
    for(int i=0;i<warm;i++){
        size_t sz = SIZES[rnd(NSIZES)];
        void *p = arenaLocalAlloc(arena, sz);
        touch_bytes(p, sz);
        slots[i] = p; sizes[i] = sz;
        st->total_alloc += sz;
    }

    uint64_t t0 = now_ns();
    for(int r=0;r<ROUNDS;r++){
        for(int k=0;k<SLOTS/2;k++){
            int idx = rnd(SLOTS);

            if(slots[idx]){
                // “drop” reference
                // actual memory stays until arena destroy
                st->total_freed += sizes[idx];
                slots[idx] = NULL; sizes[idx] = 0;
            }

            size_t sz = SIZES[rnd(NSIZES)];
            void *p = arenaLocalAlloc(arena, sz);
            touch_bytes(p, sz);
            slots[idx] = p; sizes[idx] = sz;
            st->total_alloc += sz;
        }

        if((r % SAMPLE_EVERY)==0){
            uint64_t rss = read_rss_kb();
            if(rss > st->peak_rss_kb) st->peak_rss_kb = rss;
        }
    }

    // arena still holds memory, just for counting
    for(int i=0;i<SLOTS;i++){
        if(slots[i]){
            st->total_freed += sizes[i];
            slots[i] = NULL; sizes[i] = 0;
        }
    }
    st->peak_rss_kb = (st->peak_rss_kb > read_rss_kb()) ? st->peak_rss_kb : read_rss_kb();
    st->elapsed_s = (now_ns() - t0) / 1e9;

    arenaLocalDestroy(arena);
}

// ============
// pretty print
// ============

static void print_stats(const char *label, const BenchStats *st){
    printf("-======-\n%s\n", label);
    printf("  time:           %.3f s\n", st->elapsed_s);
    printf("  total alloc:    %llu B\n", (unsigned long long)st->total_alloc);
    printf("  dropped/freed:  %llu B\n", (unsigned long long)st->total_freed);
    printf("  peak RSS:       %llu KB\n", (unsigned long long)st->peak_rss_kb);
}

// it's main, idk really what else to tell you
int main(void){
    srand(0xC0FFEE);

    BenchStats st_gc_free = {0}, st_gc_cache = {0}, st_malloc = {0}, st_arena = {0};

    // 1) ReMem GC, free pages back to OS
    run_gc_mode(true, &st_gc_free);
    print_stats("ReMem GC (freeMemory=true)", &st_gc_free);

    // 2) ReMem GC, cache pages for reuse
    run_gc_mode(false, &st_gc_cache);
    print_stats("ReMem GC (freeMemory=false)", &st_gc_cache);

    // 3) malloc/free
    run_malloc_mode(&st_malloc);
    print_stats("malloc/free", &st_malloc);

    // 4) arena-only (no frees until end)
    run_arena_only_mode(&st_arena);
    print_stats("arena-only (arenaAlloc)", &st_arena);

    return 0;
}
