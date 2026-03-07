/**
 * @file new_api_usage.cpp
 * @brief Пример использования нового API AbstractPersistMemoryManager (Issue #97)
 *
 * Демонстрирует миграцию с устаревшего синглтон-API на новый параметрический API:
 *
 * Было (устаревший синглтон):
 *   auto ptr = PersistMemoryManager<>::allocate_typed<int>();
 *
 * Стало (новый RAII-стиль):
 *   pmm::presets::SingleThreadedHeap pmm;
 *   pmm.create(1024);
 *   auto ptr = pmm.allocate(sizeof(int));
 *
 * Примеры пресетов:
 *   - EmbeddedStatic4K  — без malloc (для embedded-систем)
 *   - SingleThreadedHeap — однопоточный с HeapStorage
 *   - MultiThreadedHeap  — многопоточный с SharedMutexLock
 *   - PersistentFileMapped — файловый с персистентностью через mmap
 */

#include "pmm/io.h"
#include "pmm/pmm_presets.h"

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

    void* ptr = pmm.allocate( 64 );
    if ( ptr != nullptr )
    {
        std::memset( ptr, 0xAA, 64 );
        std::cout << "Allocated 64 bytes at " << ptr << "\n";
        pmm.deallocate( ptr );
        std::cout << "Deallocated.\n";
    }

    pmm.destroy();
    std::cout << "\n";
}

// ─── 2. SingleThreadedHeap — динамический однопоточный ──────────────────────

static void demo_single_threaded_heap()
{
    std::cout << "=== SingleThreadedHeap (HeapStorage, NoLock) ===\n";

    pmm::presets::SingleThreadedHeap pmm;
    if ( !pmm.create( 64 * 1024 ) ) // 64 KiB
    {
        std::cerr << "Failed to create SingleThreadedHeap\n";
        return;
    }

    std::cout << "Total: " << pmm.total_size() << " bytes\n";

    // Выделяем несколько блоков
    void* a = pmm.allocate( 256 );
    void* b = pmm.allocate( 1024 );
    void* c = pmm.allocate( 4096 );

    if ( a && b && c )
    {
        std::memset( a, 0x11, 256 );
        std::memset( b, 0x22, 1024 );
        std::memset( c, 0x33, 4096 );

        std::cout << "Allocated: 256 + 1024 + 4096 bytes\n";
        std::cout << "Used: " << pmm.used_size() << " bytes\n";
        std::cout << "Free: " << pmm.free_size() << " bytes\n";

        pmm.deallocate( a );
        pmm.deallocate( b );
        pmm.deallocate( c );
    }

    pmm.destroy();
    std::cout << "\n";
}

// ─── 3. PersistentFileMapped — файловая персистентность ─────────────────────

static void demo_persistent_file_mapped()
{
    std::cout << "=== PersistentFileMapped (MMapStorage) ===\n";

    const char*       filename = "/tmp/demo_pmm.dat";
    const std::size_t kSize    = 64 * 1024;

    // Первый запуск: создаём
    {
        pmm::presets::PersistentFileMapped pmm;
        if ( !pmm.backend().open( filename, kSize ) )
        {
            std::cerr << "Failed to open mmap file\n";
            return;
        }

        if ( !pmm.load() )
        {
            // Первый запуск — инициализируем
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

        void* ptr = pmm.allocate( 100 );
        if ( ptr != nullptr )
        {
            std::memset( ptr, 0x42, 100 );
            std::cout << "Allocated 100 bytes, data persisted to file.\n";
        }

        std::cout << "Blocks: " << pmm.alloc_block_count() << " allocated\n";
        pmm.backend().close(); // данные записаны в файл
    }

    // Демонстрация нового IO API (Issue #97)
    {
        pmm::presets::SingleThreadedHeap pmm;
        if ( pmm.create( kSize ) )
        {
            // Сохраняем в другой файл через новый API
            const char* export_file = "/tmp/demo_pmm_export.dat";
            if ( pmm::save_manager( pmm, export_file ) )
            {
                std::cout << "Saved to " << export_file << " via save_manager() (Issue #97)\n";
                std::remove( export_file );
            }
            pmm.destroy();
        }
    }

    std::remove( filename );
    std::cout << "\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "PersistMemoryManager — новый API (Issue #97)\n\n";
    std::cout << "Миграция: 'PersistMemoryManager<>::allocate_typed<int>()'\n";
    std::cout << "  →  'pmm::presets::SingleThreadedHeap pmm; pmm.create(1024); pmm.allocate(sizeof(int));'\n\n";

    demo_embedded_static();
    demo_single_threaded_heap();
    demo_persistent_file_mapped();

    std::cout << "Все демо завершены успешно.\n";
    return 0;
}
