---
bump: minor
---

### Added
- Extended test coverage for allocator (Issue #213, Phase 5.2):
  - 19 overflow test cases covering `size_t`/`index_type` overflow, granule boundaries, exhaustion/recovery across all AddressTraits variants (16/32/64-bit)
  - 6 concurrent test cases: varying block sizes, LIFO/random deallocation, high contention (16 threads), data integrity, concurrent `reallocate_typed`
  - 6 deterministic fuzz test cases with PRNG-driven alloc/dealloc/reallocate on static, heap, and 16-bit storage
  - libFuzzer harness (`tests/fuzz_allocator.cpp`) for coverage-guided fuzzing with Clang
- `docs/phase5_testing.md` updated with Phase 5.2 documentation
