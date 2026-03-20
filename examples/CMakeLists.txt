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
pmm_add_example(pmm_example_benchmark benchmark.cpp)

# ─── Пример нового API (Issue #97) ────────────────────────────────────────────
add_executable(new_api_usage new_api_usage.cpp)
target_link_libraries(new_api_usage PRIVATE pmm)
