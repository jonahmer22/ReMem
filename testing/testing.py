import os, sys, time, random, resource

ROUNDS = 50_000
SLOTS = 2_000
SIZES = [16, 24, 32, 40, 48, 64, 80, 96, 128, 256, 512, 1024, 2048]
SAMPLE_EVERY = 50
WARMUP_FRAC = 1/8

def read_rss_kb() -> int:
    if sys.platform.startswith("linux"):
        try:
            with open("/proc/self/statm","r") as f:
                _size, resident = f.read().split()[:2]
            pages = int(resident)
            page_kb = os.sysconf("SC_PAGESIZE") // 1024
            return pages * page_kb
        except Exception:
            pass
    ru = resource.getrusage(resource.RUSAGE_SELF)
    # macOS returns bytes but Linux returns KB
    return (ru.ru_maxrss // 1024) if sys.platform == "darwin" else ru.ru_maxrss

def touch(b: bytearray):
    n = len(b)
    step = 64
    for i in range(0, n, step):
        b[i] = (i ^ (n >> 3)) & 0xFF
    if n:
        b[-1] = (n ^ 0x5A) & 0xFF

def run_python_bench():
    slots = [None] * SLOTS
    sizes = [0] * SLOTS
    total_alloc = 0
    total_freed = 0
    peak_rss_kb = 0

    warm = int(SLOTS * WARMUP_FRAC)
    for i in range(warm):
        sz = random.choice(SIZES)
        b = bytearray(sz)
        touch(b)
        slots[i] = b
        sizes[i] = sz
        total_alloc += sz

    t0 = time.perf_counter()
    for r in range(ROUNDS):
        for _ in range(SLOTS // 2):
            idx = random.randrange(SLOTS)
            if slots[idx] is not None:
                total_freed += sizes[idx]
                slots[idx] = None
                sizes[idx] = 0
            sz = random.choice(SIZES)
            b = bytearray(sz)
            touch(b)
            slots[idx] = b
            sizes[idx] = sz
            total_alloc += sz

        if (r % SAMPLE_EVERY) == 0:
            peak_rss_kb = max(peak_rss_kb, read_rss_kb())

    for i in range(SLOTS):
        if slots[i] is not None:
            total_freed += sizes[i]
            slots[i] = None
            sizes[i] = 0

    peak_rss_kb = max(peak_rss_kb, read_rss_kb())
    elapsed = time.perf_counter() - t0
    return elapsed, total_alloc, total_freed, peak_rss_kb

def main():
    random.seed(0xC0FFEE)
    elapsed, alloc_b, freed_b, peak = run_python_bench()
    print("-======-")
    print("Python (reference workload)")
    print(f"  time:           {elapsed:.3f} s")
    print(f"  total alloc:    {alloc_b:,} B")
    print(f"  dropped/freed:  {freed_b:,} B")
    print(f"  peak RSS:       {peak:,} KB")

if __name__ == "__main__":
    main()
