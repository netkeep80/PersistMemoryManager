/**
 * @file pmm/ppool.h
 * @brief ppool<T, ManagerT> — persistent object pool with O(1) allocate/deallocate (Issue #199, Phase 3.6).
 *
 * Implements a pool of fixed-size objects in the persistent address space (PAP).
 * Objects are allocated from large chunks via a free-list, providing O(1)
 * allocation and deallocation — ideal for mass creation of tree nodes, list nodes,
 * graph nodes, and similar small objects.
 *
 * Key properties:
 *   - O(1) allocate() / deallocate() via embedded free-list.
 *   - Chunks allocated from the manager in configurable sizes (default 64 objects per chunk).
 *   - All internal references use granule indices — address-independent across PAP reloads.
 *   - POD-structure: all fields are primitive types (trivially copyable),
 *     enabling direct serialization in PAP.
 *   - Element type T must be trivially copyable.
 *   - Each slot is aligned to granule boundaries for correct granule index addressing.
 *
 * Usage:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(256 * 1024);
 *
 *   // Create a pool for int objects
 *   Mgr::pptr<Mgr::ppool<int>> pool = Mgr::create_typed<Mgr::ppool<int>>();
 *
 *   // Allocate objects from the pool — O(1)
 *   int* a = pool->allocate();
 *   int* b = pool->allocate();
 *   *a = 42;
 *   *b = 99;
 *
 *   // Deallocate — O(1), returns the slot to the free-list
 *   pool->deallocate(a);
 *
 *   // Allocate again — reuses the freed slot
 *   int* c = pool->allocate();
 *
 *   // Free all chunks and destroy
 *   pool->free_all();
 *   Mgr::destroy_typed(pool);
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see parray.h   — parray<T, ManagerT> (persistent dynamic array with O(1) indexing)
 * @see pallocator.h — pallocator<T, ManagerT> (STL-compatible allocator)
 * @see persist_memory_manager.h — PersistMemoryManager (static model)
 * @see pptr.h — pptr<T, ManagerT> (persistent pointer)
 * @version 0.1 (Issue #199 — Phase 3.6: persistent object pool)
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

/**
 * @brief Persistent object pool with O(1) allocate/deallocate (Issue #199, Phase 3.6).
 *
 * Allocates large chunks from the memory manager and subdivides them into
 * fixed-size, granule-aligned slots. Free slots are linked via an embedded
 * free-list (the slot data area stores the granule index of the next free slot).
 *
 * A ppool object is created in PAP via create_typed<ppool<T>>() and destroyed
 * via destroy_typed() after calling free_all().
 *
 * Each slot occupies one or more granules to ensure correct granule index addressing.
 * This means small types (e.g. int on a 16-byte-granule system) use one full granule
 * per slot. This is a deliberate trade-off: pool objects are typically small and
 * numerous (tree/graph nodes), and granule alignment ensures persistence safety.
 *
 * Layout in PAP:
 * @code
 *   ppool<T> (in PAP)
 *   +--------------------+
 *   | _free_head_idx     |  --> granule index of the first free slot (0 = none)
 *   | _chunk_head_idx    |  --> granule index of the first chunk (0 = none)
 *   | _objects_per_chunk  |  --> number of T objects per chunk
 *   | _total_allocated   |  --> total number of live (non-free) objects
 *   | _total_capacity    |  --> total number of slots across all chunks
 *   +--------------------+
 *
 *   Each chunk is a separately allocated block. The first granule of the chunk
 *   stores the next_chunk_idx (linking chunks into a singly-linked list).
 *   Remaining granules are slots for T objects.
 *
 *   Chunk:
 *   +------------------+--------+--------+-----+--------+
 *   | next_chunk_idx   | slot_0 | slot_1 | ... | slot_N |
 *   | (1 granule)      | (G granules each)               |
 *   +------------------+--------+--------+-----+--------+
 *
 *   G = ceil(max(sizeof(T), sizeof(index_type)) / granule_size)
 *
 *   Free slots store the granule index of the next free slot in their
 *   first sizeof(index_type) bytes (embedded free-list).
 * @endcode
 *
 * @tparam T        Element type. Must be trivially copyable for PAP persistence.
 * @tparam ManagerT Memory manager type (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename T, typename ManagerT> struct ppool
{
    static_assert( std::is_trivially_copyable_v<T>, "ppool<T>: T must be trivially copyable for PAP persistence" );

    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using value_type   = T;

    static constexpr std::size_t granule_size = ManagerT::address_traits::granule_size;

    /// @brief Minimum byte size per slot: must hold at least T and an index_type (for free-list link).
    static constexpr std::size_t min_slot_bytes =
        ( sizeof( T ) >= sizeof( index_type ) ) ? sizeof( T ) : sizeof( index_type );

    /// @brief Number of granules per slot (rounded up for granule alignment).
    static constexpr std::size_t granules_per_slot = ( min_slot_bytes + granule_size - 1 ) / granule_size;

    /// @brief Actual slot size in bytes (granule-aligned).
    static constexpr std::size_t slot_bytes = granules_per_slot * granule_size;

    /// @brief Default number of objects per chunk.
    static constexpr std::uint32_t default_objects_per_chunk = 64;

    index_type    _free_head_idx;     ///< Granule index of the first free slot (0 = empty).
    index_type    _chunk_head_idx;    ///< Granule index of the first chunk (0 = none).
    std::uint32_t _objects_per_chunk; ///< Number of T objects per chunk.
    std::uint32_t _total_allocated;   ///< Number of currently allocated (live) objects.
    std::uint32_t _total_capacity;    ///< Total slot count across all chunks.

    // --- Constructor / Destructor -----------------------------------------------

    /// @brief Default constructor — empty pool with default chunk size.
    ppool() noexcept
        : _free_head_idx( static_cast<index_type>( 0 ) ), _chunk_head_idx( static_cast<index_type>( 0 ) ),
          _objects_per_chunk( default_objects_per_chunk ), _total_allocated( 0 ), _total_capacity( 0 )
    {
    }

    /// @brief Destructor — trivial (chunks are freed via free_all()).
    ~ppool() noexcept = default;

    // --- Configuration ----------------------------------------------------------

    /**
     * @brief Set the number of objects per chunk.
     *
     * Must be called before any allocation. Has no effect if chunks are already allocated.
     *
     * @param n Number of objects per chunk (must be >= 1).
     */
    void set_objects_per_chunk( std::uint32_t n ) noexcept
    {
        if ( n >= 1 && _chunk_head_idx == static_cast<index_type>( 0 ) )
            _objects_per_chunk = n;
    }

    // --- Read access ------------------------------------------------------------

    /// @brief Number of currently allocated (live) objects.
    std::uint32_t allocated_count() const noexcept { return _total_allocated; }

    /// @brief Total slot count across all chunks.
    std::uint32_t total_capacity() const noexcept { return _total_capacity; }

    /// @brief Number of free slots available without allocating a new chunk.
    std::uint32_t free_count() const noexcept { return _total_capacity - _total_allocated; }

    /// @brief Check if the pool has no allocated objects.
    bool empty() const noexcept { return _total_allocated == 0; }

    // --- Allocation / Deallocation ----------------------------------------------

    /**
     * @brief Allocate one object from the pool — O(1).
     *
     * If the free-list is empty, a new chunk is allocated from the manager.
     * The returned memory is zero-initialized.
     *
     * @return Pointer to a slot for one T object, or nullptr on allocation failure.
     */
    T* allocate() noexcept
    {
        // If free-list is empty, allocate a new chunk.
        if ( _free_head_idx == static_cast<index_type>( 0 ) )
        {
            if ( !allocate_chunk() )
                return nullptr;
        }

        // Pop from free-list.
        std::uint8_t* base     = ManagerT::backend().base_ptr();
        std::uint8_t* slot_raw = base + static_cast<std::size_t>( _free_head_idx ) * granule_size;

        // Read the next free index from the slot.
        index_type next_free;
        std::memcpy( &next_free, slot_raw, sizeof( index_type ) );

        _free_head_idx = next_free;
        ++_total_allocated;

        // Zero the slot before returning.
        std::memset( slot_raw, 0, slot_bytes );

        return reinterpret_cast<T*>( slot_raw );
    }

    /**
     * @brief Deallocate one object back to the pool — O(1).
     *
     * The pointer must have been returned by a previous allocate() call on this pool.
     * After deallocation, the memory is returned to the free-list for reuse.
     *
     * @param ptr Pointer to the object to deallocate. Must not be nullptr.
     */
    void deallocate( T* ptr ) noexcept
    {
        if ( ptr == nullptr )
            return;

        std::uint8_t* base     = ManagerT::backend().base_ptr();
        std::uint8_t* slot_raw = reinterpret_cast<std::uint8_t*>( ptr );

        // Compute the granule index of this slot.
        std::size_t byte_off = static_cast<std::size_t>( slot_raw - base );
        index_type  slot_idx = static_cast<index_type>( byte_off / granule_size );

        // Push onto free-list: store current free_head in the slot.
        std::memcpy( slot_raw, &_free_head_idx, sizeof( index_type ) );

        _free_head_idx = slot_idx;
        --_total_allocated;
    }

    /**
     * @brief Free all chunks allocated by this pool.
     *
     * All slots (both free and allocated) become invalid after this call.
     * This method MUST be called before destroy_typed(pptr) for correct resource cleanup.
     */
    void free_all() noexcept
    {
        std::uint8_t* base = ManagerT::backend().base_ptr();

        // Walk the chunk list and deallocate each chunk.
        index_type chunk_idx = _chunk_head_idx;
        while ( chunk_idx != static_cast<index_type>( 0 ) )
        {
            std::uint8_t* chunk_raw = base + static_cast<std::size_t>( chunk_idx ) * granule_size;

            // Read the next chunk index from the chunk header.
            index_type next_chunk;
            std::memcpy( &next_chunk, chunk_raw, sizeof( index_type ) );

            // Deallocate this chunk.
            ManagerT::deallocate( chunk_raw );

            chunk_idx = next_chunk;
        }

        _free_head_idx   = static_cast<index_type>( 0 );
        _chunk_head_idx  = static_cast<index_type>( 0 );
        _total_allocated = 0;
        _total_capacity  = 0;
    }

  private:
    // --- Internal helpers -------------------------------------------------------

    /**
     * @brief Allocate a new chunk from the manager and add all its slots to the free-list.
     *
     * Chunk layout (all granule-aligned):
     *   [chunk_header: 1 granule (next_chunk_idx)] [slot_0] [slot_1] ... [slot_{n-1}]
     *
     * Each slot is `granules_per_slot` granules. The header is 1 granule.
     *
     * @return true on success, false on allocation failure.
     */
    bool allocate_chunk() noexcept
    {
        std::size_t n_objects = static_cast<std::size_t>( _objects_per_chunk );

        // Chunk = 1 header granule + n_objects * granules_per_slot granules.
        std::size_t total_granules = 1 + n_objects * granules_per_slot;
        std::size_t alloc_size     = total_granules * granule_size;

        // Overflow check.
        if ( n_objects > 0 && ( alloc_size / granule_size - 1 ) / granules_per_slot != n_objects )
            return false;

        void* raw = ManagerT::allocate( alloc_size );
        if ( raw == nullptr )
            return false;

        std::uint8_t* chunk_raw = static_cast<std::uint8_t*>( raw );
        std::uint8_t* base      = ManagerT::backend().base_ptr();

        // Compute chunk granule index.
        std::size_t chunk_byte_off = static_cast<std::size_t>( chunk_raw - base );
        index_type  chunk_idx      = static_cast<index_type>( chunk_byte_off / granule_size );

        // Write chunk header: link to previous chunk head.
        // Zero the full header granule first, then write the index.
        std::memset( chunk_raw, 0, granule_size );
        std::memcpy( chunk_raw, &_chunk_head_idx, sizeof( index_type ) );
        _chunk_head_idx = chunk_idx;

        // Initialize all slots and link them into the free-list.
        // Slots start 1 granule after the chunk start.
        std::uint8_t* slots_start = chunk_raw + granule_size;

        for ( std::size_t i = 0; i < n_objects; ++i )
        {
            std::uint8_t* slot          = slots_start + i * slot_bytes;
            std::size_t   slot_byte_off = static_cast<std::size_t>( slot - base );
            index_type    slot_idx      = static_cast<index_type>( slot_byte_off / granule_size );

            // Store current free_head in the slot (push onto free-list).
            std::memset( slot, 0, slot_bytes );
            std::memcpy( slot, &_free_head_idx, sizeof( index_type ) );
            _free_head_idx = slot_idx;
        }

        _total_capacity += _objects_per_chunk;
        return true;
    }
};

} // namespace pmm
