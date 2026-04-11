/**
 * @file pmm/manager_configs.h
 * @brief Готовые конфигурационные структуры для менеджеров ПАП.
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
 *   - `logging_policy`   — политика логирования (NoLogging, StderrLogging)
 *   - `granule_size`     — размер гранулы в байтах
 *   - `max_memory_gb`    — максимальный объём памяти в ГБ
 *   - `grow_numerator` / `grow_denominator` — коэффициент роста хранилища
 *
 * Правила выбора конфигурации:
 *
 *   Поддерживаемые размеры индекса:
 *     - uint16_t (SmallAddressTraits,   16B гранула) — до ~1 МБ, малые embedded-системы.
 *     - uint32_t (DefaultAddressTraits, 16B гранула) — до 64 ГБ, основной вариант.
 *     - uint64_t (LargeAddressTraits,   64B гранула) — до петабайт, крупные БД.
 *
 *   Ключевые ограничения (проверяются через концепт ValidPmmAddressTraits):
 *     1. granule_size >= kMinGranuleSize (4 байта — минимум размер слова архитектуры).
 *     2. granule_size — степень двойки.
 *
 *   Рекомендации по выбору гранулы:
 *     - Для минимального расхода памяти используйте конфигурации без потерь:
 *       DefaultAddressTraits (Block=32B / 16B гранула = 0 байт потерь на блок),
 *       LargeAddressTraits   (Block=64B / 64B гранула = 0 байт потерь на блок).
 *     - SmallAddressTraits допустима, но с потерями: Block<uint16_t>=18B,
 *       ceil(18/16)=2 гранулы выделяется под заголовок = 14 байт потерь/блок.
 *     - uint8_t-индекс не поддерживается (TinyAddressTraits удалена):
 *       максимум 255 гранул — практически непригодно для реальных сценариев.
 *
 *   Архитектурные сценарии:
 *     - Small embedded (16-bit, без heap, статический пул до ~1 МБ):
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
 *   // Small embedded-менеджер (16-bit индекс, до ~1 МБ)
 *   using SmallMgr = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>>;
 *   SmallMgr::create(1024);
 *   void* p = SmallMgr::allocate(32);
 *
 *   // Крупная база данных (64-bit индекс, петабайтный масштаб)
 *   using BigDB = pmm::PersistMemoryManager<pmm::LargeDBConfig>;
 *   BigDB::create(256 * 1024 * 1024);
 *   void* p = BigDB::allocate(4096);
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager
 * @see config.h — базовые политики блокировок (NoLock, SharedMutexLock)
 * @see address_traits.h — AddressTraits и стандартные алиасы (SmallAddressTraits, DefaultAddressTraits,
 * LargeAddressTraits)
 * @version 0.7
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/config.h"
#include "pmm/free_block_tree.h"
#include "pmm/heap_storage.h"
#include "pmm/logging_policy.h"
#include "pmm/static_storage.h"
#include "pmm/storage_backend.h"

#include <concepts>
#include <cstddef>

namespace pmm
{

// ─── Правила для гранул ─────────────────────────────────────────

/// @brief Минимальный допустимый размер гранулы (размер слова архитектуры = 4 байта).
inline constexpr std::size_t kMinGranuleSize = 4;

/**
 * @brief C++20 концепт: проверяет, что AddressTraitsT имеет допустимые параметры гранулы.
 *
 * Заменяет повторяющиеся пары `static_assert` в каждой конфигурационной структуре.
 *
 * Требования:
 *   - `AT::granule_size >= kMinGranuleSize` (минимум 4 байта — размер машинного слова).
 *   - `AT::granule_size` — степень двойки.
 *
 * Допустимые стандартные алиасы:
 *   - SmallAddressTraits   (uint16_t, granule=16) — 16 >= 4, степень двойки ✓
 *   - DefaultAddressTraits (uint32_t, granule=16) — 16 >= 4, степень двойки ✓
 *   - LargeAddressTraits   (uint64_t, granule=64) — 64 >= 4, степень двойки ✓
 */
template <typename AT>
concept ValidPmmAddressTraits =
    ( AT::granule_size >= kMinGranuleSize ) && ( ( AT::granule_size & ( AT::granule_size - 1 ) ) == 0 );

static_assert( ValidPmmAddressTraits<DefaultAddressTraits>, "DefaultAddressTraits must satisfy ValidPmmAddressTraits" );
static_assert( ValidPmmAddressTraits<SmallAddressTraits>, "SmallAddressTraits must satisfy ValidPmmAddressTraits" );
static_assert( ValidPmmAddressTraits<LargeAddressTraits>, "LargeAddressTraits must satisfy ValidPmmAddressTraits" );

// ─── BasicConfig — базовый шаблон для heap-конфигураций ──────────────────────

/**
 * @brief Базовый шаблон конфигурации менеджера с HeapStorage.
 *
 * Устраняет дублирование между CacheManagerConfig, PersistentDataConfig,
 * EmbeddedManagerConfig, IndustrialDBConfig и LargeDBConfig.
 * Готовые конфигурации теперь являются псевдонимами BasicConfig с конкретными параметрами.
 *
 * @tparam AddressTraitsT  Тип адресного пространства (DefaultAddressTraits, LargeAddressTraits, etc.)
 * @tparam LockPolicyT     Политика блокировок (config::NoLock или config::SharedMutexLock)
 * @tparam GrowNum         Числитель коэффициента роста хранилища (по умолчанию 5)
 * @tparam GrowDen         Знаменатель коэффициента роста хранилища (по умолчанию 4, т.е. рост 25%)
 * @tparam MaxMemoryGB     Максимальный объём памяти в ГБ (0 = без ограничения)
 * @tparam LoggingPolicyT  Политика логирования (logging::NoLogging по умолчанию)
 *
 * Пример создания собственной конфигурации:
 * @code
 *   // Многопоточный менеджер с 50% ростом и 32 ГБ лимитом
 *   using MyConfig = pmm::BasicConfig<
 *       pmm::DefaultAddressTraits,
 *       pmm::config::SharedMutexLock,
 *       3, 2,  // grow 3/2 = 50%
 *       32     // max 32 GB
 *   >;
 *   using MyManager = pmm::PersistMemoryManager<MyConfig>;
 *
 *   // Менеджер с логированием в stderr
 *   using DebugConfig = pmm::BasicConfig<
 *       pmm::DefaultAddressTraits,
 *       pmm::config::NoLock,
 *       5, 4, 64,
 *       pmm::logging::StderrLogging
 *   >;
 * @endcode
 */
template <typename AddressTraitsT = DefaultAddressTraits, typename LockPolicyT = config::NoLock,
          std::size_t GrowNum = config::kDefaultGrowNumerator, std::size_t GrowDen = config::kDefaultGrowDenominator,
          std::size_t MaxMemoryGB = 64, typename LoggingPolicyT = logging::NoLogging>
struct BasicConfig
{
    static_assert( ValidPmmAddressTraits<AddressTraitsT>,
                   "BasicConfig: AddressTraitsT must satisfy ValidPmmAddressTraits" );

    using address_traits                          = AddressTraitsT;
    using storage_backend                         = HeapStorage<AddressTraitsT>;
    using free_block_tree                         = AvlFreeTree<AddressTraitsT>;
    using lock_policy                             = LockPolicyT;
    using logging_policy                          = LoggingPolicyT;
    static constexpr std::size_t granule_size     = AddressTraitsT::granule_size;
    static constexpr std::size_t max_memory_gb    = MaxMemoryGB;
    static constexpr std::size_t grow_numerator   = GrowNum;
    static constexpr std::size_t grow_denominator = GrowDen;
};

// ─── StaticConfig — базовый шаблон для static-конфигураций ───────

/**
 * @brief Базовый шаблон конфигурации менеджера со StaticStorage.
 *
 * Устраняет дублирование между SmallEmbeddedStaticConfig и EmbeddedStaticConfig.
 * Аналогичен BasicConfig, но использует StaticStorage вместо HeapStorage.
 *
 * @tparam AddressTraitsT  Тип адресного пространства.
 * @tparam BufferSize      Размер статического буфера в байтах (кратно granule_size).
 * @tparam GrowNum         Числитель коэффициента роста (по умолчанию 3).
 * @tparam GrowDen         Знаменатель коэффициента роста (по умолчанию 2).
 */
template <typename AddressTraitsT, std::size_t BufferSize, std::size_t GrowNum = 3, std::size_t GrowDen = 2>
struct StaticConfig
{
    static_assert( ValidPmmAddressTraits<AddressTraitsT>,
                   "StaticConfig: AddressTraitsT must satisfy ValidPmmAddressTraits" );

    using address_traits                          = AddressTraitsT;
    using storage_backend                         = StaticStorage<BufferSize, AddressTraitsT>;
    using free_block_tree                         = AvlFreeTree<AddressTraitsT>;
    using lock_policy                             = config::NoLock;
    using logging_policy                          = logging::NoLogging;
    static constexpr std::size_t granule_size     = AddressTraitsT::granule_size;
    static constexpr std::size_t max_memory_gb    = 0; // Нет расширения — StaticStorage
    static constexpr std::size_t grow_numerator   = GrowNum;
    static constexpr std::size_t grow_denominator = GrowDen;
};

// ─── Embedded / статические конфигурации ─────────────────────────────────────

/**
 * @brief Конфигурация small-embedded-менеджера со статическим буфером и 16-bit индексом.
 *
 * Предназначена для малых систем без heap (микроконтроллеры, RTOS, bare-metal)
 * с ограничением памяти до ~1 МБ:
 *   - uint16_t индекс (SmallAddressTraits), 16-байтная гранула
 *   - pptr<T> хранит 2-байтный индекс (вместо 4 байт у DefaultAddressTraits)
 *   - StaticStorage<BufferSize, SmallAddressTraits> — фиксированный буфер, нет malloc
 *   - Максимальный пул: 65535 × 16 = ~1 МБ
 *   - Нет блокировок (NoLock) — только однопоточный контекст
 *   - Не расширяется (StaticStorage::expand() всегда false)
 *
 *
 * @tparam BufferSize Размер статического буфера в байтах (кратно 16, максимум ~1 МБ).
 */
template <std::size_t BufferSize = 1024> using SmallEmbeddedStaticConfig = StaticConfig<SmallAddressTraits, BufferSize>;

/**
 * @brief Конфигурация embedded-менеджера со статическим фиксированным буфером.
 *
 * Предназначена для систем без heap (микроконтроллеры, RTOS, bare-metal):
 *   - uint32_t индекс (DefaultAddressTraits), 16-байтная гранула
 *   - StaticStorage<BufferSize> — фиксированный буфер в BSS/глобальной области, нет malloc
 *   - Нет блокировок (NoLock) — только однопоточный контекст
 *   - Не расширяется (StaticStorage::expand() всегда false)
 *
 *
 * @tparam BufferSize Размер статического буфера в байтах (кратно 16).
 */
template <std::size_t BufferSize = 4096> using EmbeddedStaticConfig = StaticConfig<DefaultAddressTraits, BufferSize>;

// ─── Desktop / динамические конфигурации ─────────────────────────────────────

// ─── Desktop / динамические конфигурации ─────────────────────────────────────
// All configs below are aliases of BasicConfig<> with specific parameters.

/**
 * @brief Конфигурация кеш-менеджера (однопоточный, heap, 16B гранула).
 *
 * Оптимизирован для временного кеша с однопоточным доступом:
 *   - Нет блокировок (NoLock) — максимальная производительность
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память с авторасширением
 *   - Коэффициент роста 5/4 (25%)
 *
 * Типичный сценарий: кеш вычислений, временные буферы в однопоточном коде.
 */
using CacheManagerConfig = BasicConfig<DefaultAddressTraits, config::NoLock, config::kDefaultGrowNumerator,
                                       config::kDefaultGrowDenominator, 64>;

/**
 * @brief Конфигурация менеджера персистентных данных (многопоточный, heap, 16B гранула).
 *
 * Оптимизирован для хранения персистентных данных с многопоточным доступом:
 *   - SharedMutexLock — потокобезопасность
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Коэффициент роста 5/4 (25%)
 *
 * Типичный сценарий: долговременное хранение данных, файловые менеджеры.
 */
using PersistentDataConfig = BasicConfig<DefaultAddressTraits, config::SharedMutexLock, config::kDefaultGrowNumerator,
                                         config::kDefaultGrowDenominator, 64>;

/**
 * @brief Конфигурация embedded-менеджера с динамическим хранилищем.
 *
 * Оптимизирован для встраиваемых/ресурсоограниченных систем с heap:
 *   - Нет блокировок (NoLock) — минимальные накладные расходы
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Консервативный коэффициент роста 3/2 (50%) для экономии памяти
 *
 * Типичный сценарий: Linux embedded (RPi, etc.), системы с ограниченной памятью.
 */
using EmbeddedManagerConfig = BasicConfig<DefaultAddressTraits, config::NoLock, 3, 2, 64>;

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
 * Типичный сценарий: промышленные базы данных, time-series хранилища (до 64 ГБ).
 */
using IndustrialDBConfig = BasicConfig<DefaultAddressTraits, config::SharedMutexLock, 2, 1, 64>;

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
 * Типичный сценарий: крупные базы данных, хранилища данных, облачные хранилища,
 * петабайтные time-series системы.
 *
 * @note Известное ограничение — внутренние поля ManagerHeader
 *   (used_size, block_count, free_count, alloc_count, first_block_offset,
 *    last_block_offset, free_tree_root) хранятся как std::uint32_t, что
 *   ограничивает адресуемое пространство 2^32 гранулами × 64 байт = 256 GiB,
 *   а не петабайтным масштабом. Для полноценной 64-bit поддержки ManagerHeader
 *   необходимо сделать параметрическим по AddressTraitsT (планируемый рефакторинг).
 *
 * @code
 *   using BigDB = pmm::PersistMemoryManager<pmm::LargeDBConfig>;
 *   BigDB::create(256 * 1024 * 1024); // 256 МБ начальный размер
 *   void* ptr = BigDB::allocate(4096);
 *   // sizeof(BigDB::pptr<int>) == 8  (64-bit индекс)
 * @endcode
 */
using LargeDBConfig = BasicConfig<LargeAddressTraits, config::SharedMutexLock, 2, 1, 0>;

} // namespace pmm
