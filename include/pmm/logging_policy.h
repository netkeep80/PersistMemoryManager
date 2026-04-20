/**
 * @file pmm/logging_policy.h
 * @brief Политики логирования для PersistMemoryManager.
 *
 * Содержит:
 *   - pmm::logging::NoLogging       — заглушка без логирования (по умолчанию, нулевые накладные расходы)
 *   - pmm::logging::StderrLogging   — логирование ошибок и событий в stderr
 *
 * Хуки вызываются менеджером при ключевых событиях:
 *   - on_allocation_failure(user_size, err) — не удалось выделить память
 *   - on_expand(old_size, new_size)         — бэкенд расширен
 *   - on_corruption_detected(err)           — обнаружено повреждение данных (InvalidMagic, CrcMismatch и т.д.)
 *   - on_create(initial_size)               — менеджер успешно создан
 *   - on_destroy()                          — менеджер сброшен
 *   - on_load()                             — менеджер загружен из образа
 *
 * Все методы — static noexcept. Политика NoLogging полностью оптимизируется
 * компилятором (все методы пустые inline), обеспечивая нулевые накладные расходы
 * для конфигураций без логирования.
 *
 * Пример пользовательской политики:
 * @code
 *   struct MyLogging {
 *       static void on_allocation_failure(std::size_t user_size, pmm::PmmError err) noexcept {
 *           spdlog::warn("pmm: allocate({}) failed: {}", user_size, static_cast<int>(err));
 *       }
 *       static void on_expand(std::size_t old_size, std::size_t new_size) noexcept {
 *           spdlog::info("pmm: expanded {} -> {}", old_size, new_size);
 *       }
 *       static void on_corruption_detected(pmm::PmmError err) noexcept {
 *           spdlog::error("pmm: corruption: {}", static_cast<int>(err));
 *       }
 *       static void on_create(std::size_t size) noexcept {}
 *       static void on_destroy() noexcept {}
 *       static void on_load() noexcept {}
 *   };
 * @endcode
 *
 * @see config.h — политики блокировок (аналогичный паттерн)
 * @see persist_memory_manager.h — PersistMemoryManager (вызывает хуки)
 * @version 1.0
 */

#pragma once

#include "pmm/types.h"

#include <cstddef>
#include <cstdio>

namespace pmm
{
namespace logging
{

/// @brief Политика без логирования (нулевые накладные расходы, по умолчанию).
///
/// Все методы — пустые inline, полностью оптимизируются компилятором.
struct NoLogging
{
    /// @brief Вызывается при неудачной аллокации.
    /// @param user_size Запрошенный размер в байтах.
    /// @param err Код ошибки (OutOfMemory, Overflow, InvalidSize, NotInitialized).
    static void on_allocation_failure( std::size_t /*user_size*/, PmmError /*err*/ ) noexcept {}

    /// @brief Вызывается после успешного расширения бэкенда.
    /// @param old_size Старый размер в байтах.
    /// @param new_size Новый размер в байтах.
    static void on_expand( std::size_t /*old_size*/, std::size_t /*new_size*/ ) noexcept {}

    /// @brief Вызывается при обнаружении повреждения данных.
    /// @param err Код ошибки (InvalidMagic, CrcMismatch, SizeMismatch, GranuleMismatch, UnsupportedImageVersion).
    static void on_corruption_detected( PmmError /*err*/ ) noexcept {}

    /// @brief Вызывается после успешного создания менеджера.
    /// @param initial_size Начальный размер в байтах.
    static void on_create( std::size_t /*initial_size*/ ) noexcept {}

    /// @brief Вызывается при сбросе менеджера (destroy()).
    static void on_destroy() noexcept {}

    /// @brief Вызывается после успешной загрузки образа (load()).
    static void on_load() noexcept {}
};

/// @brief Политика логирования в stderr.
///
/// Выводит ошибки и ключевые события менеджера в стандартный поток ошибок.
/// Полезна для отладки и диагностики.
///
/// @code
///   using MyConfig = pmm::BasicConfig<
///       pmm::DefaultAddressTraits,
///       pmm::config::NoLock,
///       5, 4, 64,
///       pmm::logging::StderrLogging  // включить логирование в stderr
///   >;
///   using MyMgr = pmm::PersistMemoryManager<MyConfig>;
/// @endcode
struct StderrLogging
{
    static void on_allocation_failure( std::size_t user_size, PmmError err ) noexcept
    {
        std::fprintf( stderr, "[pmm] allocation_failure: size=%zu error=%d\n", user_size, static_cast<int>( err ) );
    }

    static void on_expand( std::size_t old_size, std::size_t new_size ) noexcept
    {
        std::fprintf( stderr, "[pmm] expand: %zu -> %zu\n", old_size, new_size );
    }

    static void on_corruption_detected( PmmError err ) noexcept
    {
        std::fprintf( stderr, "[pmm] corruption_detected: error=%d\n", static_cast<int>( err ) );
    }

    static void on_create( std::size_t initial_size ) noexcept
    {
        std::fprintf( stderr, "[pmm] create: size=%zu\n", initial_size );
    }

    static void on_destroy() noexcept { std::fprintf( stderr, "[pmm] destroy\n" ); }

    static void on_load() noexcept { std::fprintf( stderr, "[pmm] load\n" ); }
};

} // namespace logging
} // namespace pmm
