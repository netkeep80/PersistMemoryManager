cmake_minimum_required(VERSION 3.16)

# ─── Бенчмарки производительности () ────────────────────────────────

add_executable(pmm_benchmarks bench_allocator.cpp)
target_link_libraries(pmm_benchmarks PRIVATE pmm benchmark::benchmark benchmark::benchmark_main Threads::Threads)
