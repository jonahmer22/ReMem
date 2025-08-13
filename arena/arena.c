#include "arena.h"

// private

static Arena* arena;

static MemBlock* memBlockInit(size_t size){
    MemBlock *memBlock = malloc(sizeof(MemBlock));
    if(!memBlock){
        perror("[FATAL]: Could not allocate MemBlock.");

        exit(1);
    }

    memBlock->buffer = calloc(size, sizeof(char));
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

static MemBlock* memBlockAdd(MemBlock *memBlock, size_t size){
    memBlock->nextBlock = memBlockInit(size);

    return memBlock->nextBlock;
}

static MemBlock* memBlockDestroy(MemBlock* memBlock){
    MemBlock *temp = memBlock->nextBlock;
    free(memBlock->buffer);
    free(memBlock);
    return temp;
}

// public

Arena* arenaInit(){
    arena = malloc(sizeof(Arena));
    if(!arena){
        perror("[FATAL]: Could not allocate Arena.");

        exit(2);
    }

    arena->numBlocks = 1;
    arena->head = memBlockInit(BUFF_SIZE);
    arena->tail = arena->head;

    return arena;
}

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

void* arenaAlloc(size_t numBytes){
    size_t align = sizeof(void*);
    uintptr_t base = (uintptr_t)(arena->tail->buffer + arena->tail->head);
    size_t mis = base % align;
    size_t pad = mis ? (align - mis) : 0;

    if(numBytes + pad > BUFF_SIZE){
        // add a block specifically for this big chunk of data
        arena->tail = memBlockAdd(arena->tail, numBytes + (align - 1));

        base = (uintptr_t)arena->tail->buffer;
        mis = base % align;
        pad = mis ? (align - mis) : 0;

        void* ptr = arena->tail->buffer + arena->tail->head + pad;
        arena->tail->head += pad + numBytes;

        return ptr;
    }
    else if(arena->tail->head + numBytes + pad > arena->tail->size){
        // add on a new block and just add to there
        arena->tail = memBlockAdd(arena->tail, BUFF_SIZE);

        base = (uintptr_t)arena->tail->buffer;
        mis = base % align;
        pad = mis ? (align - mis) : 0;
        
        void* ptr = arena->tail->buffer + arena->tail->head + pad;
        arena->tail->head += pad + numBytes;

        return ptr;
    }
    else{
        // if we can just fit it in our current block
        void* ptr = arena->tail->buffer + arena->tail->head + pad;
        arena->tail->head += pad + numBytes;

        return ptr;
    }
}

// local functionality

Arena* arenaLocalInit(){
    Arena *larena = malloc(sizeof(Arena));
    if(!larena){
        perror("[FATAL]: Could not allocate Arena.");

        exit(3);
    }

    larena->numBlocks = 1;
    larena->head = memBlockInit(BUFF_SIZE);
    larena->tail = larena->head;

    return larena;
}

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

void* arenaLocalAlloc(Arena *larena, size_t numBytes){
    size_t align = sizeof(void*);
    uintptr_t base = (uintptr_t)(larena->tail->buffer + larena->tail->head);
    size_t mis = base % align;
    size_t pad = mis ? (align - mis) : 0;

    if(numBytes + pad > BUFF_SIZE){
        // add a block specifically for this big chunk of data
        larena->tail = memBlockAdd(larena->tail, numBytes);

        base = (uintptr_t)larena->tail->buffer;
        mis = base % align;
        pad = mis ? (align - mis) : 0;

        void* ptr = larena->tail->buffer + larena->tail->head + pad;
        larena->tail->head += pad + numBytes;

        return ptr;
    }
    else if(larena->tail->head + numBytes + pad > larena->tail->size){
        // add on a new block and just add to there
        larena->tail = memBlockAdd(larena->tail, BUFF_SIZE);

        base = (uintptr_t)larena->tail->buffer;
        mis = base % align;
        pad = mis ? (align - mis) : 0;
        
        void* ptr = larena->tail->buffer + larena->tail->head + pad;
        larena->tail->head += pad + numBytes;

        return ptr;
    }
    else{
        // if we can just fit it in our current block
        void* ptr = larena->tail->buffer + larena->tail->head + pad;
        larena->tail->head += pad + numBytes;

        return ptr;
    }
}