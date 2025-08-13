# ReMem - High-Performance Memory Recycler for C

ReMem is a lightweight, high performance memory recycler for C.  
It reuses freed memory through size classed arenas and O(1) page indexing, reducing allocation overhead and fragmentation for predictable, GC free performance.

## Features
- Size classed allocation for fast lookups
- Arena-backed pages to minimize fragmentation
- O(1) page indexing for instant block-to-page resolution
- Low overhead and predictable performance

## Example Usage
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
## License
[GPL v3.0](LICENSE)