/**
 * @file basic_usage.cpp
 * @brief Пример базового использования PersistMemoryManager (Фаза 1)
 *
 * Демонстрирует:
 * 1. Создание менеджера в произвольном буфере памяти.
 * 2. Выделение блоков разных размеров и выравниваний.
 * 3. Освобождение блоков.
 * 4. Перевыделение блока.
 * 5. Получение статистики.
 * 6. Диагностику (dump_stats и validate).
 */

#include "persist_memory_manager.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

int main()
{
    // ── 1. Подготовка буфера ─────────────────────────────────────────────────
    const std::size_t memory_size = 1024 * 1024; // 1 МБ
    void*             memory      = std::malloc( memory_size );
    if ( memory == nullptr )
    {
        std::cerr << "Не удалось выделить системную память\n";
        return 1;
    }

    // ── 2. Создание менеджера ────────────────────────────────────────────────
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( memory, memory_size );
    if ( mgr == nullptr )
    {
        std::cerr << "Не удалось создать PersistMemoryManager\n";
        std::free( memory );
        return 1;
    }
    std::cout << "Менеджер создан. Управляемая область: " << memory_size / 1024 << " КБ\n\n";

    // ── 3. Выделение блоков ───────────────────────────────────────────────────
    void* block1 = mgr->allocate( 256 );      // 256 байт, выравнивание 16 (по умолчанию)
    void* block2 = mgr->allocate( 1024, 32 ); // 1 КБ, выравнивание 32
    void* block3 = mgr->allocate( 4096, 64 ); // 4 КБ, выравнивание 64

    if ( block1 == nullptr || block2 == nullptr || block3 == nullptr )
    {
        std::cerr << "Ошибка выделения блоков\n";
        pmm::PersistMemoryManager::destroy();
        return 1;
    }

    std::cout << "Выделено 3 блока:\n";
    std::cout << "  block1 (256 байт, align=16): " << block1 << "\n";
    std::cout << "  block2 (1024 байт, align=32): " << block2 << "\n";
    std::cout << "  block3 (4096 байт, align=64): " << block3 << "\n\n";

    // Проверка выравнивания
    auto check_align = []( void* ptr, std::size_t align, const char* name )
    {
        bool ok = ( reinterpret_cast<std::uintptr_t>( ptr ) % align == 0 );
        std::cout << "  Выравнивание " << name << " на " << align << " байт: " << ( ok ? "OK" : "FAIL" ) << "\n";
        return ok;
    };
    bool aligns_ok = true;
    aligns_ok &= check_align( block1, 16, "block1" );
    aligns_ok &= check_align( block2, 32, "block2" );
    aligns_ok &= check_align( block3, 64, "block3" );
    std::cout << "\n";

    // ── 4. Запись данных ──────────────────────────────────────────────────────
    std::memset( block1, 0xAA, 256 );
    std::memset( block2, 0xBB, 1024 );
    std::memset( block3, 0xCC, 4096 );
    std::cout << "Данные записаны в блоки.\n\n";

    // ── 5. Статистика после выделений ─────────────────────────────────────────
    std::cout << "Статистика после выделений:\n";
    mgr->dump_stats();
    std::cout << "\n";

    pmm::MemoryStats stats = pmm::get_stats( mgr );
    std::cout << "Подробная статистика:\n"
              << "  Всего блоков     : " << stats.total_blocks << "\n"
              << "  Свободных блоков : " << stats.free_blocks << "\n"
              << "  Занятых блоков   : " << stats.allocated_blocks << "\n"
              << "  Крупнейший своб. : " << stats.largest_free << " байт\n\n";

    // ── 6. Информация о конкретном блоке ─────────────────────────────────────
    pmm::AllocationInfo info = pmm::get_info( mgr, block2 );
    std::cout << "Информация о block2:\n"
              << "  Валиден          : " << ( info.is_valid ? "да" : "нет" ) << "\n"
              << "  Размер           : " << info.size << " байт\n"
              << "  Выравнивание     : " << info.alignment << " байт\n\n";

    // ── 7. Освобождение блока 1 ───────────────────────────────────────────────
    mgr->deallocate( block1 );
    std::cout << "block1 освобождён.\n";
    std::cout << "Статистика после освобождения block1:\n";
    mgr->dump_stats();
    std::cout << "\n";

    // ── 8. Перевыделение block2 ────────────────────────────────────────────────
    void* block2_new = mgr->reallocate( block2, 2048 ); // Увеличиваем до 2 КБ
    if ( block2_new == nullptr )
    {
        std::cerr << "Ошибка перевыделения block2\n";
    }
    else
    {
        std::cout << "block2 перевыделён (1024 -> 2048 байт): " << block2_new << "\n\n";
        block2 = block2_new;
    }

    // ── 9. Валидация структур ─────────────────────────────────────────────────
    bool valid = mgr->validate();
    std::cout << "Валидация структур менеджера: " << ( valid ? "OK" : "FAIL" ) << "\n\n";

    // ── 10. Освобождение оставшихся блоков ────────────────────────────────────
    mgr->deallocate( block2 );
    mgr->deallocate( block3 );
    std::cout << "Все блоки освобождены.\n";
    std::cout << "Финальная статистика:\n";
    mgr->dump_stats();

    // ── 11. Уничтожение менеджера ─────────────────────────────────────────────
    pmm::PersistMemoryManager::destroy();

    std::cout << "\nПример завершён успешно.\n";
    return ( aligns_ok && valid ) ? 0 : 1;
}
