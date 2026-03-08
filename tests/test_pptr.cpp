/**
 * @file test_pptr.cpp
 * @brief Тесты персистентного типизированного указателя pptr<T, ManagerT> (Issue #102 — новый API)
 *
 * Issue #102: использует AbstractPersistMemoryManager через pmm_presets.h.
 *   - pptr<T, ManagerT> без ManagerT=void по умолчанию.
 *   - Разыменование через p.resolve(mgr), не через p.get().
 *   - Нет operator*, operator->, get_at(), operator[].
 *   - Нет reallocate_typed() в новом API.
 */

#include "pmm/pmm_presets.h"
#include "pmm/io.h"

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

using Mgr = pmm::presets::SingleThreadedHeap;

static bool test_pptr_sizeof()
{
    // Issue #102: pptr<T, ManagerT> is 4 bytes (uint32_t granule index)
    PMM_TEST( sizeof( Mgr::pptr<int> ) == 4 );
    PMM_TEST( sizeof( Mgr::pptr<double> ) == 4 );
    PMM_TEST( sizeof( Mgr::pptr<char> ) == 4 );
    PMM_TEST( sizeof( Mgr::pptr<std::uint64_t> ) == 4 );
    return true;
}

static bool test_pptr_default_null()
{
    Mgr::pptr<int> p;
    PMM_TEST( p.is_null() );
    PMM_TEST( !p );
    PMM_TEST( p.offset() == 0 );
    return true;
}

static bool test_pptr_allocate_typed_int()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( static_cast<bool>( p ) );
    PMM_TEST( p.offset() > 0 );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_resolve()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Разыменование через resolve(mgr) — единственный способ в новом API
    int* ptr = p.resolve();
    PMM_TEST( ptr != nullptr );

    *ptr = 42;
    PMM_TEST( *p.resolve() == 42 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_write_read()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Write/read via resolve
    *p.resolve() = 42;
    PMM_TEST( *p.resolve() == 42 );

    *p.resolve() = 100;
    PMM_TEST( *p.resolve() == 100 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_deallocate()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    std::size_t free_before = pmm.free_size();

    Mgr::pptr<double> p = pmm.allocate_typed<double>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    PMM_TEST( pmm.free_size() >= free_before );

    pmm.destroy();
    return true;
}

static bool test_pptr_null_resolve()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p; // null by default
    PMM_TEST( p.resolve() == nullptr );

    pmm.destroy();
    return true;
}

static bool test_pptr_allocate_array()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 10;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>( count );
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm.is_initialized() );

    int* arr = p.resolve();
    PMM_TEST( arr != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<int>( i * 10 );

    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( p.resolve()[i] == static_cast<int>( i * 10 ) );

    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

static bool test_pptr_resolve_at()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 5;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<double> p = pmm.allocate_typed<double>( count );
    PMM_TEST( !p.is_null() );

    double* arr = p.resolve();
    PMM_TEST( arr != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<double>( i ) * 1.5;

    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( pmm.resolve_at( p, i )[0] == static_cast<double>( i ) * 1.5 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_persistence()
{
    const std::size_t size     = 64 * 1024;
    const char*       filename = "pptr_test.dat";

    // Use distinct InstanceId values to simulate two separate manager "sessions"
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 200>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 201>;

    PMM_TEST( Mgr1::create( size ) );

    Mgr1::pptr<int> p1 = Mgr1::allocate_typed<int>();
    PMM_TEST( !p1.is_null() );
    *p1.resolve() = 12345;

    std::uint32_t saved_offset = p1.offset();
    PMM_TEST( pmm::save_manager<Mgr1>( filename ) );
    Mgr1::destroy();

    PMM_TEST( Mgr2::create( size ) );
    PMM_TEST( pmm::load_manager_from_file<Mgr2>( filename ) );
    PMM_TEST( Mgr2::is_initialized() );

    // Restore pptr by saved offset
    Mgr2::pptr<int> p2( saved_offset );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( *p2.resolve() == 12345 );

    Mgr2::deallocate_typed( p2 );
    Mgr2::destroy();
    std::remove( filename );
    return true;
}

static bool test_pptr_comparison()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> p2 = pmm.allocate_typed<int>();
    Mgr::pptr<int> p3 = p1;

    PMM_TEST( p1 == p3 );
    PMM_TEST( p1 != p2 );
    PMM_TEST( !( p1 == p2 ) );

    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    pmm.destroy();
    return true;
}

static bool test_pptr_multiple_types()
{
    const std::size_t size = 256 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int>    pi = pmm.allocate_typed<int>();
    Mgr::pptr<double> pd = pmm.allocate_typed<double>();
    Mgr::pptr<char>   pc = pmm.allocate_typed<char>( 16 );

    PMM_TEST( !pi.is_null() );
    PMM_TEST( !pd.is_null() );
    PMM_TEST( !pc.is_null() );
    PMM_TEST( pmm.is_initialized() );

    *pi.resolve() = 7;
    *pd.resolve() = 3.14;
    std::memcpy( pc.resolve(), "hello", 6 );

    PMM_TEST( *pi.resolve() == 7 );
    PMM_TEST( *pd.resolve() == 3.14 );
    PMM_TEST( std::memcmp( pc.resolve(), "hello", 6 ) == 0 );

    pmm.deallocate_typed( pi );
    pmm.deallocate_typed( pd );
    pmm.deallocate_typed( pc );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/**
 * @brief При нехватке памяти менеджер (HeapStorage) автоматически расширяется.
 *
 * Uses unique InstanceId (202) to start with a fresh backend of exactly 8K.
 */
static bool test_pptr_allocate_auto_expand()
{
    using MgrExpand = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 202>;

    const std::size_t initial_size = 8 * 1024;

    PMM_TEST( MgrExpand::create( initial_size ) );

    std::size_t initial_total = MgrExpand::total_size();

    // Fill most of initial buffer
    MgrExpand::pptr<std::uint8_t> p1 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p1.is_null() );

    // Request second large block — should trigger expansion
    MgrExpand::pptr<std::uint8_t> p2 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p2.is_null() );

    PMM_TEST( MgrExpand::is_initialized() );
    PMM_TEST( MgrExpand::total_size() > initial_total );

    MgrExpand::destroy();
    return true;
}

static bool test_pptr_deallocate_null()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p;
    pmm.deallocate_typed( p ); // deallocating null should be safe
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

int main()
{
    std::cout << "=== test_pptr ===\n";
    bool all_passed = true;

    PMM_RUN( "pptr_sizeof", test_pptr_sizeof );
    PMM_RUN( "pptr_default_null", test_pptr_default_null );
    PMM_RUN( "pptr_allocate_typed_int", test_pptr_allocate_typed_int );
    PMM_RUN( "pptr_resolve", test_pptr_resolve );
    PMM_RUN( "pptr_write_read", test_pptr_write_read );
    PMM_RUN( "pptr_deallocate", test_pptr_deallocate );
    PMM_RUN( "pptr_null_resolve", test_pptr_null_resolve );
    PMM_RUN( "pptr_allocate_array", test_pptr_allocate_array );
    PMM_RUN( "pptr_resolve_at", test_pptr_resolve_at );
    PMM_RUN( "pptr_persistence", test_pptr_persistence );
    PMM_RUN( "pptr_comparison", test_pptr_comparison );
    PMM_RUN( "pptr_multiple_types", test_pptr_multiple_types );
    PMM_RUN( "pptr_allocate_auto_expand", test_pptr_allocate_auto_expand );
    PMM_RUN( "pptr_deallocate_null", test_pptr_deallocate_null );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
