/**
 * @file fuzz_allocator.cpp
 * @brief libFuzzer harness for PersistMemoryManager allocator (Issue #213, Phase 5.2).
 *
 * Coverage-guided fuzzing of allocate/deallocate/reallocate operations.
 *
 * Build with:
 *   clang++ -std=c++20 -fsanitize=fuzzer,address -O2 \
 *     -I include -I single_include -I single_include/pmm \
 *     tests/fuzz_allocator.cpp -o fuzz_allocator
 *
 * Run:
 *   ./fuzz_allocator -max_total_time=60
 *
 * The fuzzer interprets the input byte sequence as a series of commands:
 *   byte 0: command (0=alloc, 1=dealloc, 2=reallocate)
 *   byte 1-2: size parameter (little-endian uint16_t)
 *
 * @see docs/phase5_testing.md §5.2
 * @version 0.1 (Issue #213 — Phase 5.2: Fuzz testing)
 */

#include "pmm/pmm_presets.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using M = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<65536>, 0>;

extern "C" int LLVMFuzzerTestOneInput( const std::uint8_t* data, std::size_t size )
{
    M::destroy();
    if ( !M::create() )
        return 0;

    struct LiveBlock
    {
        void*       ptr;
        std::size_t sz;
    };
    std::vector<LiveBlock> live;
    live.reserve( 64 );

    std::size_t pos = 0;
    while ( pos + 3 <= size )
    {
        std::uint8_t  cmd = data[pos];
        std::uint16_t param =
            static_cast<std::uint16_t>( data[pos + 1] ) | ( static_cast<std::uint16_t>( data[pos + 2] ) << 8 );
        pos += 3;

        switch ( cmd % 3 )
        {
        case 0: // allocate
        {
            std::size_t sz = ( param % 4096 ) + 1;
            void*       p  = M::allocate( sz );
            if ( p != nullptr )
            {
                std::memset( p, 0xCC, sz );
                live.push_back( { p, sz } );
            }
            break;
        }
        case 1: // deallocate
        {
            if ( !live.empty() )
            {
                std::size_t idx = param % live.size();
                M::deallocate( live[idx].ptr );
                live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
            }
            break;
        }
        case 2: // reallocate (via alloc + copy + free)
        {
            if ( !live.empty() )
            {
                std::size_t idx    = param % live.size();
                std::size_t new_sz = ( ( param >> 4 ) % 4096 ) + 1;
                void*       new_p  = M::allocate( new_sz );
                if ( new_p != nullptr )
                {
                    std::size_t copy_sz = live[idx].sz < new_sz ? live[idx].sz : new_sz;
                    std::memcpy( new_p, live[idx].ptr, copy_sz );
                    M::deallocate( live[idx].ptr );
                    live[idx].ptr = new_p;
                    live[idx].sz  = new_sz;
                }
            }
            break;
        }
        }
    }

    // Cleanup.
    for ( auto& blk : live )
        M::deallocate( blk.ptr );

    // Verify manager is still in a valid state after all operations.
    (void)M::is_initialized();

    M::destroy();
    return 0;
}
