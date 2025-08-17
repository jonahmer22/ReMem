#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

// =======
// private
// =======

// Global arena
static Arena* arena;

// Initializes an empty MemBlock of size `aligned_alloc` to align and returns a pointer to it
static MemBlock* memBlockInit(size_t size, size_t align){
    MemBlock *memBlock = malloc(sizeof(MemBlock));
    if(!memBlock){
        perror("[FATAL]: Could not allocate MemBlock.");

        exit(1);
    }

    size = ROUND_UP(size, align);
    memBlock->buffer = aligned_alloc(align, size);
    if(!memBlock->buffer){
        perror("[FATAL]: Could not allocate MemBlock buffer.");
        free(memBlock);

        exit(1);
    }
    memBlock->nextBlock = NULL;
    memBlock->head = 0;
    memBlock->size = size;

    return memBlock;
}

// Adds a MemBlock to the linked list of MemBlocks 
static MemBlock* memBlockAdd(MemBlock *memBlock, size_t size){
    memBlock->nextBlock = memBlockInit(size, ARENA_ALIGN);

    return memBlock->nextBlock;
}

// Adds a MemBlock to the linked list of MemBlocks aligned to the size of BuffSize
static MemBlock* memBlockAddAlignedBuff(MemBlock *memBlock){
    memBlock->nextBlock = memBlockInit(BUFF_SIZE, BUFF_SIZE);

    return memBlock->nextBlock;
}

// Destroys and frees a MemBlock returning a pointer to the next one in the list
static MemBlock* memBlockDestroy(MemBlock* memBlock){
    MemBlock *temp = memBlock->nextBlock;
    free(memBlock->buffer);
    free(memBlock);
    return temp;
}

// ======
// public
// ======

// Initializes the global Arena with an empty BUFF_SIZE MemBlock
// returns a pointer to the global Arena, which is optional to use
Arena* arenaInit(){
    arena = malloc(sizeof(Arena));
    if(!arena){
        perror("[FATAL]: Could not allocate Arena.");

        exit(2);
    }

    arena->numBlocks = 1;
    arena->head = memBlockInit(BUFF_SIZE, ARENA_ALIGN);
    arena->tail = arena->head;

    return arena;
}

// Destroys and frees all associated memory with the global Arena
void arenaDestroy(){
    if(arena != NULL){
        MemBlock* temp = arena->head;
        for(; temp != NULL;){
            temp = memBlockDestroy(temp);
        }
        free(arena);
    
        return;
    }
}

// Frees all memory in the global arena except the original BUFF_SIZE MemBlock and resets all heads to the base of the block
// returns a pointer to the global Arena, which is optional to use
Arena* arenaReset(){
    MemBlock* temp = arena->head->nextBlock;
    for(; temp != NULL;){
        temp = memBlockDestroy(temp);
    }
    arena->head->nextBlock = NULL;
    arena->tail = arena->head;
    arena->head = 0;

    memset(arena->head->buffer, 0, BUFF_SIZE);

    return arena;
}

// Returns a pointer to the base of a block of memory numBytes in size
// increments all global Arena head pointers and allocates extra memory if needed
void* arenaAlloc(size_t numBytes){
    numBytes = ROUND_UP(numBytes, ARENA_ALIGN);

    if(numBytes > BUFF_SIZE){
        // add a block specifically for this big chunk of data
        arena->tail = memBlockAdd(arena->tail, numBytes);

        void* ptr = arena->tail->buffer + arena->tail->head;
        arena->tail->head += numBytes;

        return ptr;
    }
    else if(arena->tail->head + numBytes > arena->tail->size){
        // add on a new block and just add to there
        arena->tail = memBlockAdd(arena->tail, BUFF_SIZE);
        
        void* ptr = arena->tail->buffer + arena->tail->head;
        arena->tail->head += numBytes;

        return ptr;
    }
    else{
        // if we can just fit it in our current block
        void* ptr = arena->tail->buffer + arena->tail->head;
        arena->tail->head += numBytes;

        return ptr;
    }
}

// Allocates a BUFF_SIZE MemBlock
// a pointer is returned to the base of the MemBlock and it is marked as full to the Arena
void* arenaAllocBuffsizeBlock(){
    arena->tail = memBlockAddAlignedBuff(arena->tail);
        
    void* ptr = arena->tail->buffer + arena->tail->head;
    arena->tail->head = BUFF_SIZE;

    return ptr;
}

// =====
// local
// =====

// Initializes a Arena with an empty BUFF_SIZE MemBlock
// returns a pointer to the Arena
Arena* arenaLocalInit(){
    Arena *larena = malloc(sizeof(Arena));
    if(!larena){
        perror("[FATAL]: Could not allocate Arena.");

        exit(3);
    }

    larena->numBlocks = 1;
    larena->head = memBlockInit(BUFF_SIZE, ARENA_ALIGN);
    larena->tail = larena->head;

    return larena;
}

// Destroys and frees all associated memory with a Arena passed in as an arguement
void arenaLocalDestroy(Arena *larena){
    if(larena != NULL){
        MemBlock* temp = larena->head;
        for(; temp != NULL;){
            temp = memBlockDestroy(temp);
        }
        free(larena);
    
        return;
    }
}

// Frees all memory associated with arena passed as arguement except the original BUFF_SIZE MemBlock and resets all heads to the base of the block
// returns a pointer to the Arena passed as arguement, which is optional to use
Arena* arenaLocalReset(Arena *larena){
    MemBlock* temp = larena->head->nextBlock;
    for(; temp != NULL;){
        temp = memBlockDestroy(temp);
    }

    arena->head->nextBlock = NULL;
    arena->tail = arena->head;
    arena->head = 0;

    memset(larena->head->buffer, 0, BUFF_SIZE);

    return larena;
}

// Returns a pointer to the base of a block of memory numBytes in size
// increments all Arena head pointers and allocates extra memory if needed to the Arena passed as an arguement
void* arenaLocalAlloc(Arena *larena, size_t numBytes){
    numBytes = ROUND_UP(numBytes, ARENA_ALIGN);

    if(numBytes > BUFF_SIZE){
        // add a block specifically for this big chunk of data
        larena->tail = memBlockAdd(larena->tail, numBytes);

        void* ptr = larena->tail->buffer + larena->tail->head;
        larena->tail->head += numBytes;

        return ptr;
    }
    else if(larena->tail->head + numBytes > larena->tail->size){
        // add on a new block and just add to there
        larena->tail = memBlockAdd(larena->tail, BUFF_SIZE);
        
        void* ptr = larena->tail->buffer + larena->tail->head;
        larena->tail->head += numBytes;

        return ptr;
    }
    else{
        // if we can just fit it in our current block
        void* ptr = larena->tail->buffer + larena->tail->head;
        larena->tail->head += numBytes;

        return ptr;
    }
}

// Allocates a BUFF_SIZE MemBlock
// a pointer is returned to the base of the MemBlock and it is marked as full to the Arena passed as an arguement
void* arenaLocalAllocBuffsizeBlock(Arena *larena){
    larena->tail = memBlockAddAlignedBuff(larena->tail);
        
    void* ptr = larena->tail->buffer + larena->tail->head;
    larena->tail->head = BUFF_SIZE;

    return ptr;
}