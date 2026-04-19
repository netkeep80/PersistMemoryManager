cmake_minimum_required(VERSION 3.16)

add_test(
    NAME test_build_testing_off_excludes_tests
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}/build-testing-off-check
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_testing_off_excludes_tests.cmake
)

# ─── Вспомогательная функция для добавления теста (Catch2) ─────────
function(pmm_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pmm Catch2::Catch2WithMain)
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

# ─── Реалистичный стресс-тест ────────────────────────────────────
pmm_add_test(test_stress_realistic test_stress_realistic.cpp)
set_tests_properties(test_stress_realistic PROPERTIES TIMEOUT 120)

# ─── Стресс-тест автоматического роста памяти ────────────────────
pmm_add_test(test_stress_auto_grow test_stress_auto_grow.cpp)

# ─── Тесты Фазы 9: Потокобезопасность ────────────────────────────────────────
add_executable(test_thread_safety test_thread_safety.cpp)
target_link_libraries(test_thread_safety PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_thread_safety COMMAND test_thread_safety)

# ─── Тесты Фазы 10: Разделённые блокировки (shared_mutex) ─────────────────────
add_executable(test_shared_mutex test_shared_mutex.cpp)
target_link_libraries(test_shared_mutex PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_shared_mutex COMMAND test_shared_mutex)

# ─── Нагрузочные и стресс-тесты ──────────────────────────────────
pmm_add_test(test_scenarios_issue34 test_scenarios_issue34.cpp)

# ─── Тесты нового алгоритма: AVL-дерево свободных блоков ──────────
pmm_add_test(test_avl_allocator test_avl_allocator.cpp)

# ─── Тесты reallocate_typed ───────────────────────────────────────
pmm_add_test(test_reallocate test_reallocate.cpp)

# ─── Тесты модернизации блока без magic ──────────────────────────
pmm_add_test(test_block_modernization test_block_modernization.cpp)

# ─── Тесты архитектурного рефакторинга ───────────────────────────
pmm_add_test(test_issue73_refactoring test_issue73_refactoring.cpp)

# ─── Тесты оптимизации констант ──────────────────────────────────
pmm_add_test(test_issue83_constants test_issue83_constants.cpp)

# ─── Тесты code review и маяки абстракций ────────────────────────
pmm_add_test(test_issue87_abstraction test_issue87_abstraction.cpp)

# ─── Тесты Phase 1: AddressTraits ────────────────────────────────
pmm_add_test(test_issue87_phase1 test_issue87_phase1.cpp)

# ─── Тесты Phase 2: LinkedListNode + TreeNode ────────────────────
pmm_add_test(test_issue87_phase2 test_issue87_phase2.cpp)

# ─── Тесты Phase 3: Block<AddressTraits> ─────────────────────────
pmm_add_test(test_issue87_phase3 test_issue87_phase3.cpp)

# ─── Тесты Phase 4: FreeBlockTree policy ─────────────────────────
pmm_add_test(test_issue87_phase4 test_issue87_phase4.cpp)

# ─── Тесты Phase 5: StorageBackend ────────────────────────────────
pmm_add_test(test_issue87_phase5 test_issue87_phase5.cpp)

# ─── Тесты Phase 6: AllocatorPolicy ──────────────────────────────
pmm_add_test(test_issue87_phase6 test_issue87_phase6.cpp)

# ─── Тесты Phase 7: AbstractPersistMemoryManager ─────────────────
pmm_add_test(test_issue87_phase7 test_issue87_phase7.cpp)

# ─── Тесты Phase 8: pmm_presets.h ────────────────────────────────
pmm_add_test(test_issue87_phase8 test_issue87_phase8.cpp)

# ─── Тесты Phase 9: BlockState machine ───────────────────────────
pmm_add_test(test_block_state test_block_state.cpp)

# ─── Интеграционные тесты пресетов ───────────────────────────────
add_executable(test_issue97_presets test_issue97_presets.cpp)
target_link_libraries(test_issue97_presets PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue97_presets COMMAND test_issue97_presets)

# ─── Тесты Phase 1: Infrastructure Preparation ──────────────────
add_executable(test_issue100 test_issue100.cpp)
target_link_libraries(test_issue100 PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue100 COMMAND test_issue100)

# ─── Интеграционные тесты BlockState machine ─────────────────────
pmm_add_test(test_issue106_block_state_integration test_issue106_block_state_integration.cpp)

# ─── Тесты статической модели менеджера ─────────────────────────
pmm_add_test(test_issue108_static_model test_issue108_static_model.cpp)

# ─── Тесты новых пресетов: EmbeddedHeap, IndustrialDBHeap ────────
pmm_add_test(test_issue123_presets test_issue123_presets.cpp)

# ─── Тесты улучшений по code review ─────────────────────────────
pmm_add_test(test_issue144_code_review test_issue144_code_review.cpp)

# ─── Тесты исправлений по code review ────────────────────────────
pmm_add_test(test_issue172_code_review test_issue172_code_review.cpp)

# ─── Тесты удаления устаревших полей ManagerHeader ───────────────
pmm_add_test(test_issue176_manager_header test_issue176_manager_header.cpp)

# ─── Тесты переосмысления конфигураций ──────────────────────────
pmm_add_test(test_issue146_configs test_issue146_configs.cpp)

# ─── Тесты поддержки 16-bit и 64-bit индексов ───────────────────
add_executable(test_issue146_index_sizes test_issue146_index_sizes.cpp)
target_link_libraries(test_issue146_index_sizes PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue146_index_sizes COMMAND test_issue146_index_sizes)

# ─── Тесты pstringview — интернированных read-only строк ─────────
pmm_add_test(test_issue151_pstringview test_issue151_pstringview.cpp)

# ─── Тесты pmap — персистентного словаря на AVL-дереве ──────────
pmm_add_test(test_issue153_pmap test_issue153_pmap.cpp)

# ─── Тесты дедупликации функциональности ─────────────────────────
pmm_add_test(test_issue160_deduplication test_issue160_deduplication.cpp)

# ─── Тесты дедупликации AVL-поиска ────────────────────────────────
pmm_add_test(test_issue162_deduplication test_issue162_deduplication.cpp)

# ─── Тесты дедупликации функциональности ─────────────────────────
pmm_add_test(test_issue166_deduplication test_issue166_deduplication.cpp)

# ─── Тесты дедупликации функций-обёрток ─────────────────────────
pmm_add_test(test_issue168_deduplication test_issue168_deduplication.cpp)

# ─── Тесты дедупликации blk_raw-вычислений ─────────────────────
pmm_add_test(test_issue179_deduplication test_issue179_deduplication.cpp)

# ─── Тесты дедупликации AVL-операций через avl_tree_mixin ────────
pmm_add_test(test_issue188_deduplication test_issue188_deduplication.cpp)

# ─── Тесты мутабельной персистентной строки pstring ───
pmm_add_test(test_issue45_pstring test_issue45_pstring.cpp)

# ─── Тесты персистентного массива parray ─────────────
pmm_add_test(test_issue195_parray test_issue195_parray.cpp)

# ─── Тесты pmap erase/size/iterator/clear ──────────
pmm_add_test(test_issue196_pmap_erase test_issue196_pmap_erase.cpp)

# ─── Тесты STL-совместимого аллокатора pallocator ───
pmm_add_test(test_issue198_pallocator test_issue198_pallocator.cpp)

# ─── Тесты персистентного пула объектов ppool ──────
pmm_add_test(test_issue199_ppool test_issue199_ppool.cpp)

# ─── Тесты корневого объекта в ManagerHeader ───────
pmm_add_test(test_issue200_root_object test_issue200_root_object.cpp)

# ─── Тесты кодов ошибок PmmError ─────────────────
pmm_add_test(test_issue201_error_codes test_issue201_error_codes.cpp)

# ─── Тесты хуков логирования ─────────────────────
pmm_add_test(test_issue202_logging_hooks test_issue202_logging_hooks.cpp)

# ─── Тесты нативного перераспределения reallocate_typed ─
pmm_add_test(test_issue210_reallocate_typed test_issue210_reallocate_typed.cpp)

# ─── Тесты конверсии pptr ↔ байтовые смещения ─────────
pmm_add_test(test_issue211_byte_offset test_issue211_byte_offset.cpp)

# ─── Тесты безопасности и устойчивости Phase 1 ──────────────────
pmm_add_test(test_issue43_phase1_safety test_issue43_phase1_safety.cpp)

# ─── Тесты персистентности и надёжности Phase 2 ─────────────────
pmm_add_test(test_issue43_phase2_persistence test_issue43_phase2_persistence.cpp)

# ─── Тесты поддержки 64-bit индексов через ManagerHeader<AT> ────
add_executable(test_issue175_64bit_indexes test_issue175_64bit_indexes.cpp)
target_link_libraries(test_issue175_64bit_indexes PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue175_64bit_indexes COMMAND test_issue175_64bit_indexes)

# ─── Тесты самодостаточности single-header файлов ───────────────
# Каждый single-header файл тестируется отдельным исполняемым файлом.
# Это гарантирует автономность: каждый файл компилируется без include/pmm/*.
foreach(preset single_threaded multi_threaded embedded industrial_db)
    set(target test_issue123_sh_${preset})
    add_executable(${target} test_issue123_sh_${preset}.cpp)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
    add_test(NAME ${target} COMMAND ${target})
endforeach()

# ─── Тест самодостаточности single-header файла для EmbeddedStaticHeap ─
set(target test_issue146_sh_embedded_static)
add_executable(${target} test_issue146_sh_embedded_static.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тест самодостаточности single-header файла для SmallEmbeddedStaticHeap ─
set(target test_issue146_sh_small_embedded_static)
add_executable(${target} test_issue146_sh_small_embedded_static.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тест самодостаточности single-header файла для LargeDBHeap ─
set(target test_issue146_sh_large_db)
add_executable(${target} test_issue146_sh_large_db.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тест самодостаточности pmm_no_comments.h — версии без комментариев ─
set(target test_issue170_sh_no_comments)
add_executable(${target} test_issue170_sh_no_comments.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тесты parray::insert/erase ──────────────────────────────────
pmm_add_test(test_issue233_parray_insert_erase test_issue233_parray_insert_erase.cpp)

# ─── Тесты расширенного покрытия ─────────────────────

# 5.2.1: Overflow tests for size calculations
pmm_add_test(test_issue213_overflow test_issue213_overflow.cpp)

# 5.2.2: Concurrent allocation/deallocation tests
add_executable(test_issue213_concurrent test_issue213_concurrent.cpp)
target_link_libraries(test_issue213_concurrent PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue213_concurrent COMMAND test_issue213_concurrent)
set_tests_properties(test_issue213_concurrent PROPERTIES TIMEOUT 120)

pmm_add_test(test_issue235_code_review test_issue235_code_review.cpp)

# 5.2.3: Fuzz-style testing of allocator (deterministic, Catch2)
pmm_add_test(test_issue213_fuzz test_issue213_fuzz.cpp)
set_tests_properties(test_issue213_fuzz PROPERTIES TIMEOUT 120)

# 5.2.3: libFuzzer harness (built only when fuzzer is available, not part of CTest)
# Build manually: clang++ -std=c++20 -fsanitize=fuzzer,address -O2 \
#   -I include -I single_include -I single_include/pmm \
#   tests/fuzz_allocator.cpp -o fuzz_allocator

# Persistent forest/domain registry coverage
pmm_add_test(test_forest_registry test_forest_registry.cpp)

# ─── Bootstrap invariant verification ────────────────────────────
pmm_add_test(test_issue241_bootstrap test_issue241_bootstrap.cpp)

# ─── Free-tree forest-policy regression tests ───────────────────
pmm_add_test(test_issue243_free_tree_policy test_issue243_free_tree_policy.cpp)

# ─── Verify/repair mode separation tests ────────────────────────
pmm_add_test(test_issue245_verify_repair test_issue245_verify_repair.cpp)

# ─── Verify/repair/load operational contract tests ──────────────
pmm_add_test(test_issue256_verify_repair_contract test_issue256_verify_repair_contract.cpp)

# ─── Pointer and block validation layer tests ───────────────────
pmm_add_test(test_issue257_validation test_issue257_validation.cpp)

# ─── Issue 258: Test matrix — bootstrap, reload, structural, corruption,
#     verify/repair behavior, and property/generative tests ──────
pmm_add_test(test_issue258_bootstrap      test_issue258_bootstrap.cpp)
pmm_add_test(test_issue258_reload         test_issue258_reload.cpp)
pmm_add_test(test_issue258_structural     test_issue258_structural.cpp)
pmm_add_test(test_issue258_corruption     test_issue258_corruption.cpp)
pmm_add_test(test_issue258_verify_behavior test_issue258_verify_behavior.cpp)
pmm_add_test(test_issue258_property       test_issue258_property.cpp)

# ─── Issue 295: layout include-shard repayment guard ────────────
pmm_add_test(test_issue295_layout_compaction test_issue295_layout_compaction.cpp)
target_compile_definitions(test_issue295_layout_compaction PRIVATE PMM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
