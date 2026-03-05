/**
 * @file pmm/heap_storage.h
 * @brief HeapStorage — динамический бэкенд хранилища ПАП (Issue #87 Phase 5).
 *
 * Управляет динамически выделяемой областью памяти через `std::malloc` / `std::free`.
 * Поддерживает расширение (expand()) путём выделения нового буфера и копирования данных.
 *
 * Применение: стандартный вариант для большинства приложений (рефакторинг expand()
 * из текущего `PersistMemoryManager`).
 *
 * @tparam AddressTraitsT Traits адресного пространства (из address_traits.h).
 *
 * @see plan_issue87.md §5 «Фаза 5: StorageBackend — HeapStorage»
 * @see storage_backend.h — концепт StorageBackend
 * @version 0.1 (Issue #87 Phase 5)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/storage_backend.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace pmm
{

/**
 * @brief Динамический бэкенд хранилища — malloc/free.
 *
 * @tparam AddressTraitsT Traits адресного пространства.
 */
template <typename AddressTraitsT = DefaultAddressTraits> class HeapStorage
{
  public:
    using address_traits = AddressTraitsT;

    HeapStorage() noexcept = default;

    /// @brief Конструктор с начальным размером.
    explicit HeapStorage( std::size_t initial_size ) noexcept
    {
        if ( initial_size == 0 )
            return;
        // Выравниваем по granule_size
        std::size_t aligned = ( ( initial_size + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size ) *
                              AddressTraitsT::granule_size;
        _buffer = static_cast<std::uint8_t*>( std::malloc( aligned ) );
        if ( _buffer != nullptr )
        {
            _size        = aligned;
            _owns_memory = true;
        }
    }

    HeapStorage( const HeapStorage& )            = delete;
    HeapStorage& operator=( const HeapStorage& ) = delete;

    HeapStorage( HeapStorage&& other ) noexcept
        : _buffer( other._buffer ), _size( other._size ), _owns_memory( other._owns_memory )
    {
        other._buffer      = nullptr;
        other._size        = 0;
        other._owns_memory = false;
    }

    HeapStorage& operator=( HeapStorage&& other ) noexcept
    {
        if ( this != &other )
        {
            if ( _owns_memory && _buffer != nullptr )
                std::free( _buffer );
            _buffer            = other._buffer;
            _size              = other._size;
            _owns_memory       = other._owns_memory;
            other._buffer      = nullptr;
            other._size        = 0;
            other._owns_memory = false;
        }
        return *this;
    }

    ~HeapStorage()
    {
        if ( _owns_memory && _buffer != nullptr )
            std::free( _buffer );
    }

    /// @brief Принять внешний буфер (менеджер не владеет им).
    void attach( void* memory, std::size_t size ) noexcept
    {
        if ( _owns_memory && _buffer != nullptr )
            std::free( _buffer );
        _buffer      = static_cast<std::uint8_t*>( memory );
        _size        = size;
        _owns_memory = false;
    }

    /// @brief Указатель на начало буфера.
    std::uint8_t*       base_ptr() noexcept { return _buffer; }
    const std::uint8_t* base_ptr() const noexcept { return _buffer; }

    /// @brief Текущий размер буфера в байтах.
    std::size_t total_size() const noexcept { return _size; }

    /**
     * @brief Расширить буфер на `additional_bytes` байт (минимум).
     *
     * Выделяет новый буфер с ростом >= additional_bytes, копирует данные.
     * Старый буфер освобождается (если owns_memory == true).
     *
     * @param additional_bytes  Минимальный прирост в байтах.
     * @return true при успехе, false при ошибке.
     */
    bool expand( std::size_t additional_bytes ) noexcept
    {
        if ( additional_bytes == 0 )
            return _size > 0;
        // Grow by 25% or by additional_bytes, whichever is larger
        // Handle initial allocation from zero (_size == 0)
        std::size_t growth   = ( _size > 0 ) ? ( _size / 4 + additional_bytes ) : additional_bytes;
        std::size_t new_size = _size + growth;
        // Align to granule_size
        new_size = ( ( new_size + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size ) *
                   AddressTraitsT::granule_size;
        if ( new_size <= _size )
            return false;

        void* new_buf = std::malloc( new_size );
        if ( new_buf == nullptr )
            return false;
        if ( _buffer != nullptr )
            std::memcpy( new_buf, _buffer, _size );
        if ( _owns_memory && _buffer != nullptr )
            std::free( _buffer );
        _buffer      = static_cast<std::uint8_t*>( new_buf );
        _size        = new_size;
        _owns_memory = true;
        return true;
    }

    /// @brief HeapStorage владеет памятью, если она была выделена через malloc.
    bool owns_memory() const noexcept { return _owns_memory; }

  private:
    std::uint8_t* _buffer      = nullptr;
    std::size_t   _size        = 0;
    bool          _owns_memory = false;
};

// ─── static_assert: HeapStorage соответствует концепту StorageBackend ──────────

static_assert( is_storage_backend_v<HeapStorage<>>, "HeapStorage must satisfy StorageBackendConcept" );

} // namespace pmm
