/**
 * @file pmm/manager_configs.h
 * @brief Готовые конфигурационные структуры для менеджеров ПАП (Issue #100, #110, #146).
 *
 * Предоставляет набор предопределённых конфигурационных структур для использования
 * с `PersistMemoryManager<ConfigT, InstanceId>`. Каждая конфигурация описывает
 * типичный сценарий использования менеджера персистентной памяти.
 *
 * Конфигурация включает:
 *   - `address_traits`   — тип адресного пространства (размер индекса, гранулы)
 *   - `storage_backend`  — бэкенд хранилища (HeapStorage, StaticStorage, MMapStorage)
 *   - `free_block_tree`  — политика дерева свободных блоков (AvlFreeTree)
 *   - `lock_policy`      — политика многопоточности (NoLock, SharedMutexLock)
 *   - `granule_size`     — размер гранулы в байтах
 *   - `max_memory_gb`    — максимальный объём памяти в ГБ
 *   - `grow_numerator` / `grow_denominator` — коэффициент роста хранилища
 *
 * Правила выбора конфигурации (Issue #146):
 *
 *   Поддерживаемые размеры индекса:
 *     - uint16_t (SmallAddressTraits,  16B гранула) — до ~1 МБ, малые embedded-системы.
 *     - uint32_t (DefaultAddressTraits, 16B гранула) — до 64 ГБ, основной вариант.
 *     - uint64_t (LargeAddressTraits,  64B гранула) — до петабайт, крупные БД.
 *
 *   Ключевые ограничения (задокументированы static_assert-ами):
 *     1. granule_size >= kMinGranuleSize (4 байта — минимум размер слова архитектуры).
 *     2. granule_size — степень двойки.
 *
 *   Архитектурные сценарии:
 *     - Tiny embedded (16-bit, без heap, статический пул до ~1 МБ):
 *         StaticStorage<N, SmallAddressTraits> + NoLock, гранула 16B.
 *         pptr<T> хранит uint16_t-индекс (2 байта).
 *     - Embedded (32-bit, без heap, статический пул):
 *         StaticStorage<N, DefaultAddressTraits> + NoLock, гранула 16B.
 *         pptr<T> хранит uint32_t-индекс (4 байта).
 *     - Desktop/server (32-bit, до 64 ГБ):
 *         HeapStorage<DefaultAddressTraits> + NoLock/SharedMutexLock, гранула 16B.
 *     - Industrial DB (32-bit, высоконагруженный):
 *         HeapStorage<DefaultAddressTraits> + SharedMutexLock + агрессивный рост, гранула 16B.
 *     - Large DB (64-bit, крупные базы данных):
 *         HeapStorage<LargeAddressTraits> + SharedMutexLock, гранула 64B.
 *         pptr<T> хранит uint64_t-индекс (8 байт).
 *
 * Доступные конфигурации:
 *   --- Embedded (статическое хранилище, однопоточный) ---
 *   - `SmallEmbeddedStaticConfig<N>` — StaticStorage<N>, NoLock, 16-bit индекс, 16B гранула
 *   - `EmbeddedStaticConfig<N>`      — StaticStorage<N>, NoLock, 32-bit индекс, 16B гранула
 *
 *   --- Desktop (динамическое хранилище, 32-bit) ---
 *   - `CacheManagerConfig`      — однопоточный, NoLock, HeapStorage, 16B, рост 25%
 *   - `PersistentDataConfig`    — многопоточный, SharedMutexLock, HeapStorage, 16B, рост 25%
 *   - `EmbeddedManagerConfig`   — однопоточный, NoLock, HeapStorage, 16B, рост 50%
 *
 *   --- Industrial DB (высокая нагрузка, 32-bit) ---
 *   - `IndustrialDBConfig`      — многопоточный, SharedMutexLock, HeapStorage, 16B, рост 100%
 *
 *   --- Large DB (крупные базы данных, 64-bit) ---
 *   - `LargeDBConfig`           — многопоточный, SharedMutexLock, HeapStorage, 64B, рост 100%
 *
 * Пример использования:
 * @code
 *   // Кеш-менеджер (однопоточный, 64 МБ)
 *   using AppCache = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   AppCache::create(64 * 1024 * 1024);
 *   AppCache::pptr<int> ptr = AppCache::allocate_typed<int>();
 *   *ptr = 42;
 *
 *   // Embedded-менеджер с фиксированным пулом 8 КБ (без heap, 32-bit)
 *   using EmbMgr = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<8192>>;
 *   EmbMgr::create(8192);
 *   void* p = EmbMgr::allocate(64);
 *
 *   // Tiny embedded-менеджер (16-bit индекс, до ~1 МБ)
 *   using TinyMgr = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>>;
 *   TinyMgr::create(1024);
 *   void* p = TinyMgr::allocate(32);
 *
 *   // Крупная база данных (64-bit индекс, петабайтный масштаб)
 *   using BigDB = pmm::PersistMemoryManager<pmm::LargeDBConfig>;
 *   BigDB::create(256 * 1024 * 1024);
 *   void* p = BigDB::allocate(4096);
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager (Issue #110)
 * @see config.h — базовые политики блокировок (NoLock, SharedMutexLock)
 * @version 0.4 (Issue #146 — добавлена поддержка 16-bit и 64-bit индексов)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/config.h"
#include "pmm/free_block_tree.h"
#include "pmm/heap_storage.h"
#include "pmm/static_storage.h"
#include "pmm/storage_backend.h"

namespace pmm
{

// ─── Правила для гранул (Issue #146) ─────────────────────────────────────────

/// @brief Минимальный допустимый размер гранулы (размер слова архитектуры = 4 байта).
inline constexpr std::size_t kMinGranuleSize = 4;

// ─── Embedded / статические конфигурации ─────────────────────────────────────

/**
 * @brief Конфигурация tiny-embedded-менеджера со статическим буфером и 16-bit индексом.
 *
 * Предназначена для сверхмалых систем без heap (микроконтроллеры, RTOS, bare-metal)
 * с ограничением памяти до ~1 МБ:
 *   - uint16_t индекс (SmallAddressTraits), 16-байтная гранула
 *   - pptr<T> хранит 2-байтный индекс (вместо 4 байт у DefaultAddressTraits)
 *   - StaticStorage<BufferSize, SmallAddressTraits> — фиксированный буфер, нет malloc
 *   - Максимальный пул: 65535 × 16 = ~1 МБ
 *   - Нет блокировок (NoLock) — только однопоточный контекст
 *   - Не расширяется (StaticStorage::expand() всегда false)
 *
 * Статические проверки:
 *   - granule_size >= kMinGranuleSize (16 >= 4) ✓
 *   - granule_size — степень двойки ✓
 *   - BufferSize кратно granule_size ✓ (проверяется в StaticStorage)
 *
 * Типичный сценарий: ARM Cortex-M, AVR, ESP32, сильно ограниченные embedded-системы.
 *
 * @tparam BufferSize Размер статического буфера в байтах (кратно 16, максимум ~1 МБ).
 *                    По умолчанию 1024 байт (1 КБ).
 *
 * @code
 *   using TinyMgr = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>>;
 *   TinyMgr::create(1024);
 *   void* ptr = TinyMgr::allocate(32);
 *   // sizeof(TinyMgr::pptr<int>) == 2  (16-bit индекс)
 * @endcode
 */
template <std::size_t BufferSize = 1024> struct SmallEmbeddedStaticConfig
{
    static_assert( SmallAddressTraits::granule_size >= kMinGranuleSize,
                   "SmallEmbeddedStaticConfig: granule_size must be >= kMinGranuleSize (4 bytes)" );
    static_assert( ( SmallAddressTraits::granule_size & ( SmallAddressTraits::granule_size - 1 ) ) == 0,
                   "SmallEmbeddedStaticConfig: granule_size must be a power of 2" );

    using address_traits                          = SmallAddressTraits;
    using storage_backend                         = StaticStorage<BufferSize, SmallAddressTraits>;
    using free_block_tree                         = AvlFreeTree<SmallAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = SmallAddressTraits::granule_size;
    static constexpr std::size_t max_memory_gb    = 0; // Нет расширения — StaticStorage
    static constexpr std::size_t grow_numerator   = 3;
    static constexpr std::size_t grow_denominator = 2;
};

/**
 * @brief Конфигурация embedded-менеджера со статическим фиксированным буфером.
 *
 * Предназначена для систем без heap (микроконтроллеры, RTOS, bare-metal):
 *   - uint32_t индекс (DefaultAddressTraits), 16-байтная гранула
 *   - StaticStorage<BufferSize> — фиксированный буфер в BSS/глобальной области, нет malloc
 *   - Нет блокировок (NoLock) — только однопоточный контекст
 *   - Не расширяется (StaticStorage::expand() всегда false)
 *
 * Статические проверки (Issue #146):
 *   - granule_size >= kMinGranuleSize (16 >= 4) ✓
 *   - granule_size — степень двойки ✓
 *   - BufferSize кратно granule_size ✓ (проверяется в StaticStorage)
 *
 * Типичный сценарий: встраиваемые системы без heap, Linux bare-metal, фиксированный пул.
 *
 * @tparam BufferSize Размер статического буфера в байтах (кратно 16).
 *                    По умолчанию 4096 байт (4 КБ).
 *
 * @code
 *   using EmbMgr = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<8192>>;
 *   EmbMgr::create(8192);
 *   void* ptr = EmbMgr::allocate(64);
 * @endcode
 */
template <std::size_t BufferSize = 4096> struct EmbeddedStaticConfig
{
    static_assert( DefaultAddressTraits::granule_size >= kMinGranuleSize,
                   "EmbeddedStaticConfig: granule_size must be >= kMinGranuleSize (4 bytes)" );
    static_assert( ( DefaultAddressTraits::granule_size & ( DefaultAddressTraits::granule_size - 1 ) ) == 0,
                   "EmbeddedStaticConfig: granule_size must be a power of 2" );

    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = StaticStorage<BufferSize, DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = DefaultAddressTraits::granule_size;
    static constexpr std::size_t max_memory_gb    = 0; // Нет расширения — StaticStorage
    static constexpr std::size_t grow_numerator   = 3;
    static constexpr std::size_t grow_denominator = 2;
};

// ─── Desktop / динамические конфигурации ─────────────────────────────────────

/**
 * @brief Конфигурация кеш-менеджера (однопоточный, heap, 16B гранула).
 *
 * Оптимизирован для временного кеша с однопоточным доступом:
 *   - Нет блокировок (NoLock) — максимальная производительность
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память с авторасширением
 *   - Коэффициент роста 5/4 (25%)
 *
 * Статические проверки (Issue #146):
 *   - granule_size >= kMinGranuleSize (16 >= 4) ✓
 *   - granule_size — степень двойки ✓
 *
 * Типичный сценарий: кеш вычислений, временные буферы в однопоточном коде.
 */
struct CacheManagerConfig
{
    static_assert( DefaultAddressTraits::granule_size >= kMinGranuleSize,
                   "CacheManagerConfig: granule_size must be >= kMinGranuleSize (4 bytes)" );
    static_assert( ( DefaultAddressTraits::granule_size & ( DefaultAddressTraits::granule_size - 1 ) ) == 0,
                   "CacheManagerConfig: granule_size must be a power of 2" );

    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = config::kDefaultGrowNumerator;
    static constexpr std::size_t grow_denominator = config::kDefaultGrowDenominator;
};

/**
 * @brief Конфигурация менеджера персистентных данных (многопоточный, heap, 16B гранула).
 *
 * Оптимизирован для хранения персистентных данных с многопоточным доступом:
 *   - SharedMutexLock — потокобезопасность
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Коэффициент роста 5/4 (25%)
 *
 * Статические проверки (Issue #146):
 *   - granule_size >= kMinGranuleSize (16 >= 4) ✓
 *   - granule_size — степень двойки ✓
 *
 * Типичный сценарий: долговременное хранение данных, файловые менеджеры.
 */
struct PersistentDataConfig
{
    static_assert( DefaultAddressTraits::granule_size >= kMinGranuleSize,
                   "PersistentDataConfig: granule_size must be >= kMinGranuleSize (4 bytes)" );
    static_assert( ( DefaultAddressTraits::granule_size & ( DefaultAddressTraits::granule_size - 1 ) ) == 0,
                   "PersistentDataConfig: granule_size must be a power of 2" );

    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::SharedMutexLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = config::kDefaultGrowNumerator;
    static constexpr std::size_t grow_denominator = config::kDefaultGrowDenominator;
};

/**
 * @brief Конфигурация embedded-менеджера с динамическим хранилищем.
 *
 * Оптимизирован для встраиваемых/ресурсоограниченных систем с heap:
 *   - Нет блокировок (NoLock) — минимальные накладные расходы
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Консервативный коэффициент роста 3/2 (50%) для экономии памяти
 *
 * Статические проверки (Issue #146):
 *   - granule_size >= kMinGranuleSize (16 >= 4) ✓
 *   - granule_size — степень двойки ✓
 *
 * Типичный сценарий: Linux embedded (RPi, etc.), системы с ограниченной памятью.
 */
struct EmbeddedManagerConfig
{
    static_assert( DefaultAddressTraits::granule_size >= kMinGranuleSize,
                   "EmbeddedManagerConfig: granule_size must be >= kMinGranuleSize (4 bytes)" );
    static_assert( ( DefaultAddressTraits::granule_size & ( DefaultAddressTraits::granule_size - 1 ) ) == 0,
                   "EmbeddedManagerConfig: granule_size must be a power of 2" );

    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = 3;
    static constexpr std::size_t grow_denominator = 2;
};

// ─── Industrial DB конфигурации ───────────────────────────────────────────────

/**
 * @brief Конфигурация промышленной базы данных (многопоточный, heap, 16B гранула, 32-bit).
 *
 * Оптимизирован для высоконагруженных промышленных систем:
 *   - SharedMutexLock — потокобезопасность с поддержкой конкурентного чтения
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Агрессивный коэффициент роста 2/1 (100%) для минимизации перевыделений
 *
 * Статические проверки (Issue #146):
 *   - granule_size >= kMinGranuleSize (16 >= 4) ✓
 *   - granule_size — степень двойки ✓
 *
 * Типичный сценарий: промышленные базы данных, time-series хранилища (до 64 ГБ).
 */
struct IndustrialDBConfig
{
    static_assert( DefaultAddressTraits::granule_size >= kMinGranuleSize,
                   "IndustrialDBConfig: granule_size must be >= kMinGranuleSize (4 bytes)" );
    static_assert( ( DefaultAddressTraits::granule_size & ( DefaultAddressTraits::granule_size - 1 ) ) == 0,
                   "IndustrialDBConfig: granule_size must be a power of 2" );

    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::SharedMutexLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = 2;
    static constexpr std::size_t grow_denominator = 1;
};

// ─── Large DB конфигурации (64-bit индекс) ────────────────────────────────────

/**
 * @brief Конфигурация крупной базы данных с 64-bit индексом (многопоточный, heap, 64B гранула).
 *
 * Предназначена для хранилищ петабайтного масштаба:
 *   - uint64_t индекс (LargeAddressTraits), 64-байтная гранула
 *   - pptr<T> хранит 8-байтный индекс — адресует до 2^64 × 64 байт памяти
 *   - SharedMutexLock — потокобезопасность с поддержкой конкурентного чтения
 *   - HeapStorage — динамическая память
 *   - Агрессивный коэффициент роста 2/1 (100%) для минимизации перевыделений
 *
 * Статические проверки:
 *   - granule_size >= kMinGranuleSize (64 >= 4) ✓
 *   - granule_size — степень двойки ✓
 *
 * Типичный сценарий: крупные базы данных, хранилища данных, облачные хранилища,
 * петабайтные time-series системы.
 *
 * @code
 *   using BigDB = pmm::PersistMemoryManager<pmm::LargeDBConfig>;
 *   BigDB::create(256 * 1024 * 1024); // 256 МБ начальный размер
 *   void* ptr = BigDB::allocate(4096);
 *   // sizeof(BigDB::pptr<int>) == 8  (64-bit индекс)
 * @endcode
 */
struct LargeDBConfig
{
    static_assert( LargeAddressTraits::granule_size >= kMinGranuleSize,
                   "LargeDBConfig: granule_size must be >= kMinGranuleSize (4 bytes)" );
    static_assert( ( LargeAddressTraits::granule_size & ( LargeAddressTraits::granule_size - 1 ) ) == 0,
                   "LargeDBConfig: granule_size must be a power of 2" );

    using address_traits                          = LargeAddressTraits;
    using storage_backend                         = HeapStorage<LargeAddressTraits>;
    using free_block_tree                         = AvlFreeTree<LargeAddressTraits>;
    using lock_policy                             = config::SharedMutexLock;
    static constexpr std::size_t granule_size     = LargeAddressTraits::granule_size;
    static constexpr std::size_t max_memory_gb    = 0; // Без ограничения (64-bit адресация)
    static constexpr std::size_t grow_numerator   = 2;
    static constexpr std::size_t grow_denominator = 1;
};

} // namespace pmm
