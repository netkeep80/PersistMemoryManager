/**
 * @file pmm/typed_guard.h
 * @brief RAII scope-guard for persistent containers.
 *
 * Provides typed_guard<T, ManagerT> — an RAII wrapper that automatically calls
 * the container's cleanup method (free_data() or free_all()) and then
 * ManagerT::destroy_typed() when the guard goes out of scope.
 *
 * This prevents resource leaks when users forget to call free_data()/free_all()
 * before destroy_typed().
 *
 * Usage:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(64 * 1024);
 *
 *   {
 *       auto guard = Mgr::make_guard<Mgr::pstring>();
 *       guard->assign("hello");
 *       // ...
 *   } // free_data() + destroy_typed() called automatically
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see pstring.h, parray.h, ppool.h — containers requiring explicit cleanup
 * @version 0.1
 */

#pragma once

#include <type_traits>
#include <utility>

namespace pmm
{

// ─── Concepts for cleanup method detection ──────────────────────

/// @brief Detects types with a free_data() method (pstring, parray).
template <typename T>
concept HasFreeData = requires( T& t ) {
    { t.free_data() } noexcept;
};

/// @brief Detects types with a free_all() method (ppool).
template <typename T>
concept HasFreeAll = requires( T& t ) {
    { t.free_all() } noexcept;
};

/// @brief Detects types that need cleanup before destroy_typed().
template <typename T>
concept HasPersistentCleanup = HasFreeData<T> || HasFreeAll<T>;

/**
 * @brief RAII scope-guard for persistent typed objects.
 *
 * Calls the appropriate cleanup method (free_data() or free_all()) and then
 * ManagerT::destroy_typed() when the guard goes out of scope.
 *
 * @tparam T        The persistent container type (e.g., pstring, parray, ppool).
 * @tparam ManagerT The PersistMemoryManager type.
 */
template <typename T, typename ManagerT> class typed_guard
{
  public:
    using pptr_type = typename ManagerT::template pptr<T>;

    /// @brief Construct a guard owning the given persistent pointer.
    explicit typed_guard( pptr_type p ) noexcept : _ptr( p ) {}

    /// @brief Default-construct an empty guard (null pointer).
    typed_guard() noexcept = default;

    typed_guard( const typed_guard& )            = delete;
    typed_guard& operator=( const typed_guard& ) = delete;

    typed_guard( typed_guard&& other ) noexcept : _ptr( other._ptr ) { other._ptr = pptr_type(); }

    typed_guard& operator=( typed_guard&& other ) noexcept
    {
        if ( this != &other )
        {
            reset();
            _ptr       = other._ptr;
            other._ptr = pptr_type();
        }
        return *this;
    }

    ~typed_guard() { reset(); }

    /// @brief Release ownership and clean up resources.
    void reset() noexcept
    {
        if ( !_ptr.is_null() )
        {
            cleanup( *_ptr );
            ManagerT::destroy_typed( _ptr );
            _ptr = pptr_type();
        }
    }

    /// @brief Release ownership without cleanup. Returns the raw pptr.
    pptr_type release() noexcept
    {
        pptr_type p = _ptr;
        _ptr        = pptr_type();
        return p;
    }

    /// @brief Access the managed object.
    T* operator->() const noexcept { return &( *_ptr ); }
    T& operator*() const noexcept { return *_ptr; }

    /// @brief Get the underlying pptr.
    pptr_type get() const noexcept { return _ptr; }

    /// @brief Check if the guard owns a valid object.
    explicit operator bool() const noexcept { return !_ptr.is_null(); }

  private:
    pptr_type _ptr;

    /// @brief Call the appropriate cleanup method based on the container type.
    static void cleanup( T& obj ) noexcept
    {
        if constexpr ( HasFreeData<T> )
            obj.free_data();
        else if constexpr ( HasFreeAll<T> )
            obj.free_all();
        // Types without cleanup methods are simply destroyed via destroy_typed().
    }
};

} // namespace pmm
