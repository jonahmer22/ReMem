# Changelog

All notable changes to this project will be documented in this file.

This project adheres to [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and follows [Semantic Versioning](https://semver.org/) (0.x indicates initial development).

## [v0.2.0] – 2025-08-18
### Changed
- Switched the arena allocator's foundation from `calloc` to `aligned_alloc`, eliminating the need for manual pointer alignment.
### Added
- Introduced a new mode toggle to control memory freeing:
  - Option to allow the garbage collector (GC) to free memory incrementally during runtime.
  - Option to defer freeing all allocated memory until program exit (end-of-life cleanup).

## [Unreleased]
- Nursery: Add in Nursery support alongside current functionality. There should be a 1.5-5x speedup from implementing and using this (this is an estimate though).
- Multithreading: Allow for Multithreaded applications (long term goal).

---