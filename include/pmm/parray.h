/**
 * @file pmm/parray.h
 * @brief parray<T, ManagerT> — persistent dynamic array with O(1) random access (Issue #195, Phase 3.2).
 *
 * Implements a dynamic array in the persistent address space (PAP).
 * Unlike pvector<T> (AVL-tree based, O(log n) random access), parray provides
 * O(1) indexed access via a contiguous data block, similar to std::vector.
 *
 * Key properties:
 *   - O(1) random access: at(i) / operator[] resolve directly to the i-th element.
 *   - Amortized O(1) push_back: capacity doubles on growth.
 *   - Data stored in a separate contiguous block in PAP.
 *   - POD-structure: all fields are primitive types (trivially copyable),
 *     enabling direct serialization in PAP.
 *   - Persistence: granule indices are address-independent across PAP reloads.
 *   - Element type T must be trivially copyable (required for PAP persistence).
 *
 * Usage:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(64 * 1024);
 *
 *   // Create a persistent array
 *   Mgr::pptr<Mgr::parray<int>> p = Mgr::create_typed<Mgr::parray<int>>();
 *
 *   // Add elements
 *   p->push_back(10);
 *   p->push_back(20);
 *   p->push_back(30);
 *
 *   // O(1) random access
 *   int* elem = p->at(1);    // points to 20
 *   int  val  = (*p)[0];     // 10
 *
 *   // Query
 *   std::size_t n = p->size();    // 3
 *   bool empty    = p->empty();   // false
 *
 *   // Remove last element
 *   p->pop_back();               // size = 2
 *
 *   // Pre-allocate capacity
 *   p->reserve(100);
 *
 *   // Free data and destroy
 *   p->free_data();
 *   Mgr::destroy_typed(p);
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see pvector.h  — pvector<T, ManagerT> (AVL-tree based persistent vector)
 * @see pstring.h  — pstring<ManagerT> (mutable persistent string)
 * @see persist_memory_manager.h — PersistMemoryManager (static model)
 * @see pptr.h — pptr<T, ManagerT> (persistent pointer)
 * @version 0.1 (Issue #195 — Phase 3.2: persistent array with O(1) indexing)
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
 * @brief Persistent dynamic array with O(1) random access (Issue #195, Phase 3.2).
 *
 * Stores a header (size, capacity, data block index) in PAP.
 * Element data is stored in a separate contiguous block allocated via the manager.
 *
 * A parray object is created in PAP via create_typed<parray<T>>() and destroyed
 * via destroy_typed() after calling free_data().
 *
 * Invariants:
 *   - If _data_idx != 0, the data block contains _size elements of type T.
 *   - _capacity >= _size (there is room for at least _size elements).
 *   - When _data_idx == 0, the array is empty.
 *   - Element type T must be trivially copyable.
 *
 * Layout in PAP:
 * @code
 *   parray<T> (in PAP)           Data block (in PAP)
 *   +----------------+           +--------------------------+
 *   | _size:  u32    |           | T[0] | T[1] | ... | T[n]|
 *   | _capacity: u32 |---idx---> |                          |
 *   | _data_idx      |           +--------------------------+
 *   +----------------+
 * @endcode
 *
 * @tparam T        Element type. Must be trivially copyable for PAP persistence.
 * @tparam ManagerT Memory manager type (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename T, typename ManagerT> struct parray
{
    static_assert( std::is_trivially_copyable_v<T>, "parray<T>: T must be trivially copyable for PAP persistence" );

    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using value_type   = T;

    std::uint32_t _size;     ///< Number of elements currently stored.
    std::uint32_t _capacity; ///< Capacity of the data block (number of T elements).
    index_type    _data_idx; ///< Granule index of the data block (0 = no data).

    // --- Constructor / Destructor -----------------------------------------------

    /// @brief Default constructor — empty array.
    parray() noexcept : _size( 0 ), _capacity( 0 ), _data_idx( static_cast<index_type>( 0 ) ) {}

    /// @brief Destructor — trivial (data is freed via free_data()).
    ~parray() noexcept = default;

    // --- Read access ------------------------------------------------------------

    /// @brief Number of elements in the array.
    std::size_t size() const noexcept { return static_cast<std::size_t>( _size ); }

    /// @brief Check if the array is empty.
    bool empty() const noexcept { return _size == 0; }

    /// @brief Current capacity (number of elements that fit without reallocation).
    std::size_t capacity() const noexcept { return static_cast<std::size_t>( _capacity ); }

    /**
     * @brief Access element by index with bounds checking.
     *
     * @param i Index of the element (0-based).
     * @return Pointer to the element, or nullptr if index is out of range or no data.
     */
    T* at( std::size_t i ) noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return nullptr;
        T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }

    /// @brief Const version of at().
    const T* at( std::size_t i ) const noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return nullptr;
        const T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }

    /**
     * @brief Access element by index without bounds checking.
     *
     * @param i Index of the element.
     * @return Copy of the element. If data is not resolved, returns T{}.
     */
    T operator[]( std::size_t i ) const noexcept
    {
        const T* data = resolve_data();
        return ( data != nullptr ) ? data[i] : T{};
    }

    /**
     * @brief Access the first element.
     *
     * @return Pointer to the first element, or nullptr if empty.
     */
    T* front() noexcept { return at( 0 ); }

    /// @brief Const version of front().
    const T* front() const noexcept { return at( 0 ); }

    /**
     * @brief Access the last element.
     *
     * @return Pointer to the last element, or nullptr if empty.
     */
    T* back() noexcept { return ( _size > 0 ) ? at( static_cast<std::size_t>( _size ) - 1 ) : nullptr; }

    /// @brief Const version of back().
    const T* back() const noexcept { return ( _size > 0 ) ? at( static_cast<std::size_t>( _size ) - 1 ) : nullptr; }

    /**
     * @brief Get a raw pointer to the underlying data block.
     *
     * @return Pointer to the first element, or nullptr if empty.
     */
    T* data() noexcept { return resolve_data(); }

    /// @brief Const version of data().
    const T* data() const noexcept { return resolve_data(); }

    // --- Mutating operations ----------------------------------------------------

    /**
     * @brief Add an element to the end of the array.
     *
     * If capacity is insufficient, reallocates with doubled capacity (amortized O(1)).
     *
     * @param value The element to add.
     * @return true on success, false on allocation failure.
     */
    bool push_back( const T& value ) noexcept
    {
        if ( !ensure_capacity( _size + 1 ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        d[_size] = value;
        ++_size;
        return true;
    }

    /**
     * @brief Remove the last element from the array.
     *
     * Does nothing if the array is empty. Does not shrink the data block.
     */
    void pop_back() noexcept
    {
        if ( _size > 0 )
            --_size;
    }

    /**
     * @brief Set the element at the given index.
     *
     * @param i     Index of the element (must be < size()).
     * @param value New value.
     * @return true on success, false if index is out of range.
     */
    bool set( std::size_t i, const T& value ) noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        d[i] = value;
        return true;
    }

    /**
     * @brief Reserve capacity for at least n elements.
     *
     * If current capacity is already >= n, does nothing.
     *
     * @param n Desired capacity.
     * @return true on success, false on allocation failure.
     */
    bool reserve( std::size_t n ) noexcept
    {
        if ( n > static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) )
            return false;
        return ensure_capacity( static_cast<std::uint32_t>( n ) );
    }

    /**
     * @brief Resize the array to contain n elements.
     *
     * If n > size(), new elements are value-initialized (T{}).
     * If n < size(), excess elements are discarded.
     *
     * @param n New size.
     * @return true on success, false on allocation failure.
     */
    bool resize( std::size_t n ) noexcept
    {
        if ( n > static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) )
            return false;
        auto new_size = static_cast<std::uint32_t>( n );
        if ( new_size > _size )
        {
            if ( !ensure_capacity( new_size ) )
                return false;
            T* d = resolve_data();
            if ( d == nullptr )
                return false;
            // Zero-initialize new elements.
            std::memset( d + _size, 0, static_cast<std::size_t>( new_size - _size ) * sizeof( T ) );
        }
        _size = new_size;
        return true;
    }

    /**
     * @brief Clear the array (set size to 0) without freeing the data block.
     *
     * Capacity is preserved for potential reuse.
     */
    void clear() noexcept { _size = 0; }

    /**
     * @brief Free the data block.
     *
     * Deallocates the data block via the manager. After calling, the array is empty.
     * This method MUST be called before destroy_typed(pptr) for correct resource cleanup.
     */
    void free_data() noexcept
    {
        if ( _data_idx != static_cast<index_type>( 0 ) )
        {
            std::uint8_t* base = ManagerT::backend().base_ptr();
            void*         raw  = base + static_cast<std::size_t>( _data_idx ) * ManagerT::address_traits::granule_size;
            ManagerT::deallocate( raw );
            _data_idx = static_cast<index_type>( 0 );
        }
        _size     = 0;
        _capacity = 0;
    }

    // --- Comparison operators ---------------------------------------------------

    /// @brief Equality: same size and all elements equal.
    bool operator==( const parray& other ) const noexcept
    {
        if ( this == &other )
            return true;
        if ( _size != other._size )
            return false;
        if ( _size == 0 )
            return true;
        const T* a = resolve_data();
        const T* b = other.resolve_data();
        if ( a == nullptr || b == nullptr )
            return ( a == b );
        return std::memcmp( a, b, static_cast<std::size_t>( _size ) * sizeof( T ) ) == 0;
    }

    /// @brief Inequality.
    bool operator!=( const parray& other ) const noexcept { return !( *this == other ); }

  private:
    // --- Internal helpers -------------------------------------------------------

    /// @brief Resolve the granule index to a raw pointer to the data block.
    T* resolve_data() const noexcept
    {
        if ( _data_idx == static_cast<index_type>( 0 ) )
            return nullptr;
        std::uint8_t* base = ManagerT::backend().base_ptr();
        return reinterpret_cast<T*>( base +
                                     static_cast<std::size_t>( _data_idx ) * ManagerT::address_traits::granule_size );
    }

    /**
     * @brief Ensure the data block can hold at least `required` elements.
     *
     * If current capacity is sufficient, does nothing.
     * Otherwise, allocates a new block with doubled capacity (amortized O(1)),
     * copies old data, and frees the old block.
     *
     * @param required Required number of elements.
     * @return true on success, false on allocation failure.
     */
    bool ensure_capacity( std::uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;

        // New capacity: double current or required, whichever is larger.
        // Minimum 4 elements to avoid frequent reallocations for small arrays.
        std::uint32_t new_cap = _capacity * 2;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap < 4 )
            new_cap = 4;

        // Check for overflow in allocation size.
        std::size_t alloc_size = static_cast<std::size_t>( new_cap ) * sizeof( T );
        if ( sizeof( T ) > 0 && alloc_size / sizeof( T ) != static_cast<std::size_t>( new_cap ) )
            return false; // overflow

        void* new_raw = ManagerT::allocate( alloc_size );
        if ( new_raw == nullptr )
            return false;

        // Compute new index.
        std::uint8_t* base        = ManagerT::backend().base_ptr();
        std::size_t   byte_off    = static_cast<std::uint8_t*>( new_raw ) - base;
        index_type    new_dat_idx = static_cast<index_type>( byte_off / ManagerT::address_traits::granule_size );

        // Copy old data.
        if ( _size > 0 && _data_idx != static_cast<index_type>( 0 ) )
        {
            T* old_data = resolve_data();
            if ( old_data != nullptr )
                std::memcpy( new_raw, old_data, static_cast<std::size_t>( _size ) * sizeof( T ) );
        }

        // Free old block.
        if ( _data_idx != static_cast<index_type>( 0 ) )
        {
            void* old_raw = base + static_cast<std::size_t>( _data_idx ) * ManagerT::address_traits::granule_size;
            ManagerT::deallocate( old_raw );
        }

        _data_idx = new_dat_idx;
        _capacity = new_cap;
        return true;
    }
};

// parray<T, ManagerT> is a POD-structure for direct serialization in PAP.

} // namespace pmm
