#ifndef ARENA_H
#define ARENA_H

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define BUFF_SIZE (size_t)(1024 * 1024) // 1mb by default

// data structures

typedef struct MemBlock{
    char *buffer;   // start of the block
    size_t size;    // total bytes in buffer (default BUFF_SIZE)
    size_t head;    // which byte the arena has filled to (like a drive head on hdd)

    struct MemBlock *nextBlock; // next block of memory
} MemBlock;

typedef struct Arena{
    MemBlock *head;
    MemBlock *tail;
    
    int numBlocks;
} Arena;

// regular

Arena* arenaInit();

void arenaDestroy();

Arena* arenaReset();

void* arenaAlloc(size_t numBytes);

// local

Arena* arenaLocalInit();

void arenaLocalDestroy(Arena *arena);

Arena* arenaLocalReset(Arena *arena);

void* arenaLocalAlloc(Arena *arena, size_t numBytes);

#endif
