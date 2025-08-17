# ReMem - High-Performance Memory Recycler & Garbage Collector for C

ReMem is a lightweight, high performance memory recycler & garbage collector for single threaded* C.  
It reuses and releases freed memory through size classed arenas and O(1) page indexing, reducing allocation overhead and fragmentation for predictable, GC free performance.

---
*multithreading support is planned for the future, see roadmap

## Features
- Size classed allocation for fast lookups
- Arena-backed pages to minimize fragmentation
- O(1) page indexing for instant block-to-page resolution
- Low overhead and predictable performance
- Automatic memory management for "worry free" use of memory
- Significantly lowers total memory footprint*

*given that your program doesn't have large objects it never stops using.

## Compatability
Compatability is the main goal of this library, by using no external libraries or dependencies this library maintains the ability to be moved from platform to platform or even accross cpu architectures.

#### However!!
This library has only been tested and confirmed to work on the following platforms:
- Linux
- MacOS

This library has been tested on the following architectures:
- Apple Silicon Arm
- Intel x86-64
- Via Nano x86-64

## Performance
TLDR: for peak performance use without freeMemory so you can take advantage of caching of free pages. This library is slower than manualy using malloc and free and has a slightly larger memory footprint (obviously), but roughly ~8-9x quicker than interpreted languages with a gc like python.

This test was ran to assess speed and effectiveness of the GC to decrease total runtime footprint of a program that uses ~16GB of memory over it's lifetime.

##### NOTE: (RSS roughly translates to peak memory footprint)
#### Hardware aknowledgement
This test was ran on fairly low-end hardware, but for the sake of clarity & reference the relevant specs are listed below (courtesy of fastfetch). Please note that your results may differ based on factors like hardware, os, and build flags.
- Host: HP Laptop 14-ep2xxx
- Kernel: Linux 6.15.9
- CPU: Intel(R) N150 (4) @ 3.60 GHz
- Memory: 15.27 GiB
- Swap: 8.00 GiB

### Plain malloc & free
```
-======-
malloc/free
  time:           4.202 s
  total alloc:    16804705360 B
  dropped/freed:  16804705360 B
  peak RSS:       2576 KB
```
Plain malloc and free completed the test in ~4 seconds with a RSS of ~2.5MB. This will be used as a baseline of comparison for all other measurements.
### ReMem without freeing
```
-======-
ReMem GC (freeMemory=false)
  time:           9.968 s
  total alloc:    16798758688 B
  dropped/freed:  16798758688 B
  peak RSS:       10008 KB
```
Without freeing (allowing the GC to "cache" empty pages) the GC performed the test in ~10 seconds with a RSS of ~10MB.
#### Compared to malloc & free
- 2.37x longer execution time
- 3.89x larger RSS
### ReMem with freeing
```
-======-
ReMem GC (freeMemory=true)
  time:           39.950 s
  total alloc:    16801329840 B
  dropped/freed:  16801329840 B
  peak RSS:       9956 KB
```
While allowing the GC to free pages back to the memory pool the GC performed the test in ~40 seconds with a RSS of ~10MB.
#### Compared to malloc & free
- 9.51x longer execution time
- 3.86x larger RSS
### Shunting arena
```
-======-
arena-only (arenaAlloc)
  time:           19.235 s
  total alloc:    16808046848 B
  dropped/freed:  16808046848 B
  peak RSS:       12496680 KB
```
While using purely the "underlying arena"* the test completed in ~19 seconds and had an RSS of ~12GB
#### Compared to malloc & free
- 4.58x longer execution time
- 4851.20x larger RSS
### Python
```
-======-
Python (reference workload)
  time:           80.790 s
  total alloc:    16,805,727,608 B
  dropped/freed:  16,805,727,608 B
  peak RSS:       11,420 KB
```
Under the interpreted language python this test completed in ~81 seconds with a RSS of ~11MB
#### Compared to malloc & free
- 19.23x longer execution time
- 4.43x larger RSS

### Conclusion
While using a GC in C cannot possibly be quicker or more efficient than manual memory management, with proper settings and setup a program can gain the feature of GC from interpreted languages without the slowdown associated. 

#### Recomendations
If you do not want to manually manage memory I recommend to use this library with the `freeMemory` togled to false, there is no significant impact on RSS by enabling it and it is ~4x quicker without it. For short lived programs where pausing to do a GC sweep is not permissable use the underlying arena detailed below. 

##### When to use `freeMemory` then?
it is best to use `freeMemory` only when your program is long living and RSS is important or when system resources are verifiably low and reliable time sensetive alloc speeds are not absolutely necessary.

---

*The "underlying arena" is the shunting arena that provides the GC with memory (when it has large objects or freeMemory is false), this arena ships with this library and can be found at `./arena/`. Please note that this arena is also licensed under [GPL v3.0](LICENSE) and can be found on my github page.
#### Basics of using Arena
Please read the source or see my github repo for more in depth instructions.
- `arenaInit()`: initializes the global arena.
- `arenaAlloc(size_t size)`: allocates a `size` sized block of memory from the arena. 
- `arenaDestroy()`: destroys and frees all memory associated with the arena.

All of these functions have an accompanying "Local" function that take an `Arena *` as an arguement and allow for usage of mutliple arenas.
## Function Documentation
### `void gcDebugPrintStats()`
Will print basic info about the internal state of the GC prints current inuse pageCount empty pageCount last bytes.

---
### `bool gcInit(const void *stack_top_hint, bool freeMemory)`
Initializes the GC and returns a bool to indicate whether initialization succeded.
stack_top_hint is the address of a variable in main() ex:`&stackTop` used to scan the stack for memory addresses.
freeMemory is a boolean used to togle between GC collection behavior.
- if true the GC will free empty pages upon collect.
- if false the GC will save empty pages in the arena and cached in a list for reuse.

---
### `void gcDestroy()`
Destroys the GC and arena it controlls, frees any associated memory.

---
### `void gcCollect()`
Manually trigger a collection from the GC to get more usable memory.

---
### `void *gcAlloc(size_t size)`
Allocates a `size` block of memory and returns a pointer to the base of it.
- any blocks to large to fit into GC pages will be allocated to an underlying arena these blocks will not be freed until the GC is destroyed.

---
### `void gcRootVariable(void **addr)`
Manually root a variable for safety so that the GC will not free it until unrooted.

---
### `void gcUnrootVariable(void **addr)`
Manually root a variable for safety so that the GC will then be able to free it on next collect.

---
## Example Usage
You can also see `./testing/testing.c` for a more in depth example (used to benchmark performance).
```Example.c
#include "ReMem.h"

int main(int argc, char **argv){
    int stackTop;   // put any variable on the stack as a hint for where to start
    gcInit(&stackTop);  // initialize the GC by passing the address of the stack hint

    // proceed to use the gcAlloc() function as a drop in for malloc() without any accompanying free
    int *arr = gcAlloc(sizeof(int) * 1024);

    // safely destroy the GC
    gcDestroy();
}
```
## Roadmap
The below listed functionality are things I plan to eventually add to this library.
- Nursery: Add in Nursery support alongside current functionality. There should be a 1.5-5x speedup from implementing and using this (this is an estimate though).
- Multithreading: Allow for Multithreaded applications (long term goal).

## License
[GPL v3.0](LICENSE)