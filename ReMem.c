#include "ReMem.h"
#include "arena/arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

// -=*##############*=-
//    PRIVATE THINGS
// -=*##############*=-

// ========================
// Structures & Global Info
// ========================

// byte sizes that page blocks will be broken up into
static const size_t sizeClasses[] = {
    16, 32, 64, 128, 256, 512,
    1024, 2048, 4096, 8192,
    16384, 32768, 65536, 131072, 262144
};
#define NUM_CLASSES \
    (sizeof(sizeClasses) / sizeof(sizeClasses[0]))

// align helpers
#define ALIGN_DOWN(x,a) \
    ((uintptr_t)(x) & ~((uintptr_t)(a) - 1))
#define ALIGN_UP(x,a) \
    (((uintptr_t)(x) + ((uintptr_t)(a) - 1)) & ~((uintptr_t)(a) - 1))

typedef struct Page{
    // pointer to arena buffer
    void *block;

    // pages info
    size_t sizeClass;   // slot size (bytes) for this page
    uint32_t nslots;    // how many slots fit into block
    uint32_t inuseCount;// number of currently allocated slots
    int32_t freeHead;   // index of first free slot (-1 if none)

    // bit arrays to mark for gc collection
    uint8_t *inuseBits;
    uint8_t *markBits;  // if reachable by stack pointer

    struct Page *nextPage;
} Page;

typedef struct Book{
    // Pages
    Page *classPages[NUM_CLASSES];  // list of pointera for each class size
    Page *emptyPages;

    int numPages;
} Book;

typedef struct WorkItem{
    Page *page;
    uint32_t idx;
} WorkItem;

typedef struct GC{
    // where the stack's top should be
    const void *stack_top_hint; // has to be before where the program begins in main()

    // shunting arena
    Arena *arena;

    // book of pages
    Book book;
    
    // roots marked as in use
    void ***roots;
    size_t rootsLen;
    size_t rootsCap;

    // GC pressure stats
    size_t bytesSinceLastGC;
    size_t lastLiveBytes;
    double growthFactor;
} GC;

// dynamic array for worklist items
static WorkItem *worklist = NULL;
static size_t workLen = 0;
static size_t workCap = 0;

// open addressing hash map (key = *page->block)
static uintptr_t *pageIndexKeys = NULL; // 0 means empty slot
static Page **pageIndexVals = NULL;
static size_t pageIndexCap  = 0;    // power of two
static size_t pageIndexCnt  = 0;

// global reference to gc (maybe add multiple as an array of gc's later)
static GC gc;

// ====================
// Hash Table & Helpers
// ====================

static inline uint64_t hash64(uint64_t x){
    // SplitMix64-ish hashing
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;

    return x;
}

static void pageIndexInit(size_t cap){
    if(cap < 64) cap = 64;
    // round to power of two

    size_t p = 1;
    while(p < cap) p <<= 1;

    // set defaults for global info
    pageIndexCap = p;
    pageIndexCnt = 0;
    pageIndexKeys = calloc(pageIndexCap, sizeof(uintptr_t));
    pageIndexVals = calloc(pageIndexCap, sizeof(Page*));

    if(!pageIndexKeys || !pageIndexVals){
        perror("[FATAL]: Could not allocate page index.");

        exit(90);
    }
}

static void pageIndexFree(){
    free(pageIndexKeys);
    pageIndexKeys = NULL;

    free(pageIndexVals);
    pageIndexVals = NULL;

    pageIndexCap = pageIndexCnt = 0;
}

static void pageIndexGrow(){
    // save all current values in temp variables
    size_t oldCap = pageIndexCap;
    uintptr_t *oldKeys = pageIndexKeys;
    Page **oldVals = pageIndexVals;

    // grow by factor of 2
    pageIndexInit(oldCap ? oldCap * 2 : 128);

    // reinsert all old values into new expanded hash
    for(size_t i = 0; i < oldCap; i++){
        uintptr_t k = oldKeys[i];

        if(k){
            // reinsert
            uint64_t h = hash64(k);
            size_t mask = pageIndexCap - 1;
            size_t pos = (size_t)h & mask;

            while(pageIndexKeys[pos]){
                pos = (pos + 1) & mask;
            }

            pageIndexKeys[pos] = k;
            pageIndexVals[pos] = oldVals[i];
            pageIndexCnt++;
        }
    }

    free(oldKeys);
    free(oldVals);
}

static void pageIndexInsert(Page *page){
    // get pointer to base of page block
    uintptr_t base = (uintptr_t)page->block;

    if(pageIndexCap == 0) \
        pageIndexInit(128);

    if((pageIndexCnt + 1) * 10 >= pageIndexCap * 7){
        pageIndexGrow();
    }

    // get position from pointer and mask
    uint64_t h = hash64(base);
    size_t mask = pageIndexCap - 1;
    size_t pos = (size_t)h & mask;

    while(pageIndexKeys[pos] && pageIndexKeys[pos] != base){
        pos = (pos + 1) & mask;
    }

    // finally insert and update values
    if(!pageIndexKeys[pos]) \
        pageIndexCnt++;
    pageIndexKeys[pos] = base;
    pageIndexVals[pos] = page;
}

static Page *pageIndexFindByAddr(void *p){
    if(pageIndexCap == 0) \
        return NULL;

    // cast pointer align for hash indexing
    uintptr_t addr = (uintptr_t)p;
    uintptr_t base = ALIGN_DOWN(addr, BUFF_SIZE);

    // get position
    uint64_t h = hash64(base);
    size_t mask = pageIndexCap - 1;
    size_t pos = (size_t)h & mask;

    // get page index
    while(pageIndexKeys[pos]){
        if(pageIndexKeys[pos] == base){
            return pageIndexVals[pos];
        }

        pos = (pos + 1) & mask;
    }

    // didn't find anything
    return NULL;
}

// ===========
// Bit Helpers
// ===========

static inline size_t bitByte(size_t i){
    return i >> 3;
}

static inline uint8_t bitMask(size_t i){
    return (uint8_t)(1u << (i & 7));
}

// ==============
// Slot Alignment
// ==============

static inline void *slotBase(Page *page, uint32_t idx){
    return (void *)((uintptr_t)page->block + (uintptr_t)idx * page->sizeClass);
}

static inline int32_t *slotNextPtr(Page *page, uint32_t idx){
    return (int32_t *)slotBase(page, idx);
}

// ================
// Pages Management
// ================

static int classForSize(size_t size){
    for(int i = 0; i < (int)NUM_CLASSES; i++){
        if(size <= sizeClasses[i]){
            return i;
        }
    }

    // larger than biggest class
    return -1;
}

static Page *pageInitForClass(int classIndex){
    Page *page = malloc(sizeof(Page));
    if(page == NULL){
        perror("[FATAL]: Could not allocate Page metadata.");
        gcDestroy();

        exit(52);
    }

    // over allocate to force BUFF_SIZE alignment
    size_t need = BUFF_SIZE + (BUFF_SIZE - 1);
    void *raw = arenaLocalAlloc(gc.arena, need);
    if(raw == NULL){
        perror("[FATAL]: Could not allocate Page block.");
        free(page);
        gcDestroy();

        exit(53);
    }
    void *aligned = (void*)ALIGN_UP(raw, BUFF_SIZE);

    // initialize page
    page->block = aligned;
    page->sizeClass = sizeClasses[classIndex];
    page->nslots = (uint32_t)(BUFF_SIZE / page->sizeClass);
    page->inuseCount = 0;
    page->freeHead = 0;
    page->nextPage = NULL;

    // number of bytes for bit arrays
    size_t nbytes = (page->nslots + 7) / 8;
    page->inuseBits = calloc(nbytes, 1);
    page->markBits  = calloc(nbytes, 1);
    if(page->inuseBits == NULL || page->markBits == NULL){
        perror("[FATAL]: Could not allocate Page bitmaps.");
        free(page->inuseBits);
        free(page->markBits);
        free(page);
        gcDestroy();

        exit(54);
    }

    // build freelist with -1 as end marker
    for(uint32_t i = 0; i < page->nslots; i++){
        *slotNextPtr(page, i) = (i + 1 < page->nslots) ? (int32_t)(i + 1) : -1;
    }
    pageIndexInsert(page);

    return page;
}

static void pageResetForClass(Page *page, int classIndex){
    // base address and index entry remain valid
    page->sizeClass = sizeClasses[classIndex];
    page->nslots = (uint32_t)(BUFF_SIZE / page->sizeClass);
    page->inuseCount = 0;
    page->freeHead = 0;

    // number of bytes for bit arrays
    size_t nbytes = (page->nslots + 7) / 8;
    free(page->inuseBits);
    free(page->markBits);
    page->inuseBits = calloc(nbytes, 1);
    page->markBits  = calloc(nbytes, 1);
    if(page->inuseBits == NULL || page->markBits == NULL){
        perror("[FATAL]: Could not reallocate Page bitmaps.");
        free(page->inuseBits);
        free(page->markBits);
        gcDestroy();

        exit(55);
    }

    // reset freelist with -1 as end marker
    for(uint32_t i = 0; i < page->nslots; i++){
        *slotNextPtr(page, i) = (i + 1 < page->nslots) ? (int32_t)(i + 1) : -1;
    }
}

static void pageDestroyMeta(Page *page){
    // page->block memory stays in the arena
    free(page->inuseBits);
    free(page->markBits);
    page->block = NULL;
    page->inuseBits = NULL;
    page->markBits = NULL;
    page->nslots = 0;
    page->inuseCount = 0;
    page->freeHead = -1;
    page->sizeClass = 0;
    page->nextPage = NULL;
    free(page);
}

// ===============
// Book Management
// ===============

static void bookInit(Book *book){
    for(size_t i = 0; i < NUM_CLASSES; i++){
        book->classPages[i] = NULL;
    }

    book->emptyPages = NULL;
    book->numPages = 0;
}

static void pagesDestroyList(Page *pages){
    for(Page *page = pages; page != NULL;){
        Page *next = page->nextPage;
        pageDestroyMeta(page);
        page = next;
    }
}

static void bookDestroy(Book *book){
    for(size_t i = 0; i < NUM_CLASSES; i++){
        pagesDestroyList(book->classPages[i]);
        book->classPages[i] = NULL;
    }

    pagesDestroyList(book->emptyPages);
    book->emptyPages = NULL;
    book->numPages = 0;
}

// ===========
// Roots Array
// ===========

static void addRoot(void **root){
    // if roots array is empty
    if(gc.rootsLen == 0){
        gc.roots = malloc(sizeof(void **) * 16);
        if(gc.roots == NULL){
            perror("[FATAL]: Could not allocate GC roots.");
            gcDestroy();

            exit(50);
        }
        gc.rootsCap = 16;
        gc.roots[0] = root;
        gc.rootsLen++;

        return; // exit and skip adding
    }
    // if roots array needs to be expanded
    else if((gc.rootsLen) >= gc.rootsCap){
        void ***temp = realloc(gc.roots, sizeof(void **) * (gc.rootsCap * 2));
        if(temp == NULL){
            perror("[FATAL]: Could not reallocate GC roots.");
            gcDestroy();

            exit(51);
        }
        gc.roots = temp;
        gc.rootsCap *= 2;
    }

    // try to add root into vacant slot
    for(size_t i = 0; i < gc.rootsLen; i++){
        if(gc.roots[i] == NULL || gc.roots[i] == root){
            gc.roots[i] = root;

            return; // exit early
        }
    }

    // add at end as last resort
    gc.roots[gc.rootsLen] = root;
    gc.rootsLen++;
}

static bool removeRoot(void **root){
    // search to find the root
    for(size_t i = 0; i < gc.rootsLen; i++){
        if(gc.roots[i] == root){
            // set it to null and exit
            gc.roots[i] = NULL;

            return true;
        }
    }

    // couldn't find anything
    return false;
}

// ===========================
// Pressure Based Auto Collect
// ===========================

static size_t recomputeLiveBytes(){
    size_t live = 0;

    for(size_t c = 0; c < NUM_CLASSES; c++){    // for every class
        for(Page *page = gc.book.classPages[c]; page != NULL; page = page->nextPage){   // while there are still pages to be counted
            live += (size_t)page->inuseCount * page->sizeClass; // add the number of bytes it is curently using
        }
    }

    return live;
}

static inline void maybeCollectOnPressure(size_t upcomingAllocBytes){
    size_t baseline = gc.lastLiveBytes ? gc.lastLiveBytes : BUFF_SIZE;
    size_t threshold = (size_t)(baseline * gc.growthFactor);

    if(gc.bytesSinceLastGC + upcomingAllocBytes > threshold){
        gcCollect();
        gc.bytesSinceLastGC = 0;
    }
}

// ==================
// Allocation Helpers
// ==================

static void *allocFromClass(int classIndex){
    // check pressure before considering new pages
    maybeCollectOnPressure(sizeClasses[classIndex]);

    // try existing pages for this class
    for(Page *page = gc.book.classPages[classIndex]; page != NULL; page = page->nextPage){
        // if the current page is open
        if(page->freeHead != -1){
            uint32_t idx = (uint32_t)page->freeHead;

            // add to page
            page->freeHead = *slotNextPtr(page, idx);
            page->inuseCount++;
            page->inuseBits[bitByte(idx)] |= bitMask(idx);

            // add number of bytes since last GC
            gc.bytesSinceLastGC += sizeClasses[classIndex];

            return slotBase(page, idx); // exit early
        }
    }

    // reuse an empty page if available
    if(gc.book.emptyPages != NULL){
        // move page to emptypages
        Page *page = gc.book.emptyPages;
        gc.book.emptyPages = page->nextPage;
        page->nextPage = NULL;

        // reset the page for needed class
        pageResetForClass(page, classIndex);

        uint32_t idx = (uint32_t)page->freeHead;
        page->freeHead = *slotNextPtr(page, idx);
        page->inuseCount++;
        page->inuseBits[bitByte(idx)] |= bitMask(idx);

        // push front into class list
        page->nextPage = gc.book.classPages[classIndex];
        gc.book.classPages[classIndex] = page;

        // add number of bytes to since last GC
        gc.bytesSinceLastGC += sizeClasses[classIndex];

        return slotBase(page, idx); // exit early
    }

    // make a new page as last resort
    Page *page = pageInitForClass(classIndex);

    // push front into class list
    page->nextPage = gc.book.classPages[classIndex];
    gc.book.classPages[classIndex] = page;
    gc.book.numPages++;

    uint32_t idx = (uint32_t)page->freeHead;
    page->freeHead = *slotNextPtr(page, idx);
    page->inuseCount++;
    page->inuseBits[bitByte(idx)] |= bitMask(idx);

    gc.bytesSinceLastGC += sizeClasses[classIndex];

    return slotBase(page, idx); // return new page's base
}

// ==============================
// Marking For Stack Scan & Roots
// ==============================

static void markPtr(void *ptr);

static void scanStackForRoots(){
    volatile int here;  // try to flush all registers by adding a volatile to the stack

    uintptr_t low = (uintptr_t)&here;   // also doubles as a hint to where the bottom of the stack is
    uintptr_t high = (uintptr_t)gc.stack_top_hint;

    // swap them if they are backwards
    if(low > high){
        uintptr_t t = low; low = high; high = t;
    }

    // cast everything in the stack as a pointer and attempt to mark it if it is in the arena
    for(uintptr_t *w = (uintptr_t *)low; w < (uintptr_t *)high; w++){
        markPtr((void *)(*w));
    }
}

static void markFromExplicitRoots(){
    // walk the declared roots
    for(size_t r = 0; r < gc.rootsLen; r++){
        if(gc.roots == NULL) \
            break;
        if(gc.roots[r] == NULL) \
            continue;
        
        // try to mark the pointers
        void **slot = gc.roots[r];
        markPtr(*slot);
    }
}

// compute base with mask & look up in index
static Page *findPageContaining(void *p, uint32_t *outIdx){
    // if passed pointer is invalid
    if(p == NULL) \
        return NULL;

    // get page from passed address
    Page *page = pageIndexFindByAddr(p);
    if(page == NULL) \
        return NULL;    // if it's not valid page

    // get the offset in the page
    uintptr_t off = (uintptr_t)p - (uintptr_t)page->block;
    if(off >= BUFF_SIZE) \
        return NULL;    // if the offset is larger than the page size

    // get the index in the page
    uint32_t idx = (uint32_t)(off / page->sizeClass);
    if(idx >= page->nslots) \
        return NULL;    // if the index is larger than the number of slots

    // set the outIdx as the found one
    if(outIdx) \
        *outIdx = idx;

    return page;
}

static void wlPush(Page *page, uint32_t idx){
    // grow the worklist array
    if(workLen == workCap){
        // grow values
        size_t newCap = workCap ? workCap * 2 : 128;
        WorkItem *temp = realloc(worklist, newCap * sizeof(WorkItem));

        if(temp == NULL){
            perror("[FATAL]: Could not allocate worklist.");
            gcDestroy();

            exit(56);
        }

        // update values
        worklist = temp;
        workCap = newCap;
    }

    // add the workitem to the lisl
    worklist[workLen].page = page;
    worklist[workLen].idx = idx;
    workLen++;
}

static int slotMark(Page *page, uint32_t idx){
    // get markBit and bitmask for the index
    uint8_t *mb = &page->markBits[bitByte(idx)];
    uint8_t m = bitMask(idx);

    if((*mb) & m){
        return 0;   // already marked
    }
    (*mb) |= m;

    return 1;
}

static void markPtr(void *ptr){
    // get page based off of pointer
    uint32_t idx = 0;
    Page *page = findPageContaining(ptr, &idx);
    if(page == NULL)
        return;

    // only consider allocated slots
    if(!(page->inuseBits[bitByte(idx)] & bitMask(idx)))
        return;

    // mark it and add to worklist if it's not already marked
    if(slotMark(page, idx)){
        wlPush(page, idx);
    }
}

static void traceWorklist(){
    // walk the worklist
    while(workLen){
        workLen--;

        // get the page and index
        Page *page = worklist[workLen].page;
        uint32_t idx = worklist[workLen].idx;

        // scan payload as words
        uintptr_t *words = (uintptr_t *)slotBase(page, idx);
        size_t nwords = page->sizeClass / sizeof(uintptr_t);
        for(size_t i = 0; i < nwords; i++){
            markPtr((void *)words[i]);  // attempt to mark anything
        }
    }
}

// ========
// Sweeping
// ========

static void freeSlot(Page *page, uint32_t idx){
    // push slot back to freelist
    *slotNextPtr(page, idx) = page->freeHead;
    page->freeHead = (int32_t)idx;

    // clear inuse bit
    page->inuseBits[bitByte(idx)] &= (uint8_t)~bitMask(idx);
    if(page->inuseCount > 0)
        page->inuseCount--;
}

static void sweepAllPages(){
    // for every class size
    for(size_t c = 0; c < NUM_CLASSES; c++){
        Page **link = &gc.book.classPages[c];
        while(*link){   // while there are pages left
            Page *page = *link;

            // walk all slots on the page
            for(uint32_t i = 0; i < page->nslots; i++){
                uint8_t ib = page->inuseBits[bitByte(i)] & bitMask(i);
                uint8_t mb = page->markBits [bitByte(i)] & bitMask(i);

                if(ib && !mb){
                    freeSlot(page, i);
                }
                else if(mb){
                    // clear mark for next cycle
                    page->markBits[bitByte(i)] &= (uint8_t)~bitMask(i);
                }
            }

            if(page->inuseCount == 0){
                // unlink from class list
                *link = page->nextPage;

                // move to emptyPages cache
                page->nextPage = gc.book.emptyPages;
                gc.book.emptyPages = page;
            }
            else{
                link = &page->nextPage; // get next page
            }
        }
    }
}

// -=*#############*=-
//    PUBLIC THINGS
// -=*#############*=-

// =====
// Debug
// =====

void gcDebugPrintStats(){
    size_t totalPages = 0;  // active + empty
    size_t activePages = 0; // pages currently in class lists
    size_t emptyPages = 0;  // pages cached for reuse
    size_t liveBytes = 0;   // exact: sum of (inuseCount * sizeClass)

    // for every class
    for (size_t i = 0; i < NUM_CLASSES; i++){
        for (Page *p = gc.book.classPages[i]; p != NULL; p = p->nextPage){  // look at every page
            // increment pages and add size of bytes in use
            activePages++;
            liveBytes += (size_t)p->inuseCount * p->sizeClass;
        }
    }

    // for every empty page
    for (Page *p = gc.book.emptyPages; p != NULL; p = p->nextPage){
        emptyPages++;   // increment the number of empty pages
    }
    totalPages = activePages + emptyPages;

    // print the debug message
    printf("[GC DEBUG] Pages: %zu (active %zu, empty %zu)  Live bytes: %zu  lastLiveBytes: %zu\n", totalPages, activePages, emptyPages, liveBytes, gc.lastLiveBytes);
}

// =======================
// Initialize & Destroy GC
// =======================

bool gcInit(const void *stack_top_hint){
    // store address of approximately where the stack top would be
    gc.stack_top_hint = stack_top_hint;

    // start the arena
    gc.arena = arenaLocalInit();
    if(gc.arena == NULL)
        return false;
    
    // start the book
    bookInit(&gc.book);
    pageIndexInit(128); // init page index
    
    // set up roots array
    gc.roots = NULL;
    gc.rootsLen = 0;
    gc.rootsCap = 0;

    // initialize GC base autocollect data
    gc.bytesSinceLastGC = 0;
    gc.lastLiveBytes = BUFF_SIZE;   // sane baseline
    gc.growthFactor = 1.5;  // collect when new bytes ~150% of last live

    return true;
}

void gcDestroy(){
    // if the GC was initialized (based on whether the arena is valid)
    if(gc.arena){
        arenaLocalDestroy(gc.arena);
        gc.arena = NULL;
        gc.stack_top_hint = NULL;
        bookDestroy(&gc.book);
    }

    // free the roots array
    free(gc.roots);
    gc.roots = NULL;
    gc.rootsLen = 0;
    gc.rootsCap = 0;

    // free the worklist
    free(worklist);
    worklist = NULL;
    workLen = 0;
    workCap = 0;

    pageIndexFree();
}

// ==========================
// Explicit Collect & Rooting
// ==========================

void gcCollect(){
    // mark
    workLen = 0; // reset worklist (capacity kept)
    scanStackForRoots();
    markFromExplicitRoots();
    traceWorklist();

    // sweep
    sweepAllPages();

    // update pressure
    gc.lastLiveBytes = recomputeLiveBytes();
    gc.bytesSinceLastGC = 0;
}

void gcRootVariable(void **addr){
    // add root based on explicit address
    if(addr) \
        addRoot(addr);
}

void gcUnrootVariable(void **addr){
    // remove the root based on explicit address
    if(!addr || !removeRoot(addr)){
        fprintf(stderr, "Could not find variable at address %p to 'Unroot'.\n", (void*)addr);
    }
}

// =====
// Alloc
// =====

void *gcAlloc(size_t size){
    int classIndex = classForSize(size);
    if(classIndex < 0){
        // large objects are allocated from the arena directly (not GC-managed)
        maybeCollectOnPressure(size);   // still count towards pressure
        void *block = arenaLocalAlloc(gc.arena, size);

        if(block == NULL){
            gcCollect();
            block = arenaLocalAlloc(gc.arena, size);

            if(block == NULL){
                perror("[FATAL]: arena alloc for large object failed.");

                exit(70);
            }
        }
        gc.bytesSinceLastGC += size;    // add to size of managed bytes

        return block;   // exit giving pointer to the raw arena block for the large block
    }

    // allocate from helper (managed by GC)
    void *ptr = allocFromClass(classIndex);
    if(ptr == NULL){
        gcCollect();
        ptr = allocFromClass(classIndex);

        if(ptr == NULL){
            perror("[FATAL]: gcAlloc from class failed after GC.");

            exit(71);
        }
    }

    return ptr; // exit giving pointer to slot in page in arena
}
