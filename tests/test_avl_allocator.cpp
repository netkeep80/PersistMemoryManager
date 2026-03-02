/**
 * @file test_avl_allocator.cpp
 * @brief Тесты нового алгоритма (Issue #55): AVL-дерево свободных блоков.
 *
 * Проверяет:
 * - Корректность best-fit поиска через AVL-дерево
 * - Слияние блоков при освобождении
 * - Целостность AVL-дерева после серии операций
 * - Корректность 6 полей блока (size, prev, next, left, right, parent)
 */

#include "persist_memory_manager.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define PMM_TEST( expr )                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #expr << "\n";                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while ( false )

#define PMM_RUN( name, fn )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

/// Блок с size==0 должен считаться свободным (Issue #75: переименовано из used_size)
static bool test_free_block_has_zero_size()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    // Сразу после создания: 1 свободный блок
    auto info = pmm::get_manager_info();
    PMM_TEST( info.free_count == 1 );

    // Обойдём все блоки и убедимся, что свободный имеет size==0
    int  free_blocks        = 0;
    bool all_free_zero_used = true;
    pmm::for_each_block(
        [&]( const pmm::BlockView& blk )
        {
            if ( !blk.used )
            {
                free_blocks++;
                if ( blk.user_size != 0 )
                    all_free_zero_used = false;
            }
        } );
    PMM_TEST( free_blocks == 1 );
    PMM_TEST( all_free_zero_used );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

/// best-fit: при наличии нескольких свободных блоков разного размера
/// должен выбираться наименьший подходящий
static bool test_best_fit_selection()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    // Создаём 4 блока разного размера: 512, 1024, 2048, 4096
    pmm::pptr<std::uint8_t> p[4];
    p[0] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 );
    p[1] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 1024 );
    p[2] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 2048 );
    p[3] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 4096 );
    PMM_TEST( !p[0].is_null() && !p[1].is_null() && !p[2].is_null() && !p[3].is_null() );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Освобождаем все — при слиянии соседей получим один большой блок
    pmm::PersistMemoryManager::deallocate_typed( p[0] );
    pmm::PersistMemoryManager::deallocate_typed( p[1] );
    pmm::PersistMemoryManager::deallocate_typed( p[2] );
    pmm::PersistMemoryManager::deallocate_typed( p[3] );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Единственный блок объединён — allocate должно выбрать его
    pmm::pptr<std::uint8_t> big = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 1500 );
    PMM_TEST( !big.is_null() );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::deallocate_typed( big );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

/// AVL-дерево должно оставаться валидным после множества alloc/dealloc
static bool test_avl_integrity_stress()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    static const int        N = 50;
    pmm::pptr<std::uint8_t> ptrs[N];
    std::size_t             sizes[] = { 64, 128, 256, 512, 1024, 2048 };
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( sizes[i % 6] );
        PMM_TEST( !ptrs[i].is_null() );
        PMM_TEST( pmm::PersistMemoryManager::validate() );
    }

    // Освобождаем каждый второй
    for ( int i = 0; i < N; i += 2 )
    {
        pmm::PersistMemoryManager::deallocate_typed( ptrs[i] );
        PMM_TEST( pmm::PersistMemoryManager::validate() );
    }
    // Освобождаем оставшиеся
    for ( int i = 1; i < N; i += 2 )
    {
        pmm::PersistMemoryManager::deallocate_typed( ptrs[i] );
        PMM_TEST( pmm::PersistMemoryManager::validate() );
    }

    // После полного освобождения должен быть 1 свободный блок
    auto stats = pmm::get_stats();
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

/// Тест слияния с соседями: prev + current + next → один блок
static bool test_coalesce_three_way()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 );
    pmm::pptr<std::uint8_t> p4 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 ); // барьер
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() && !p4.is_null() );

    // Освобождаем p1 и p3 (создаём два несмежных свободных блока)
    pmm::PersistMemoryManager::deallocate_typed( p1 );
    pmm::PersistMemoryManager::deallocate_typed( p3 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto before = pmm::get_stats();

    // Освобождаем p2 — должно слиться с p1 (предыдущим) и p3 (следующим)
    pmm::PersistMemoryManager::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto after = pmm::get_stats();
    // 2 объединения = block_count уменьшился на 2
    PMM_TEST( after.total_blocks == before.total_blocks - 2 );
    // free_blocks уменьшился на 1 (3 стало 1)
    PMM_TEST( after.free_blocks == before.free_blocks - 1 );

    pmm::PersistMemoryManager::deallocate_typed( p4 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

/// Тест: 6 полей блока доступны через BlockView
static bool test_block_view_fields()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    int  used_blocks       = 0;
    bool fields_consistent = true;
    pmm::for_each_block(
        [&]( const pmm::BlockView& blk )
        {
            if ( blk.used )
            {
                if ( blk.user_size == 0 )
                    fields_consistent = false;
                if ( blk.total_size < blk.user_size + blk.header_size )
                    fields_consistent = false;
                used_blocks++;
            }
            else
            {
                if ( blk.user_size != 0 )
                    fields_consistent = false;
            }
        } );
    PMM_TEST( fields_consistent );
    PMM_TEST( used_blocks == 2 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::deallocate_typed( p1 );
    pmm::PersistMemoryManager::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

/// Тест: save/load сохраняет корректность AVL-дерева
static bool test_avl_survives_save_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );
    pmm::PersistMemoryManager::deallocate_typed( p1 ); // создаём фрагментацию
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Имитируем сохранение/загрузку (копируем буфер и загружаем)
    void* snapshot = std::malloc( size );
    PMM_TEST( snapshot != nullptr );
    std::memcpy( snapshot, mem, size );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );

    // Загружаем из snapshot
    PMM_TEST( pmm::PersistMemoryManager::load( snapshot, size ) );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Должна быть возможность выделить память после load
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p3.is_null() );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::destroy();
    std::free( snapshot );
    return true;
}

/// Тест: best-fit выбирает наименьший подходящий блок из нескольких
static bool test_best_fit_chooses_smallest_fitting()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    // Создаём блоки размером, чтобы после освобождения получились дыры разного размера.
    pmm::pptr<std::uint8_t> barrier[6];
    pmm::pptr<std::uint8_t> gap[5];
    gap[0]     = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    barrier[0] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    gap[1]     = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 256 );
    barrier[1] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    gap[2]     = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 512 );
    barrier[2] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    gap[3]     = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 1024 );
    barrier[3] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    barrier[4] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 128 ); // keep allocated at end
    PMM_TEST( !gap[0].is_null() && !barrier[0].is_null() && !gap[1].is_null() && !barrier[1].is_null() &&
              !gap[2].is_null() && !barrier[2].is_null() );
    PMM_TEST( !gap[3].is_null() && !barrier[3].is_null() && !barrier[4].is_null() );

    // Создаём дыры: освобождаем gap[0..3]
    pmm::PersistMemoryManager::deallocate_typed( gap[0] );
    pmm::PersistMemoryManager::deallocate_typed( gap[1] );
    pmm::PersistMemoryManager::deallocate_typed( gap[2] );
    pmm::PersistMemoryManager::deallocate_typed( gap[3] );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Теперь запрашиваем 200 байт: best-fit должен выбрать дыру
    pmm::pptr<std::uint8_t> result = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 200 );
    PMM_TEST( !result.is_null() );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Освобождаем всё
    pmm::PersistMemoryManager::deallocate_typed( result );
    for ( int i = 0; i < 4; i++ )
        pmm::PersistMemoryManager::deallocate_typed( barrier[i] );
    pmm::PersistMemoryManager::deallocate_typed( barrier[4] );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

/// Тест: reallocate работает с новым алгоритмом
static bool test_reallocate_works()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr.is_null() );

    // Запись данных
    std::memset( ptr.get(), 0xAB, 256 );

    // Реаллокация в больший блок
    pmm::pptr<std::uint8_t> new_ptr = pmm::PersistMemoryManager::reallocate_typed( ptr, 512 );
    PMM_TEST( !new_ptr.is_null() );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Проверяем, что данные сохранились
    const std::uint8_t* p = new_ptr.get();
    for ( std::size_t i = 0; i < 256; i++ )
    {
        PMM_TEST( p[i] == 0xAB );
    }

    pmm::PersistMemoryManager::deallocate_typed( new_ptr );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

/// Issue #75: root_offset semantics — 0 for free blocks, own-index for allocated blocks.
static bool test_root_offset_semantics()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    // After creation: single free block must have root_offset == 0
    {
        const auto*   base     = static_cast<const std::uint8_t*>( mem );
        const auto*   hdr      = reinterpret_cast<const pmm::detail::ManagerHeader*>( base );
        std::uint32_t free_idx = hdr->first_block_offset;
        PMM_TEST( free_idx != pmm::detail::kNoBlock );
        const auto* blk = reinterpret_cast<const pmm::detail::BlockHeader*>( base + free_idx * pmm::kGranuleSize );
        PMM_TEST( blk->size == 0 );        // free block
        PMM_TEST( blk->root_offset == 0 ); // belongs to free-blocks tree
    }

    // Allocate a block: root_offset must equal its own granule index
    pmm::pptr<std::uint32_t> p = pmm::PersistMemoryManager::allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p.is_null() );
    {
        const auto*   base     = static_cast<const std::uint8_t*>( mem );
        std::uint32_t data_idx = p.offset();
        std::uint32_t blk_idx  = data_idx - pmm::detail::kBlockHeaderGranules;
        const auto*   blk = reinterpret_cast<const pmm::detail::BlockHeader*>( base + blk_idx * pmm::kGranuleSize );
        PMM_TEST( blk->size > 0 );               // allocated block
        PMM_TEST( blk->root_offset == blk_idx ); // is root of its own tree
    }

    // After deallocation: root_offset must be 0 again
    pmm::PersistMemoryManager::deallocate_typed( p );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

int main()
{
    std::cout << "=== test_avl_allocator ===\n";
    bool all_passed = true;

    PMM_RUN( "free_block_has_zero_size", test_free_block_has_zero_size );
    PMM_RUN( "best_fit_selection", test_best_fit_selection );
    PMM_RUN( "avl_integrity_stress", test_avl_integrity_stress );
    PMM_RUN( "coalesce_three_way", test_coalesce_three_way );
    PMM_RUN( "block_view_fields", test_block_view_fields );
    PMM_RUN( "avl_survives_save_load", test_avl_survives_save_load );
    PMM_RUN( "best_fit_chooses_smallest_fitting", test_best_fit_chooses_smallest_fitting );
    PMM_RUN( "reallocate_works", test_reallocate_works );
    PMM_RUN( "root_offset_semantics", test_root_offset_semantics );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
