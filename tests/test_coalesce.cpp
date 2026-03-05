/**
 * @file test_coalesce.cpp
 * @brief Тесты слияния соседних свободных блоков — Фаза 2 (обновлено в Issue #61)
 *
 * Issue #61: менеджер — полностью статический класс.
 *   - Все операции через статические методы PersistMemoryManager::xxx().
 *   - Выделение через allocate_typed<T>(), освобождение через deallocate_typed().
 */

#include "pmm/legacy_manager.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

static bool test_coalesce_with_next()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    auto before = pmm::get_stats();

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto mid = pmm::get_stats();
    PMM_TEST( mid.total_blocks == before.total_blocks );

    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto after = pmm::get_stats();
    PMM_TEST( after.total_blocks < mid.total_blocks );

    pmm::pptr<std::uint8_t> big = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 400 );
    PMM_TEST( !big.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( big );
    pmm::PersistMemoryManager<>::deallocate_typed( p3 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_with_prev()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    auto before = pmm::get_stats();

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto mid = pmm::get_stats();
    PMM_TEST( mid.total_blocks == before.total_blocks );

    pmm::PersistMemoryManager<>::deallocate_typed( p3 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto after = pmm::get_stats();
    PMM_TEST( after.total_blocks < mid.total_blocks );

    pmm::pptr<std::uint8_t> big = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 400 );
    PMM_TEST( !big.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( big );
    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_both_neighbors()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p4 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() && !p4.is_null() );

    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    pmm::PersistMemoryManager<>::deallocate_typed( p3 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto        before      = pmm::get_stats();
    std::size_t free_before = before.free_blocks;

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto after = pmm::get_stats();
    PMM_TEST( after.total_blocks == before.total_blocks - 2 );
    PMM_TEST( after.free_blocks == free_before - 1 );

    pmm::pptr<std::uint8_t> big = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 600 );
    PMM_TEST( !big.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( big );
    pmm::PersistMemoryManager<>::deallocate_typed( p4 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_no_merge_when_neighbors_used()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    auto before = pmm::get_stats();

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto after = pmm::get_stats();
    PMM_TEST( after.total_blocks == before.total_blocks );
    PMM_TEST( after.free_blocks == before.free_blocks + 1 );

    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    pmm::PersistMemoryManager<>::deallocate_typed( p3 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_first_block_no_next_free()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    auto before = pmm::get_stats();

    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto after = pmm::get_stats();
    PMM_TEST( after.total_blocks == before.total_blocks );
    PMM_TEST( after.free_blocks == before.free_blocks + 1 );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_zero_fragmentation_after_all_free()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    const int               N = 8;
    pmm::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < N; i += 2 )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    PMM_TEST( pmm::PersistMemoryManager<>::fragmentation() > 0 );

    for ( int i = 1; i < N; i += 2 )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::fragmentation() == 0 );
    auto stats = pmm::get_stats();
    // Issue #75: BlockHeader_0 (ManagerHeader) always present as allocated block
    PMM_TEST( stats.total_blocks == 2 );
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 1 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_lifo_results_in_one_block()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    const int               N = 5;
    pmm::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = N - 1; i >= 0; i-- )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    auto stats = pmm::get_stats();
    // Issue #75: BlockHeader_0 (ManagerHeader) always present as allocated block
    PMM_TEST( stats.total_blocks == 2 );
    PMM_TEST( stats.free_blocks == 1 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_fifo_results_in_one_block()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    const int               N = 5;
    pmm::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < N; i++ )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    auto stats = pmm::get_stats();
    // Issue #75: BlockHeader_0 (ManagerHeader) always present as allocated block
    PMM_TEST( stats.total_blocks == 2 );
    PMM_TEST( stats.free_blocks == 1 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_large_allocation_after_merge()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    pmm::pptr<std::uint8_t> probe = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 700 );
    if ( !probe.is_null() )
        pmm::PersistMemoryManager<>::deallocate_typed( probe );

    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::deallocate_typed( p3 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::pptr<std::uint8_t> big = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !big.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    std::memset( big.get(), 0xAB, 512 );
    const std::uint8_t* p = big.get();
    for ( std::size_t i = 0; i < 512; i++ )
        PMM_TEST( p[i] == 0xAB );

    pmm::PersistMemoryManager<>::deallocate_typed( big );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_coalesce_stress_interleaved()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    static const int        ROUNDS = 200;
    pmm::pptr<std::uint8_t> ptrs[4];
    std::size_t             sizes[] = { 64, 128, 256, 512 };

    for ( int r = 0; r < ROUNDS; r++ )
    {
        int slot = r % 4;
        if ( !ptrs[slot].is_null() )
        {
            pmm::PersistMemoryManager<>::deallocate_typed( ptrs[slot] );
            ptrs[slot] = pmm::pptr<std::uint8_t>();
            PMM_TEST( pmm::PersistMemoryManager<>::validate() );
        }
        ptrs[slot] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( sizes[slot] );
        PMM_TEST( !ptrs[slot].is_null() );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    for ( int k = 0; k < 4; k++ )
    {
        if ( !ptrs[k].is_null() )
            pmm::PersistMemoryManager<>::deallocate_typed( ptrs[k] );
    }
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats = pmm::get_stats();
    PMM_TEST( stats.free_blocks == 1 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

int main()
{
    std::cout << "=== test_coalesce ===\n";
    bool all_passed = true;

    PMM_RUN( "coalesce_with_next", test_coalesce_with_next );
    PMM_RUN( "coalesce_with_prev", test_coalesce_with_prev );
    PMM_RUN( "coalesce_both_neighbors", test_coalesce_both_neighbors );
    PMM_RUN( "coalesce_no_merge_when_neighbors_used", test_coalesce_no_merge_when_neighbors_used );
    PMM_RUN( "coalesce_first_block_no_next_free", test_coalesce_first_block_no_next_free );
    PMM_RUN( "coalesce_zero_fragmentation_after_all_free", test_coalesce_zero_fragmentation_after_all_free );
    PMM_RUN( "coalesce_lifo_results_in_one_block", test_coalesce_lifo_results_in_one_block );
    PMM_RUN( "coalesce_fifo_results_in_one_block", test_coalesce_fifo_results_in_one_block );
    PMM_RUN( "coalesce_large_allocation_after_merge", test_coalesce_large_allocation_after_merge );
    PMM_RUN( "coalesce_stress_interleaved", test_coalesce_stress_interleaved );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
