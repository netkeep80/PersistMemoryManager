/**
 * @file persistence_demo.cpp
 * @brief PersistMemoryManager persistence demo
 *
 * Demonstrates:
 * 1. Creating a manager and populating with data.
 * 2. Saving the memory image to a file.
 * 3. Destroying the first manager (simulates program termination).
 * 4. Loading the image into a new manager.
 * 5. Verifying that data and metadata are fully restored.
 * 6. Continuing operations on the restored manager.
 *
 * - All methods are static (Mgr::create(), Mgr::allocate(), etc.)
 * - p.resolve() — no argument needed (uses static manager resolve)
 * - pmm::save_manager<Mgr>(filename) — template-based save
 * - pmm::load_manager_from_file<Mgr>(filename) — template-based load
 * - Two distinct InstanceIds (10 / 11) simulate separate program sessions
 */

#include "pmm_single_threaded_heap.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

// Session A: first run (create + populate + save)
using MgrA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 10>;
// Session B: second run (load + verify + continue)
using MgrB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 11>;

static const char* IMAGE_FILE = "heap_image.dat";

int main()
{
    std::cout << "=== PersistMemoryManager — Persistence Demo (updated #110) ===\n\n";

    // ─── Phase A: Create and populate ────────────────────────────────────────

    const std::size_t memory_size = 256 * 1024; // 256 KB

    if ( !MgrA::create( memory_size ) )
    {
        std::cerr << "Failed to create manager\n";
        return 1;
    }
    std::cout << "[A] Manager created. Size: " << memory_size / 1024 << " KB\n";

    const std::size_t size1 = 512;
    const std::size_t size2 = 1024;
    const std::size_t size3 = 256;

    MgrA::pptr<uint8_t> p1 = MgrA::allocate_typed<uint8_t>( size1 );
    MgrA::pptr<uint8_t> p2 = MgrA::allocate_typed<uint8_t>( size2 );
    MgrA::pptr<uint8_t> p3 = MgrA::allocate_typed<uint8_t>( size3 );

    if ( p1.is_null() || p2.is_null() || p3.is_null() )
    {
        std::cerr << "Error allocating blocks\n";
        MgrA::destroy();
        return 1;
    }

    // Write: p1 = string, p2 = array of squares, p3 = pattern
    std::strcpy( reinterpret_cast<char*>( p1.resolve() ), "Hello, PersistMemoryManager!" );

    int* arr2 = reinterpret_cast<int*>( p2.resolve() );
    for ( std::size_t i = 0; i < size2 / sizeof( int ); i++ )
        arr2[i] = static_cast<int>( i * i );

    std::memset( p3.resolve(), 0xFF, size3 );

    std::cout << "[A] 3 blocks allocated and written.\n";

    // Deallocate p3 to show partial free state is preserved
    MgrA::deallocate_typed( p3 );
    std::cout << "[A] Block p3 deallocated (shows partial free heap saved correctly).\n";

    if ( !MgrA::is_initialized() )
    {
        std::cerr << "Manager not initialized before save\n";
        MgrA::destroy();
        return 1;
    }

    std::cout << "\nStats before save:\n"
              << "  Total blocks : " << MgrA::block_count() << "\n"
              << "  Free blocks  : " << MgrA::free_block_count() << "\n"
              << "  Alloc blocks : " << MgrA::alloc_block_count() << "\n"
              << "  Free size    : " << MgrA::free_size() << " bytes\n";

    // Save granule offsets for reconstruction after load
    std::uint32_t off1 = p1.offset();
    std::uint32_t off2 = p2.offset();

    // ─── Phase B: Save image ──────────────────────────────────────────────────

    if ( !pmm::save_manager<MgrA>( IMAGE_FILE ) )
    {
        std::cerr << "Error saving image to: " << IMAGE_FILE << "\n";
        MgrA::destroy();
        return 1;
    }
    std::cout << "\n[B] Image saved to: " << IMAGE_FILE << "\n";

    MgrA::destroy();
    std::cout << "[B] First manager destroyed (simulates program termination).\n";

    // ─── Phase C: Restore from file ───────────────────────────────────────────

    std::cout << "\n[C] Loading image from file...\n";

    if ( !MgrB::create( memory_size ) )
    {
        std::cerr << "Failed to create second manager\n";
        return 1;
    }

    if ( !pmm::load_manager_from_file<MgrB>( IMAGE_FILE ) )
    {
        std::cerr << "Failed to load image from file\n";
        MgrB::destroy();
        return 1;
    }

    if ( !MgrB::is_initialized() )
    {
        std::cerr << "Manager not initialized after load\n";
        MgrB::destroy();
        return 1;
    }

    std::cout << "[C] Image loaded successfully.\n";
    std::cout << "\nStats after load:\n"
              << "  Total blocks : " << MgrB::block_count() << "\n"
              << "  Free blocks  : " << MgrB::free_block_count() << "\n"
              << "  Alloc blocks : " << MgrB::alloc_block_count() << "\n";

    // ─── Phase D: Data verification ───────────────────────────────────────────

    // Reconstruct pptr from saved granule offsets
    MgrB::pptr<uint8_t> q1( off1 );
    MgrB::pptr<uint8_t> q2( off2 );

    std::cout << "\n[D] Data verification:\n";

    bool data_ok = true;
    if ( std::strcmp( reinterpret_cast<const char*>( q1.resolve() ), "Hello, PersistMemoryManager!" ) == 0 )
    {
        std::cout << "  p1 (string)  : OK — \"" << reinterpret_cast<const char*>( q1.resolve() ) << "\"\n";
    }
    else
    {
        std::cout << "  p1 (string)  : FAIL — data corrupted\n";
        data_ok = false;
    }

    bool arr_ok = true;
    int* q2_int = reinterpret_cast<int*>( q2.resolve() );
    for ( std::size_t i = 0; i < size2 / sizeof( int ); i++ )
    {
        if ( q2_int[i] != static_cast<int>( i * i ) )
        {
            arr_ok = false;
            break;
        }
    }
    std::cout << "  p2 (array)   : " << ( arr_ok ? "OK" : "FAIL" ) << "\n";
    data_ok &= arr_ok;

    // ─── Phase E: Continued operations ───────────────────────────────────────

    std::cout << "\n[E] Continued operation on restored manager:\n";

    MgrB::pptr<uint8_t> p_new = MgrB::allocate_typed<uint8_t>( 128 );
    if ( !p_new.is_null() )
    {
        std::memset( p_new.resolve(), 0xAB, 128 );
        std::cout << "  New block allocated: offset=" << p_new.offset() << "\n";
        MgrB::deallocate_typed( p_new );
        std::cout << "  New block deallocated.\n";
    }
    else
    {
        std::cout << "  Failed to allocate new block.\n";
        data_ok = false;
    }

    if ( MgrB::is_initialized() )
        std::cout << "  Final state validation: OK\n";
    else
    {
        std::cout << "  Final state validation: FAIL\n";
        data_ok = false;
    }

    // ─── Cleanup ──────────────────────────────────────────────────────────────

    MgrB::destroy();
    std::remove( IMAGE_FILE );

    std::cout << "\n=== Demo completed: " << ( data_ok ? "SUCCESS" : "ERROR" ) << " ===\n";
    return data_ok ? 0 : 1;
}
