#ifndef REMEM_H
#define REMEM_H

#include <stddef.h>
#include <stdbool.h>
#include "arena/arena.h"

// Macro to make Marking variables for the GC easier
// arguement is a regular variable (no pointer or address)
#define GC_MARK(var) \
    gcRootVariable((void**)&(var))
// Macro to make Unmarking variables for the GC easier
// arguement is a regular variable (no pointer or address)
#define GC_UNMARK(var) \
    gcUnrootVariable((void**)&(var))

// Will print basic info about the internal state of the GC
// prints current inuse pageCount empty pageCount last bytes...
void gcDebugPrintStats();

// Initializes the GC and returns a bool to indicate whether initialization succeded
// stack_top_hint is the address of a variable in main() ex:`&stackTop` used to scan the stack for memory addresses
// freeMemory is a boolean used to togle between GC collection behavior
// - if true the GC will free empty pages upon collect
// - if false the GC will save empty pages in the arena and cached in a list for reuse
bool gcInit(const void *stack_top_hint, bool freeMemory);

// Destroys the GC and arena it controlls, frees any associated memory
void gcDestroy();

// Manually trigger a collection from the GC to get more usable memory
void gcCollect();

// Allocates a `size` block of memory and returns a pointer to the base of it
// - any blocks to large to fit into GC pages will be allocated to an underlying arena these blocks will not be freed until the GC is destroyed
void *gcAlloc(size_t size);

// Manually root a variable for safety so that the GC will not free it until unrooted
void gcRootVariable(void **addr);

// Manually root a variable for safety so that the GC will then be able to free it on next collect
void gcUnrootVariable(void **addr);

#endif