cmake_minimum_required(VERSION 3.16)

# ─── Вспомогательная функция для добавления примера ──────────────────────────
function(pmm_add_example name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pmm)
endfunction()

# ─── Примеры Фазы 1 ───────────────────────────────────────────────────────────
pmm_add_example(basic_usage      basic_usage.cpp)

# ─── Примеры Фазы 3 ───────────────────────────────────────────────────────────
pmm_add_example(persistence_demo persistence_demo.cpp)

# ─── Примеры Фазы 4 ───────────────────────────────────────────────────────────
pmm_add_example(stress_test      stress_test.cpp)

# ─── Примеры Фазы 6 ───────────────────────────────────────────────────────────
pmm_add_example(benchmark        benchmark.cpp)
