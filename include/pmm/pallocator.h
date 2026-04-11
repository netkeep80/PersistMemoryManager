/**
 * @file pmm/pallocator.h
 * @brief pallocator<T, ManagerT> — STL-compatible allocator for persistent address space.
 *
 * Implements an allocator that satisfies std::allocator_traits requirements,
 * delegating allocation/deallocation to a PersistMemoryManager instance.
 * This allows using STL containers with persistent memory:
 *
 * @code
 *   using Mgr = pmm::presets::SingleThreadedHeap;
 *   Mgr::create(64 * 1024);
 *
 *   // Use STL vector with persistent allocator
 *   std::vector<int, Mgr::pallocator<int>> vec;
 *   vec.push_back(42);
 *   vec.push_back(100);
 *
 *   // Elements are stored in the persistent address space
 *   assert(vec[0] == 42);
 *   assert(vec[1] == 100);
 *
 *   vec.clear();
 *   vec.shrink_to_fit();  // deallocates the data block
 *   Mgr::destroy();
 * @endcode
 *
 * Key properties:
 *   - Satisfies Allocator named requirements for use with STL containers.
 *   - allocate(n) delegates to ManagerT::allocate(n * sizeof(T)).
 *   - deallocate(p, n) delegates to ManagerT::deallocate(p).
 *   - Stateless: all instances with the same ManagerT are interchangeable.
 *   - propagate_on_container traits default to std::true_type (stateless allocator).
 *   - Throws std::bad_alloc on allocation failure (STL containers expect this).
 *
 * @warning The allocated memory resides in the persistent address space (PAP).
 *   Raw pointers returned by allocate() are only valid while the manager is
 *   initialized and the PAP is mapped at the same base address. Do NOT store
 *   raw pointers across manager destroy/load cycles — use pptr<T> for persistence.
 *
 * @see persist_memory_manager.h — PersistMemoryManager (static model)
 * @see pptr.h — pptr<T, ManagerT> (persistent pointer)
 * @see parray.h — parray<T, ManagerT> (persistent dynamic array with O(1) indexing)
 * @version 0.1
 */

#pragma once

#include <cstddef>
#include <limits>
#include <new>

namespace pmm
{

/**
 * @brief STL-compatible allocator backed by PersistMemoryManager.
 *
 * pallocator<T, ManagerT> delegates memory allocation and deallocation to a
 * PersistMemoryManager, allowing STL containers to store their data in the
 * persistent address space (PAP).
 *
 * The allocator is stateless — all state is held in the static ManagerT.
 * Two pallocator instances with the same ManagerT are always equal.
 *
 * @tparam T        Element type.
 * @tparam ManagerT Memory manager type (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename T, typename ManagerT> struct pallocator
{
    // --- Standard allocator type aliases (required by std::allocator_traits) ---

    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::true_type;

    // --- Constructors ---

    /// @brief Default constructor.
    constexpr pallocator() noexcept = default;

    /// @brief Copy constructor (trivial — allocator is stateless).
    constexpr pallocator( const pallocator& ) noexcept = default;

    /**
     * @brief Converting constructor from pallocator with a different value type.
     *
     * Required by std::allocator_traits for rebinding (e.g., std::list allocates
     * nodes of a different type than the value type).
     *
     * @tparam U The other value type.
     */
    template <typename U> constexpr pallocator( const pallocator<U, ManagerT>& ) noexcept {}

    // --- Allocation ---

    /**
     * @brief Allocate memory for n objects of type T.
     *
     * Delegates to ManagerT::allocate(n * sizeof(T)). If the manager cannot
     * satisfy the request, throws std::bad_alloc (as required by the Allocator
     * named requirements).
     *
     * @param n Number of objects to allocate space for. Must be > 0.
     * @return Pointer to the allocated memory.
     * @throws std::bad_alloc if allocation fails.
     */
    [[nodiscard]] T* allocate( std::size_t n )
    {
        if ( n == 0 )
            throw std::bad_alloc();

        // Overflow check.
        if ( n > max_size() )
            throw std::bad_alloc();

        void* raw = ManagerT::allocate( n * sizeof( T ) );
        if ( raw == nullptr )
            throw std::bad_alloc();

        return static_cast<T*>( raw );
    }

    /**
     * @brief Deallocate memory previously allocated by allocate().
     *
     * Delegates to ManagerT::deallocate(p). The count n is ignored (pmm tracks
     * block sizes internally).
     *
     * @param p   Pointer previously returned by allocate().
     * @param n   Number of objects (ignored — pmm tracks block size).
     */
    void deallocate( T* p, std::size_t /*n*/ ) noexcept { ManagerT::deallocate( static_cast<void*>( p ) ); }

    // --- Size limit ---

    /**
     * @brief Maximum number of objects that can theoretically be allocated.
     *
     * @return Upper bound on allocation size.
     */
    std::size_t max_size() const noexcept { return ( std::numeric_limits<std::size_t>::max )() / sizeof( T ); }

    // --- Comparison (all pallocators with the same ManagerT are equal) ---

    template <typename U> bool operator==( const pallocator<U, ManagerT>& ) const noexcept { return true; }

    template <typename U> bool operator!=( const pallocator<U, ManagerT>& ) const noexcept { return false; }
};

} // namespace pmm
