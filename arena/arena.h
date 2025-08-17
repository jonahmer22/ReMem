#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#define BUFF_SIZE (size_t)(1024 * 1024) // 1mb by default
#define ARENA_ALIGN alignof(max_align_t)    // alignment (16 bits on most systems)
#define ROUND_UP(needed, align) \
    (((needed) + ((align) - 1)) & ~((align) - 1))   // rounding for memory alignment

// data structures

// MemBlock functions as a singly-linked list node
// it holds a rawblock of memory along with metadata for size, current inuse bytes
typedef struct MemBlock{
    char *buffer;   // start of the block
    size_t size;    // total bytes in buffer (default BUFF_SIZE)
    size_t head;    // which byte the arena has filled to (like a drive head on hdd)

    struct MemBlock *nextBlock; // next block of memory
} MemBlock;

// Arena functions as a singly-linked list with head and tail pointers
// nodes are MemBlocks, and the total number of blocks are stored in numBlocks
typedef struct Arena{
    MemBlock *head;
    MemBlock *tail;
    
    int numBlocks;
} Arena;

// =======
// regular
// =======

// Initializes the global Arena with an empty BUFF_SIZE MemBlock
// returns a pointer to the global Arena, which is optional to use
Arena* arenaInit();

// Destroys and frees all associated memory with the global Arena
void arenaDestroy();

// Frees all memory in the global arena except the original BUFF_SIZE MemBlock and resets all heads to the base of the block
// returns a pointer to the global Arena, which is optional to use
Arena* arenaReset();

// Returns a pointer to the base of a block of memory numBytes in size
// increments all global Arena head pointers and allocates extra memory if needed
void* arenaAlloc(size_t numBytes);

// Allocates a BUFF_SIZE MemBlock
// a pointer is returned to the base of the MemBlock and it is marked as full to the Arena
void* arenaAllocBuffsizeBlock();

// =====
// local
// =====

// Initializes a Arena with an empty BUFF_SIZE MemBlock
// returns a pointer to the Arena
Arena* arenaLocalInit();

// Destroys and frees all associated memory with a Arena passed in as an arguement
void arenaLocalDestroy(Arena *arena);

// Frees all memory associated with arena passed as arguement except the original BUFF_SIZE MemBlock and resets all heads to the base of the block
// returns a pointer to the Arena passed as arguement, which is optional to use
Arena* arenaLocalReset(Arena *arena);

// Returns a pointer to the base of a block of memory numBytes in size
// increments all Arena head pointers and allocates extra memory if needed to the Arena passed as an arguement
void* arenaLocalAlloc(Arena *arena, size_t numBytes);

// Allocates a BUFF_SIZE MemBlock
// a pointer is returned to the base of the MemBlock and it is marked as full to the Arena passed as an arguement
void* arenaLocalAllocBuffsizeBlock(Arena *larena);

#endif
