/**
 * @file pmm/static_manager_factory.h
 * @brief StaticPersistMemoryManager — статические экземпляры менеджера ПАП (Issue #100).
 *
 * Предоставляет шаблон `StaticPersistMemoryManager<ConfigT, Tag>`, который создаёт
 * статически идентифицируемые экземпляры менеджера персистентной памяти.
 *
 * Отличие от обычного `AbstractPersistMemoryManager`:
 *   - Параметр `Tag` делает тип уникальным даже при той же конфигурации
 *   - Позволяет использовать `pptr<T, StaticManager>` с гарантией привязки к конкретному
 *     именованному менеджеру (а не просто к конфигурации)
 *   - Удобен для случаев, когда несколько менеджеров одного типа должны различаться
 *
 * Пример использования:
 * @code
 *   // Создаём два разных статических менеджера одной конфигурации
 *   struct CacheTag {};
 *   struct PersistentTag {};
 *
 *   using CacheManager      = pmm::StaticPersistMemoryManager<pmm::CacheManagerConfig, CacheTag>;
 *   using PersistentManager = pmm::StaticPersistMemoryManager<pmm::CacheManagerConfig, PersistentTag>;
 *
 *   CacheManager     cache_mgr;
 *   PersistentManager persist_mgr;
 *   cache_mgr.create(64 * 1024);
 *   persist_mgr.create(256 * 1024);
 *
 *   // pptr<int, CacheManager> и pptr<int, PersistentManager> — разные типы!
 *   CacheManager::pptr<int>     cache_ptr     = cache_mgr.allocate_typed<int>();
 *   PersistentManager::pptr<int> persist_ptr  = persist_mgr.allocate_typed<int>();
 *
 *   // Разыменование строго типизировано
 *   *cache_ptr.resolve(cache_mgr) = 42;
 *   *persist_ptr.resolve(persist_mgr) = 100;
 *
 *   // Компилятор запретит смешение:
 *   // *cache_ptr.resolve(persist_mgr) = 0; // compile error!
 * @endcode
 *
 * @see abstract_pmm.h — AbstractPersistMemoryManager (базовый класс)
 * @see manager_configs.h — готовые конфигурации менеджеров
 * @see manager_concept.h — is_persist_memory_manager concept
 * @version 0.1 (Issue #100 — Phase 2: Static Manager Factories)
 */

#pragma once

#include "pmm/abstract_pmm.h"
#include "pmm/address_traits.h"
#include "pmm/config.h"
#include "pmm/free_block_tree.h"
#include "pmm/heap_storage.h"
#include "pmm/manager_concept.h"
#include "pmm/manager_configs.h"
#include "pmm/pptr.h"
#include "pmm/storage_backend.h"

namespace pmm
{

/**
 * @brief Тег для тэгирования статических экземпляров менеджера по умолчанию.
 *
 * Используется как параметр по умолчанию для `StaticPersistMemoryManager<ConfigT, Tag>`.
 * Для различения нескольких экземпляров — определите собственные теги.
 */
struct DefaultManagerTag
{
};

/**
 * @brief Статически-идентифицируемый менеджер персистентной памяти.
 *
 * Наследует `AbstractPersistMemoryManager` с конфигурацией из `ConfigT`,
 * но добавляет параметр `Tag`, делающий тип уникальным. Это позволяет
 * создавать несколько менеджеров одной конфигурации с разными типами.
 *
 * @tparam ConfigT Конфигурация менеджера (например pmm::CacheManagerConfig).
 *                 Должна предоставлять:
 *                   - `lock_policy` — политика блокировок
 * @tparam Tag     Тег для уникализации типа (по умолчанию DefaultManagerTag).
 *                 Обычно задаётся как пустая структура.
 *
 * Наследует все методы AbstractPersistMemoryManager:
 *   - `create()` / `create(size_t)` — инициализация
 *   - `allocate<T>()` / `deallocate<T>()` — выделение/освобождение
 *   - `allocate_typed<T>()` — типизированное выделение с pptr
 *   - `resolve<T>(pptr)` — разыменование
 *   - `total_size()`, `used_size()`, `free_size()` — статистика
 *
 * @note `StaticPersistMemoryManager<Config, TagA>::pptr<T>` и
 *       `StaticPersistMemoryManager<Config, TagB>::pptr<T>` — разные типы
 *       при `TagA != TagB`, что обеспечивает типобезопасность.
 */
template <typename ConfigT = CacheManagerConfig, typename Tag = DefaultManagerTag>
class StaticPersistMemoryManager
    : public AbstractPersistMemoryManager<DefaultAddressTraits, HeapStorage<DefaultAddressTraits>,
                                          AvlFreeTree<DefaultAddressTraits>, typename ConfigT::lock_policy>
{
  public:
    using base_type = AbstractPersistMemoryManager<DefaultAddressTraits, HeapStorage<DefaultAddressTraits>,
                                                   AvlFreeTree<DefaultAddressTraits>, typename ConfigT::lock_policy>;

    /// @brief Тег, уникализирующий данный экземпляр менеджера.
    using tag = Tag;

    /// @brief Конфигурация данного менеджера.
    using config = ConfigT;

    /// @brief Тип самого менеджера (переопределяет manager_type из базового класса).
    using manager_type = StaticPersistMemoryManager<ConfigT, Tag>;

    /**
     * @brief Вложенный псевдоним персистентного указателя, привязанного к этому менеджеру.
     *
     * Каждый тег создаёт уникальный тип pptr — это гарантирует, что указатели
     * от разных менеджеров нельзя перепутать во время компиляции.
     *
     * @tparam T Тип данных.
     */
    template <typename T> using pptr = pmm::pptr<T, manager_type>;

    // Наследуем конструкторы
    using base_type::base_type;

    // ─── Переопределение типизированного API для использования нашего pptr<T> ──
    // Базовый AbstractPersistMemoryManager определяет методы для своего manager_type.
    // Здесь мы добавляем перегрузки для нашего manager_type = StaticPersistMemoryManager.

    /**
     * @brief Выделить один объект типа T и вернуть pptr<T> этого менеджера.
     *
     * @tparam T Тип выделяемого объекта.
     * @return pptr<T> — персистентный указатель, привязанный к данному менеджеру.
     */
    template <typename T> pptr<T> allocate_typed() noexcept
    {
        typename base_type::template pptr<T> p = base_type::template allocate_typed<T>();
        return pptr<T>( p.offset() );
    }

    /**
     * @brief Выделить массив из count объектов типа T и вернуть pptr<T>.
     *
     * @tparam T Тип элемента массива.
     * @param count Количество элементов.
     * @return pptr<T> — персистентный указатель, привязанный к данному менеджеру.
     */
    template <typename T> pptr<T> allocate_typed( std::size_t count ) noexcept
    {
        typename base_type::template pptr<T> p = base_type::template allocate_typed<T>( count );
        return pptr<T>( p.offset() );
    }

    /**
     * @brief Освободить блок по pptr<T> этого менеджера.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель.
     */
    template <typename T> void deallocate_typed( pptr<T> p ) noexcept
    {
        base_type::template deallocate_typed<T>( typename base_type::template pptr<T>( p.offset() ) );
    }

    /**
     * @brief Разыменовать pptr<T> этого менеджера.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель.
     * @return T* — указатель на данные или nullptr.
     */
    template <typename T> T* resolve( pptr<T> p ) const noexcept
    {
        return base_type::template resolve<T>( typename base_type::template pptr<T>( p.offset() ) );
    }

    /**
     * @brief Разыменовать pptr<T> и получить i-й элемент массива.
     *
     * @tparam T Тип элемента.
     * @param p Персистентный указатель на массив.
     * @param i Индекс элемента.
     * @return T* — указатель на i-й элемент или nullptr.
     */
    template <typename T> T* resolve_at( pptr<T> p, std::size_t i ) const noexcept
    {
        return base_type::template resolve_at<T>( typename base_type::template pptr<T>( p.offset() ), i );
    }
};

// Note: StaticPersistMemoryManager is a legacy class from Issue #100.
// It uses instance methods, not static methods. The is_persist_memory_manager_v
// concept now checks for static methods (Issue #110), so StaticPersistMemoryManager
// no longer satisfies it. Use PersistMemoryManager<ConfigT, InstanceId> instead.

} // namespace pmm
