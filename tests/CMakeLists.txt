cmake_minimum_required(VERSION 3.16)

# ─── Вспомогательная функция для добавления теста ────────────────────────────
function(pmm_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pmm)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ─── Тесты Фазы 1 ─────────────────────────────────────────────────────────────
pmm_add_test(test_allocate   test_allocate.cpp)
pmm_add_test(test_deallocate test_deallocate.cpp)

# ─── Тесты Фазы 2 ─────────────────────────────────────────────────────────────
pmm_add_test(test_coalesce    test_coalesce.cpp)

# ─── Тесты Фазы 3 ─────────────────────────────────────────────────────────────
pmm_add_test(test_persistence test_persistence.cpp)
