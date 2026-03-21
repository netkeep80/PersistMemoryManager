cmake_minimum_required(VERSION 3.16)

# ─── Вспомогательная функция для добавления теста (Catch2, Phase 5.1) ─────────
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

# ─── Реалистичный стресс-тест (Issue #20) ────────────────────────────────────
pmm_add_test(test_stress_realistic test_stress_realistic.cpp)
set_tests_properties(test_stress_realistic PROPERTIES TIMEOUT 120)

# ─── Стресс-тест автоматического роста памяти (Issue #30) ────────────────────
pmm_add_test(test_stress_auto_grow test_stress_auto_grow.cpp)

# ─── Тесты Фазы 9: Потокобезопасность ────────────────────────────────────────
add_executable(test_thread_safety test_thread_safety.cpp)
target_link_libraries(test_thread_safety PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_thread_safety COMMAND test_thread_safety)

# ─── Тесты Фазы 10: Разделённые блокировки (shared_mutex) ─────────────────────
add_executable(test_shared_mutex test_shared_mutex.cpp)
target_link_libraries(test_shared_mutex PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_shared_mutex COMMAND test_shared_mutex)

# ─── Нагрузочные и стресс-тесты (Issue #34) ──────────────────────────────────
pmm_add_test(test_scenarios_issue34 test_scenarios_issue34.cpp)

# ─── Тесты нового алгоритма (Issue #55): AVL-дерево свободных блоков ──────────
pmm_add_test(test_avl_allocator test_avl_allocator.cpp)

# ─── Тесты reallocate_typed (Issue #67) ───────────────────────────────────────
pmm_add_test(test_reallocate test_reallocate.cpp)

# ─── Тесты модернизации блока без magic (Issue #69) ──────────────────────────
pmm_add_test(test_block_modernization test_block_modernization.cpp)

# ─── Тесты архитектурного рефакторинга (Issue #73) ───────────────────────────
pmm_add_test(test_issue73_refactoring test_issue73_refactoring.cpp)

# ─── Тесты оптимизации констант (Issue #83) ──────────────────────────────────
pmm_add_test(test_issue83_constants test_issue83_constants.cpp)

# ─── Тесты code review и маяки абстракций (Issue #87) ────────────────────────
pmm_add_test(test_issue87_abstraction test_issue87_abstraction.cpp)

# ─── Тесты Phase 1: AddressTraits (Issue #87) ────────────────────────────────
pmm_add_test(test_issue87_phase1 test_issue87_phase1.cpp)

# ─── Тесты Phase 2: LinkedListNode + TreeNode (Issue #87) ────────────────────
pmm_add_test(test_issue87_phase2 test_issue87_phase2.cpp)

# ─── Тесты Phase 3: Block<AddressTraits> (Issue #87) ─────────────────────────
pmm_add_test(test_issue87_phase3 test_issue87_phase3.cpp)

# ─── Тесты Phase 4: FreeBlockTree policy (Issue #87) ─────────────────────────
pmm_add_test(test_issue87_phase4 test_issue87_phase4.cpp)

# ─── Тесты Phase 5: StorageBackend (Issue #87) ────────────────────────────────
pmm_add_test(test_issue87_phase5 test_issue87_phase5.cpp)

# ─── Тесты Phase 6: AllocatorPolicy (Issue #87) ──────────────────────────────
pmm_add_test(test_issue87_phase6 test_issue87_phase6.cpp)

# ─── Тесты Phase 7: AbstractPersistMemoryManager (Issue #87) ─────────────────
pmm_add_test(test_issue87_phase7 test_issue87_phase7.cpp)

# ─── Тесты Phase 8: pmm_presets.h (Issue #87) ────────────────────────────────
pmm_add_test(test_issue87_phase8 test_issue87_phase8.cpp)

# ─── Тесты Phase 9: BlockState machine (Issue #93) ───────────────────────────
pmm_add_test(test_block_state test_block_state.cpp)

# ─── Интеграционные тесты пресетов (Issue #97) ───────────────────────────────
add_executable(test_issue97_presets test_issue97_presets.cpp)
target_link_libraries(test_issue97_presets PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue97_presets COMMAND test_issue97_presets)

# ─── Тесты Phase 1: Infrastructure Preparation (Issue #100) ──────────────────
add_executable(test_issue100 test_issue100.cpp)
target_link_libraries(test_issue100 PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue100 COMMAND test_issue100)

# ─── Интеграционные тесты BlockState machine (Issue #106) ─────────────────────
pmm_add_test(test_issue106_block_state_integration test_issue106_block_state_integration.cpp)

# ─── Тесты статической модели менеджера (Issue #108) ─────────────────────────
pmm_add_test(test_issue108_static_model test_issue108_static_model.cpp)

# ─── Тесты новых пресетов: EmbeddedHeap, IndustrialDBHeap (Issue #123) ────────
pmm_add_test(test_issue123_presets test_issue123_presets.cpp)

# ─── Тесты улучшений по code review (Issue #144) ─────────────────────────────
pmm_add_test(test_issue144_code_review test_issue144_code_review.cpp)

# ─── Тесты исправлений по code review (Issue #172) ────────────────────────────
pmm_add_test(test_issue172_code_review test_issue172_code_review.cpp)

# ─── Тесты удаления устаревших полей ManagerHeader (Issue #176) ───────────────
pmm_add_test(test_issue176_manager_header test_issue176_manager_header.cpp)

# ─── Тесты переосмысления конфигураций (Issue #146) ──────────────────────────
pmm_add_test(test_issue146_configs test_issue146_configs.cpp)

# ─── Тесты поддержки 16-bit и 64-bit индексов (Issue #146) ───────────────────
add_executable(test_issue146_index_sizes test_issue146_index_sizes.cpp)
target_link_libraries(test_issue146_index_sizes PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue146_index_sizes COMMAND test_issue146_index_sizes)

# ─── Тесты pstringview — интернированных read-only строк (Issue #151) ─────────
pmm_add_test(test_issue151_pstringview test_issue151_pstringview.cpp)

# ─── Тесты pmap — персистентного словаря на AVL-дереве (Issue #153) ──────────
pmm_add_test(test_issue153_pmap test_issue153_pmap.cpp)

# ─── Тесты pvector — персистентного вектора (Issue #186) ──────────────────────
pmm_add_test(test_issue186_pvector test_issue186_pvector.cpp)

# ─── Тесты дедупликации функциональности (Issue #160) ─────────────────────────
pmm_add_test(test_issue160_deduplication test_issue160_deduplication.cpp)

# ─── Тесты дедупликации AVL-поиска (Issue #162) ────────────────────────────────
pmm_add_test(test_issue162_deduplication test_issue162_deduplication.cpp)

# ─── Тесты дедупликации функциональности (Issue #166) ─────────────────────────
pmm_add_test(test_issue166_deduplication test_issue166_deduplication.cpp)

# ─── Тесты дедупликации функций-обёрток (Issue #168) ─────────────────────────
pmm_add_test(test_issue168_deduplication test_issue168_deduplication.cpp)

# ─── Тесты дедупликации blk_raw-вычислений (Issue #179) ─────────────────────
pmm_add_test(test_issue179_deduplication test_issue179_deduplication.cpp)

# ─── Тесты дедупликации AVL-операций pvector через avl_tree_mixin (Issue #188) ─
pmm_add_test(test_issue188_deduplication test_issue188_deduplication.cpp)

# ─── Тесты мутабельной персистентной строки pstring (Issue #45, Phase 3.1) ───
pmm_add_test(test_issue45_pstring test_issue45_pstring.cpp)

# ─── Тесты персистентного массива parray (Issue #195, Phase 3.2) ─────────────
pmm_add_test(test_issue195_parray test_issue195_parray.cpp)

# ─── Тесты pmap erase/size/iterator/clear (Issue #196, Phase 3.3) ──────────
pmm_add_test(test_issue196_pmap_erase test_issue196_pmap_erase.cpp)

# ─── Тесты pvector erase(index) (Issue #197, Phase 3.4) ─────────────────────
pmm_add_test(test_issue197_pvector_erase test_issue197_pvector_erase.cpp)

# ─── Тесты STL-совместимого аллокатора pallocator (Issue #198, Phase 3.5) ───
pmm_add_test(test_issue198_pallocator test_issue198_pallocator.cpp)

# ─── Тесты персистентного пула объектов ppool (Issue #199, Phase 3.6) ──────
pmm_add_test(test_issue199_ppool test_issue199_ppool.cpp)

# ─── Тесты корневого объекта в ManagerHeader (Issue #200, Phase 3.7) ───────
pmm_add_test(test_issue200_root_object test_issue200_root_object.cpp)

# ─── Тесты кодов ошибок PmmError (Issue #201, Phase 4.1) ─────────────────
pmm_add_test(test_issue201_error_codes test_issue201_error_codes.cpp)

# ─── Тесты хуков логирования (Issue #202, Phase 4.2) ─────────────────────
pmm_add_test(test_issue202_logging_hooks test_issue202_logging_hooks.cpp)

# ─── Тесты нативного перераспределения reallocate_typed (Issue #210, Phase 4.3) ─
pmm_add_test(test_issue210_reallocate_typed test_issue210_reallocate_typed.cpp)

# ─── Тесты конверсии pptr ↔ байтовые смещения (Issue #211, Phase 4.4) ─────────
pmm_add_test(test_issue211_byte_offset test_issue211_byte_offset.cpp)

# ─── Тесты безопасности и устойчивости Phase 1 (Issue #43) ──────────────────
pmm_add_test(test_issue43_phase1_safety test_issue43_phase1_safety.cpp)

# ─── Тесты персистентности и надёжности Phase 2 (Issue #43) ─────────────────
pmm_add_test(test_issue43_phase2_persistence test_issue43_phase2_persistence.cpp)

# ─── Тесты поддержки 64-bit индексов через ManagerHeader<AT> (Issue #175) ────
add_executable(test_issue175_64bit_indexes test_issue175_64bit_indexes.cpp)
target_link_libraries(test_issue175_64bit_indexes PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue175_64bit_indexes COMMAND test_issue175_64bit_indexes)

# ─── Тесты самодостаточности single-header файлов (Issue #123) ───────────────
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

# ─── Тест самодостаточности single-header файла для EmbeddedStaticHeap (Issue #146) ─
set(target test_issue146_sh_embedded_static)
add_executable(${target} test_issue146_sh_embedded_static.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тест самодостаточности single-header файла для SmallEmbeddedStaticHeap (Issue #146) ─
set(target test_issue146_sh_small_embedded_static)
add_executable(${target} test_issue146_sh_small_embedded_static.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тест самодостаточности single-header файла для LargeDBHeap (Issue #146) ─
set(target test_issue146_sh_large_db)
add_executable(${target} test_issue146_sh_large_db.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тест самодостаточности pmm_no_comments.h — версии без комментариев (Issue #170) ─
set(target test_issue170_sh_no_comments)
add_executable(${target} test_issue170_sh_no_comments.cpp)
target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/single_include ${CMAKE_SOURCE_DIR}/single_include/pmm)
target_compile_features(${target} PRIVATE cxx_std_20)
target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME ${target} COMMAND ${target})

# ─── Тесты parray::insert/erase (Issue #233) ──────────────────────────────────
pmm_add_test(test_issue233_parray_insert_erase test_issue233_parray_insert_erase.cpp)

# ─── Тесты расширенного покрытия (Issue #213, Phase 5.2) ─────────────────────

# 5.2.1: Overflow tests for size calculations
pmm_add_test(test_issue213_overflow test_issue213_overflow.cpp)

# 5.2.2: Concurrent allocation/deallocation tests
add_executable(test_issue213_concurrent test_issue213_concurrent.cpp)
target_link_libraries(test_issue213_concurrent PRIVATE pmm Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_issue213_concurrent COMMAND test_issue213_concurrent)
set_tests_properties(test_issue213_concurrent PROPERTIES TIMEOUT 120)

# Issue #235: code review improvements (typed_guard, HeapStorage, deprecated attrs)
pmm_add_test(test_issue235_code_review test_issue235_code_review.cpp)

# 5.2.3: Fuzz-style testing of allocator (deterministic, Catch2)
pmm_add_test(test_issue213_fuzz test_issue213_fuzz.cpp)
set_tests_properties(test_issue213_fuzz PROPERTIES TIMEOUT 120)

# 5.2.3: libFuzzer harness (built only when fuzzer is available, not part of CTest)
# Build manually: clang++ -std=c++20 -fsanitize=fuzzer,address -O2 \
#   -I include -I single_include -I single_include/pmm \
#   tests/fuzz_allocator.cpp -o fuzz_allocator
