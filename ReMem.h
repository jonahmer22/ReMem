#ifndef REMEM_H
#define REMEM_H

#include <stddef.h>
#include <stdbool.h>

#define GC_MARK(var) \
    gcRootVariable((void**)&(var))
#define GC_UNMARK(var) \
    gcUnrootVariable((void**)&(var))

void gcDebugPrintStats();

bool gcInit(const void *stack_top_hint);
void gcDestroy();

void gcCollect();

void *gcAlloc(size_t size);

void gcRootVariable(void **addr);
void gcUnrootVariable(void **addr);

#endif