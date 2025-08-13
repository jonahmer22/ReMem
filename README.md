# ReMem - High-Performance Memory Recycler for C

ReMem is a lightweight, high performance memory recycler for C.  
It reuses freed memory through size classed arenas and O(1) page indexing, reducing allocation overhead and fragmentation for predictable, GC free performance.

## Features
- Size classed allocation for fast lookups
- Arena-backed pages to minimize fragmentation
- O(1) page indexing for instant block-to-page resolution
- Low overhead and predictable performance

## License
[GPL v3.0](LICENSE)