/**
 * @file basic_usage.cpp
 * @brief Basic usage example for PersistMemoryManager
 *
 * Demonstrates:
 * 1. Creating a static manager with HeapStorage.
 * 2. Allocating blocks of different sizes.
 * 3. Deallocating blocks.
 * 4. Manual realloc pattern (alloc-copy-dealloc).
 * 5. Block statistics via new API.
 * 6. Save/load round-trip.
 *
 * - All methods are static (Mgr::create(), Mgr::allocate(), etc.)
 * - p.resolve() — no argument needed (uses static manager resolve)
 * - pmm::save_manager<Mgr>(filename) — template-based save
 * - pmm::load_manager_from_file<Mgr>(filename) — template-based load
 */

#include "pmm_single_threaded_heap.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

using Mgr  = pmm::presets::SingleThreadedHeap;
using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 1>; // second "session"

int main()
{
    // ── 1. Create manager ─────────────────────────────────────────────────────
    const std::size_t memory_size = 1024 * 1024; // 1 MB

    if ( !Mgr::create( memory_size ) )
    {
        std::cerr << "Failed to create PersistMemoryManager\n";
        return 1;
    }
    std::cout << "Manager created. Managed area: " << memory_size / 1024 << " KB\n\n";

    // ── 2. Allocate blocks ────────────────────────────────────────────────────
    Mgr::pptr<std::uint8_t> block1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> block2 = Mgr::allocate_typed<std::uint8_t>( 1024 );
    Mgr::pptr<std::uint8_t> block3 = Mgr::allocate_typed<std::uint8_t>( 4096 );

    if ( block1.is_null() || block2.is_null() || block3.is_null() )
    {
        std::cerr << "Error allocating blocks\n";
        Mgr::destroy();
        return 1;
    }

    std::cout << "Allocated 3 blocks:\n";
    std::cout << "  block1 (256 bytes):  offset=" << block1.offset() << "\n";
    std::cout << "  block2 (1024 bytes): offset=" << block2.offset() << "\n";
    std::cout << "  block3 (4096 bytes): offset=" << block3.offset() << "\n\n";

    // Check alignment (granule = 16 bytes guarantees align=16)
    auto check_align = []( void* ptr, std::size_t align, const char* name )
    {
        bool ok = ( reinterpret_cast<std::uintptr_t>( ptr ) % align == 0 );
        std::cout << "  Alignment " << name << " at " << align << " bytes: " << ( ok ? "OK" : "FAIL" ) << "\n";
        return ok;
    };
    bool aligns_ok = true;
    aligns_ok &= check_align( block1.resolve(), 16, "block1" );
    aligns_ok &= check_align( block2.resolve(), 16, "block2" );
    aligns_ok &= check_align( block3.resolve(), 16, "block3" );
    std::cout << "\n";

    // ── 3. Write data ─────────────────────────────────────────────────────────
    std::memset( block1.resolve(), 0xAA, 256 );
    std::memset( block2.resolve(), 0xBB, 1024 );
    std::memset( block3.resolve(), 0xCC, 4096 );
    std::cout << "Data written to blocks.\n\n";

    // ── 4. Block statistics ───────────────────────────────────────────────────
    std::cout << "Block statistics after allocations:\n"
              << "  Total blocks : " << Mgr::block_count() << "\n"
              << "  Free blocks  : " << Mgr::free_block_count() << "\n"
              << "  Alloc blocks : " << Mgr::alloc_block_count() << "\n"
              << "  Free size    : " << Mgr::free_size() << " bytes\n"
              << "  Used size    : " << Mgr::used_size() << " bytes\n\n";

    // ── 5. Deallocate block1 ──────────────────────────────────────────────────
    Mgr::deallocate_typed( block1 );
    block1 = Mgr::pptr<std::uint8_t>(); // null
    std::cout << "block1 deallocated.\n";
    std::cout << "Alloc blocks after dealloc: " << Mgr::alloc_block_count() << "\n\n";

    // ── 6. Manual realloc of block2 (alloc-copy-dealloc) ─────────────────────
    const std::size_t old_size = 1024;
    const std::size_t new_size = 2048;

    Mgr::pptr<std::uint8_t> block2_new = Mgr::allocate_typed<std::uint8_t>( new_size );
    if ( block2_new.is_null() )
    {
        std::cerr << "Error reallocating block2\n";
    }
    else
    {
        std::memcpy( block2_new.resolve(), block2.resolve(), old_size );
        Mgr::deallocate_typed( block2 );
        block2 = block2_new;
        std::cout << "block2 reallocated (1024 -> 2048 bytes): offset=" << block2.offset() << "\n\n";
    }

    // ── 7. Verify manager is still valid ─────────────────────────────────────
    bool valid = Mgr::is_initialized();
    std::cout << "Manager initialized: " << ( valid ? "OK" : "FAIL" ) << "\n\n";

    // ── 8. Deallocate remaining blocks ────────────────────────────────────────
    Mgr::deallocate_typed( block2 );
    Mgr::deallocate_typed( block3 );
    std::cout << "All blocks deallocated.\n";
    std::cout << "Final alloc_block_count: " << Mgr::alloc_block_count() << "\n";

    // ── 9. Save/load round-trip demo ──────────────────────────────────────────
    const char* DEMO_FILE = "basic_usage_demo.dat";
    if ( pmm::save_manager<Mgr>( DEMO_FILE ) )
    {
        std::cout << "\nSaved manager state to: " << DEMO_FILE << "\n";
        Mgr::destroy();

        if ( Mgr2::create( memory_size ) && pmm::load_manager_from_file<Mgr2>( DEMO_FILE ) )
        {
            std::cout << "Loaded manager state: " << ( Mgr2::is_initialized() ? "OK" : "FAIL" ) << "\n";
            Mgr2::destroy();
        }
        std::remove( DEMO_FILE );
    }
    else
    {
        // ── 10. Destroy manager ───────────────────────────────────────────────
        Mgr::destroy();
    }

    std::cout << "\nExample completed " << ( aligns_ok && valid ? "successfully" : "with failures" ) << ".\n";
    return ( aligns_ok && valid ) ? 0 : 1;
}
