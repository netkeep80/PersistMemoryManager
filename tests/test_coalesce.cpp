/**
 * @file test_coalesce.cpp
 * @brief Тесты слияния соседних свободных блоков — Фаза 2 (обновлено в Фазе 7)
 *
 * Фаза 7: синглтон, destroy() освобождает буфер.
 */

#include "persist_memory_manager.h"

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

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 );

    auto before = pmm::get_stats( mgr );

    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    auto mid = pmm::get_stats( mgr );
    PMM_TEST( mid.total_blocks == before.total_blocks );

    mgr->deallocate( p1 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    PMM_TEST( after.total_blocks < mid.total_blocks );

    void* big = mgr->allocate( 400 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    mgr->deallocate( big );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_with_prev()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 );

    auto before = pmm::get_stats( mgr );

    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    auto mid = pmm::get_stats( mgr );
    PMM_TEST( mid.total_blocks == before.total_blocks );

    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    PMM_TEST( after.total_blocks < mid.total_blocks );

    void* big = mgr->allocate( 400 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    mgr->deallocate( big );
    mgr->deallocate( p1 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_both_neighbors()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    void* p4 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 && p4 );

    mgr->deallocate( p1 );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    auto        before      = pmm::get_stats( mgr );
    std::size_t free_before = before.free_blocks;

    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    PMM_TEST( after.total_blocks == before.total_blocks - 2 );
    PMM_TEST( after.free_blocks == free_before - 1 );

    void* big = mgr->allocate( 600 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    mgr->deallocate( big );
    mgr->deallocate( p4 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_no_merge_when_neighbors_used()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 128 );
    void* p2 = mgr->allocate( 128 );
    void* p3 = mgr->allocate( 128 );
    PMM_TEST( p1 && p2 && p3 );

    auto before = pmm::get_stats( mgr );

    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    PMM_TEST( after.total_blocks == before.total_blocks );
    PMM_TEST( after.free_blocks == before.free_blocks + 1 );

    mgr->deallocate( p1 );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_first_block_no_next_free()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 );

    auto before = pmm::get_stats( mgr );

    mgr->deallocate( p1 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    PMM_TEST( after.total_blocks == before.total_blocks );
    PMM_TEST( after.free_blocks == before.free_blocks + 1 );

    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_zero_fragmentation_after_all_free()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int N = 8;
    void*     ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 256 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = 0; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
    }
    PMM_TEST( mgr->validate() );
    PMM_TEST( mgr->fragmentation() > 0 );

    for ( int i = 1; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    PMM_TEST( mgr->fragmentation() == 0 );
    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.total_blocks == 1 );
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_lifo_results_in_one_block()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int N = 5;
    void*     ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 512 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = N - 1; i >= 0; i-- )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.total_blocks == 1 );
    PMM_TEST( stats.free_blocks == 1 );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_fifo_results_in_one_block()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int N = 5;
    void*     ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 512 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = 0; i < N; i++ )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.total_blocks == 1 );
    PMM_TEST( stats.free_blocks == 1 );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_large_allocation_after_merge()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 );

    void* probe = mgr->allocate( 700 );
    if ( probe )
    {
        mgr->deallocate( probe );
    }

    mgr->deallocate( p1 );
    mgr->deallocate( p2 );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    void* big = mgr->allocate( 512 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    std::memset( big, 0xAB, 512 );
    const std::uint8_t* p = static_cast<const std::uint8_t*>( big );
    for ( std::size_t i = 0; i < 512; i++ )
    {
        PMM_TEST( p[i] == 0xAB );
    }

    mgr->deallocate( big );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_coalesce_stress_interleaved()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    static const int ROUNDS  = 200;
    void*            ptrs[4] = { nullptr, nullptr, nullptr, nullptr };
    std::size_t      sizes[] = { 64, 128, 256, 512 };

    for ( int r = 0; r < ROUNDS; r++ )
    {
        int slot = r % 4;
        if ( ptrs[slot] )
        {
            mgr->deallocate( ptrs[slot] );
            ptrs[slot] = nullptr;
            PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
        }
        // After auto-expand mgr pointer may have changed — always use instance()
        ptrs[slot] = pmm::PersistMemoryManager::instance()->allocate( sizes[slot] );
        PMM_TEST( ptrs[slot] != nullptr );
        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    }

    for ( int k = 0; k < 4; k++ )
    {
        if ( ptrs[k] )
        {
            pmm::PersistMemoryManager::instance()->deallocate( ptrs[k] );
        }
    }
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( stats.free_blocks == 1 );

    pmm::PersistMemoryManager::destroy();
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
