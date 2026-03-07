/**
 * @file new_api_usage.cpp
 * @brief Пример использования нового API AbstractPersistMemoryManager (Issue #97)
 *
 * Демонстрирует миграцию с устаревшего синглтон-API на новый параметрический API.
 * По требованию Issue #97: снаружи менеджера используются только pptr<T>.
 *
 * Было (устаревший синглтон):
 *   auto p = PersistMemoryManager<>::allocate_typed<int>();
 *   *p = 42; // разыменование через синглтон
 *
 * Стало (новый RAII-стиль с pptr<T>):
 *   pmm::presets::SingleThreadedHeap pmm;
 *   pmm.create(1024);
 *   pmm::pptr<int> p = pmm.allocate_typed<int>(); // возвращает pptr<T>
 *   *pmm.resolve(p) = 42;                          // разыменование через экземпляр
 *   pmm.deallocate_typed(p);                       // освобождение через pptr<T>
 *
 * Ключевые преимущества pptr<T>:
 *   - 4 байта вместо 8 (address-independent 32-bit granule index)
 *   - Корректно загружается из файла по другому базовому адресу
 *   - Запрет p++ / p-- (нет случайной адресной арифметики)
 *
 * Примеры пресетов:
 *   - EmbeddedStatic4K  — без malloc (для embedded-систем)
 *   - SingleThreadedHeap — однопоточный с HeapStorage
 *   - MultiThreadedHeap  — многопоточный с SharedMutexLock
 *   - PersistentFileMapped — файловый с персистентностью через mmap
 */

#include "pmm/io.h"
#include "pmm/pmm_presets.h"

#include <cstdio>
#include <cstring>
#include <iostream>

// ─── 1. EmbeddedStatic4K — без динамических выделений ───────────────────────

static void demo_embedded_static()
{
    std::cout << "=== EmbeddedStatic4K (StaticStorage, no malloc) ===\n";

    pmm::presets::EmbeddedStatic4K pmm;
    if ( !pmm.create() )
    {
        std::cerr << "Failed to create EmbeddedStatic4K\n";
        return;
    }

    std::cout << "Total: " << pmm.total_size() << " bytes\n";
    std::cout << "Free:  " << pmm.free_size() << " bytes\n";

    // Issue #97: снаружи менеджера используем только pptr<T>
    pmm::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 64 );
    if ( p )
    {
        std::uint8_t* raw = pmm.resolve( p );
        std::memset( raw, 0xAA, 64 );
        std::cout << "Allocated 64 bytes (pptr offset=" << p.offset() << ", sizeof(pptr)=" << sizeof( p ) << ")\n";
        pmm.deallocate_typed( p );
        std::cout << "Deallocated via pptr<T>.\n";
    }

    pmm.destroy();
    std::cout << "\n";
}

// ─── 2. SingleThreadedHeap — динамический однопоточный с pptr<T> ─────────────

struct Point
{
    double x, y;
};

static void demo_single_threaded_heap()
{
    std::cout << "=== SingleThreadedHeap (HeapStorage, NoLock) — pptr<T> API ===\n";

    pmm::presets::SingleThreadedHeap pmm;
    if ( !pmm.create( 64 * 1024 ) ) // 64 KiB
    {
        std::cerr << "Failed to create SingleThreadedHeap\n";
        return;
    }

    std::cout << "Total: " << pmm.total_size() << " bytes\n";

    // Выделяем структуру через typed API — снаружи только pptr<T>
    pmm::pptr<Point> p_point = pmm.allocate_typed<Point>();
    pmm::pptr<int>   p_arr   = pmm.allocate_typed<int>( 10 ); // массив из 10 int

    if ( p_point && p_arr )
    {
        // Разыменование через resolve (единственный правильный способ с AbstractPMM)
        Point* pt = pmm.resolve( p_point );
        pt->x     = 1.5;
        pt->y     = 2.5;
        std::cout << "Point: (" << pmm.resolve( p_point )->x << ", " << pmm.resolve( p_point )->y << ")\n";

        // Запись в массив через resolve_at
        for ( int i = 0; i < 10; ++i )
            *pmm.resolve_at( p_arr, static_cast<std::size_t>( i ) ) = i * i;

        int sum = 0;
        for ( int i = 0; i < 10; ++i )
            sum += *pmm.resolve_at( p_arr, static_cast<std::size_t>( i ) );
        std::cout << "Array sum of squares: " << sum << " (expected: 285)\n";

        std::cout << "Used: " << pmm.used_size() << " bytes\n";
        std::cout << "Free: " << pmm.free_size() << " bytes\n";

        // Сохраняем гранульный индекс для демонстрации персистентности
        std::uint32_t point_offset = p_point.offset();
        std::cout << "Point pptr offset=" << point_offset << " (4 bytes, address-independent)\n";

        pmm.deallocate_typed( p_point );
        pmm.deallocate_typed( p_arr );
    }

    pmm.destroy();
    std::cout << "\n";
}

// ─── 3. PersistentFileMapped — файловая персистентность с pptr<T> ────────────

static void demo_persistent_file_mapped()
{
    std::cout << "=== PersistentFileMapped (MMapStorage) — pptr<T> persistence ===\n";

    const char*       filename = "/tmp/demo_pmm.dat";
    const std::size_t kSize    = 64 * 1024;

    std::uint32_t saved_offset = 0; // Сохранённый pptr offset (персистентный)

    // Первый запуск: создаём и сохраняем данные через pptr<T>
    {
        pmm::presets::PersistentFileMapped pmm;
        if ( !pmm.backend().open( filename, kSize ) )
        {
            std::cerr << "Failed to open mmap file\n";
            return;
        }

        if ( !pmm.load() )
        {
            if ( !pmm.create() )
            {
                std::cerr << "Failed to create PersistentFileMapped\n";
                return;
            }
            std::cout << "First run: created new PMM\n";
        }
        else
        {
            std::cout << "Subsequent run: loaded existing PMM\n";
        }

        // Выделяем через pptr<T>
        pmm::pptr<int> p = pmm.allocate_typed<int>();
        if ( p )
        {
            *pmm.resolve( p ) = 42;
            saved_offset      = p.offset(); // Сохраняем pptr offset (не адрес!)
            std::cout << "Allocated int=42 via pptr<int> (offset=" << saved_offset << ")\n";
        }

        std::cout << "Blocks: " << pmm.alloc_block_count() << " allocated\n";
        pmm.backend().close(); // данные записаны в файл через mmap
    }

    // Второй запуск: загружаем и проверяем через сохранённый pptr offset
    {
        pmm::presets::PersistentFileMapped pmm;
        if ( !pmm.backend().open( filename, kSize ) || !pmm.load() )
        {
            std::cerr << "Failed to reload PMM\n";
        }
        else
        {
            // Восстанавливаем pptr из сохранённого смещения
            pmm::pptr<int> p( saved_offset );
            std::cout << "Reloaded: pptr<int>(offset=" << saved_offset << ") → value=" << *pmm.resolve( p )
                      << " (expected: 42)\n";
            pmm.backend().close();
        }
    }

    // Демонстрация нового IO API (Issue #97): save_manager / load_manager_from_file
    {
        pmm::presets::SingleThreadedHeap pmm;
        if ( pmm.create( kSize ) )
        {
            pmm::pptr<double> p = pmm.allocate_typed<double>();
            if ( p )
                *pmm.resolve( p ) = 3.14;

            const char* export_file = "/tmp/demo_pmm_export.dat";
            if ( pmm::save_manager( pmm, export_file ) )
            {
                std::cout << "Saved via save_manager() (Issue #97)\n";
                std::remove( export_file );
            }
            pmm.deallocate_typed( p );
            pmm.destroy();
        }
    }

    std::remove( filename );
    std::cout << "\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "PersistMemoryManager — новый API с pptr<T> (Issue #97)\n\n";
    std::cout << "Миграция:\n";
    std::cout << "  Было: PersistMemoryManager<>::allocate_typed<int>() → pptr<int>\n";
    std::cout << "  Стало: pmm.allocate_typed<int>() → pptr<int> (resolve через экземпляр)\n\n";
    std::cout << "Правило (Issue #97): снаружи менеджера — только pptr<T>, не void*!\n\n";

    demo_embedded_static();
    demo_single_threaded_heap();
    demo_persistent_file_mapped();

    std::cout << "Все демо завершены успешно.\n";
    return 0;
}
