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

# ─── Тесты Фазы 5 ─────────────────────────────────────────────────────────────
pmm_add_test(test_pptr test_pptr.cpp)

# ─── Тесты Фазы 6 ─────────────────────────────────────────────────────────────
pmm_add_test(test_performance test_performance.cpp)

# ─── Реалистичный стресс-тест (Issue #20) ────────────────────────────────────
pmm_add_test(test_stress_realistic test_stress_realistic.cpp)

# ─── Стресс-тест автоматического роста памяти (Issue #30) ────────────────────
pmm_add_test(test_stress_auto_grow test_stress_auto_grow.cpp)

# ─── Тесты Фазы 9: Потокобезопасность ────────────────────────────────────────
add_executable(test_thread_safety test_thread_safety.cpp)
target_link_libraries(test_thread_safety PRIVATE pmm Threads::Threads)
add_test(NAME test_thread_safety COMMAND test_thread_safety)

# ─── Тесты Фазы 10: Разделённые блокировки (shared_mutex) ─────────────────────
add_executable(test_shared_mutex test_shared_mutex.cpp)
target_link_libraries(test_shared_mutex PRIVATE pmm Threads::Threads)
add_test(NAME test_shared_mutex COMMAND test_shared_mutex)
