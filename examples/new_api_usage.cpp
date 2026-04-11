/**
 * @file new_api_usage.cpp
 * @brief Example of unified PersistMemoryManager<ConfigT, InstanceId> API
 *
 * Demonstrates the unified static API introduced.
 *
 * both AbstractPersistMemoryManager (object model) and StaticMemoryManager.
 *
 * Key API:
 *   - All methods are static: Mgr::create(), Mgr::allocate_typed(), etc.
 *   - p.resolve() — no argument needed (uses static manager resolve)
 *   - pmm::save_manager<Mgr>(filename) — template-based save
 *   - pmm::load_manager_from_file<Mgr>(filename) — template-based load
 *   - Multiple independent managers via distinct InstanceIds
 *
 * Key advantages of pptr<T>:
 *   - 4 bytes instead of 8 (address-independent 32-bit granule index)
 *   - Correctly loads from file at a different base address
 *   - No p++/p-- (no accidental pointer arithmetic)
 */

#include "pmm_single_threaded_heap.h"

#include <cstdio>
#include <cstring>
#include <iostream>

// ─── 1. Single-threaded heap manager ──────────────────────────────────────────

struct Point
{
    double x, y;
};

static void demo_single_threaded_heap()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 40>;

    std::cout << "=== SingleThreadedHeap (HeapStorage, NoLock) — static API ===\n";

    if ( !Mgr::create( 64 * 1024 ) ) // 64 KiB
    {
        std::cerr << "Failed to create manager\n";
        return;
    }

    std::cout << "Total: " << Mgr::total_size() << " bytes\n";

    // Allocate typed objects — externally only pptr<T, MgrT>
    Mgr::pptr<Point> p_point = Mgr::allocate_typed<Point>();
    Mgr::pptr<int>   p_arr   = Mgr::allocate_typed<int>( 10 ); // array of 10 ints

    if ( !p_point.is_null() && !p_arr.is_null() )
    {
        // Dereference via resolve() — no argument needed with static model
        Point* pt = p_point.resolve();
        pt->x     = 1.5;
        pt->y     = 2.5;
        std::cout << "Point: (" << p_point.resolve()->x << ", " << p_point.resolve()->y << ")\n";

        // Write to array via resolve() + index
        int* arr = p_arr.resolve();
        for ( int i = 0; i < 10; ++i )
            arr[i] = i * i;

        int sum = 0;
        for ( int i = 0; i < 10; ++i )
            sum += arr[i];
        std::cout << "Array sum of squares: " << sum << " (expected: 285)\n";

        std::cout << "Used: " << Mgr::used_size() << " bytes\n";
        std::cout << "Free: " << Mgr::free_size() << " bytes\n";

        // Save granule offset — demonstrates address-independence
        std::uint32_t point_offset = p_point.offset();
        std::cout << "Point pptr offset=" << point_offset << " (4 bytes, address-independent)\n";

        Mgr::deallocate_typed( p_point );
        Mgr::deallocate_typed( p_arr );
    }

    Mgr::destroy();
    std::cout << "\n";
}

// ─── 2. Multi-threaded heap manager ───────────────────────────────────────────

static void demo_multi_threaded_heap()
{
    using Mgr = pmm::PersistMemoryManager<pmm::PersistentDataConfig, 41>;

    std::cout << "=== MultiThreadedHeap (HeapStorage, SharedMutexLock) — static API ===\n";

    if ( !Mgr::create( 32 * 1024 ) )
    {
        std::cerr << "Failed to create MultiThreadedHeap\n";
        return;
    }

    std::cout << "Total: " << Mgr::total_size() << " bytes\n";

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 128 );
    if ( !p.is_null() )
    {
        std::memset( p.resolve(), 0xBB, 128 );
        std::cout << "Allocated 128 bytes (pptr offset=" << p.offset() << ", sizeof(pptr)=" << sizeof( p ) << ")\n";
        Mgr::deallocate_typed( p );
        std::cout << "Deallocated via pptr<T, MgrT>.\n";
    }

    Mgr::destroy();
    std::cout << "\n";
}

// ─── 3. Persistence with save/load ───────────────────────────────────────────

// Session A: create and populate
using SessionA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 42>;
// Session B: reload and verify
using SessionB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 43>;

static void demo_persistence()
{
    std::cout << "=== Persistence demo: save_manager / load_manager_from_file ===\n";

    const char*       export_file = "/tmp/demo_pmm_export.dat";
    const std::size_t kSize       = 64 * 1024;

    std::uint32_t saved_offset = 0;

    // Session A: create, allocate, save
    if ( !SessionA::create( kSize ) )
    {
        std::cerr << "Failed to create session A\n";
        return;
    }

    SessionA::pptr<double> p = SessionA::allocate_typed<double>();
    if ( !p.is_null() )
    {
        *p.resolve() = 3.14;
        saved_offset = p.offset();
        std::cout << "Allocated double=3.14 via pptr<double> (offset=" << saved_offset << ")\n";
    }

    if ( pmm::save_manager<SessionA>( export_file ) )
    {
        std::cout << "Saved via save_manager<SessionA>()\n";
    }
    else
    {
        std::cerr << "Failed to save\n";
        SessionA::destroy();
        return;
    }

    SessionA::destroy();

    // Session B: reload and verify
    if ( !SessionB::create( kSize ) )
    {
        std::cerr << "Failed to create session B\n";
        std::remove( export_file );
        return;
    }

    if ( pmm::load_manager_from_file<SessionB>( export_file ) )
    {
        // Reconstruct pptr from saved granule offset
        SessionB::pptr<double> q( saved_offset );
        std::cout << "Reloaded: pptr<double>(offset=" << saved_offset << ") → value=" << *q.resolve()
                  << " (expected: 3.14)\n";
    }
    else
    {
        std::cerr << "Failed to load\n";
    }

    SessionB::destroy();
    std::remove( export_file );
    std::cout << "\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "PersistMemoryManager — unified static API \n\n";
    std::cout << "Architecture: PersistMemoryManager<ConfigT, InstanceId>\n";
    std::cout << "  - All methods static: Mgr::create(), Mgr::allocate_typed(), etc.\n";
    std::cout << "  - p.resolve() — no argument (uses static manager resolve)\n";
    std::cout << "  - Multiple managers: distinct InstanceIds = distinct static state\n\n";

    demo_single_threaded_heap();
    demo_multi_threaded_heap();
    demo_persistence();

    std::cout << "All demos completed successfully.\n";
    return 0;
}
