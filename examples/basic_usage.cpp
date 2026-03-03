/**
 * @file basic_usage.cpp
 * @brief Пример базового использования PersistMemoryManager (Фаза 1)
 *
 * Демонстрирует:
 * 1. Создание менеджера в произвольном буфере памяти.
 * 2. Выделение блоков разных размеров.
 * 3. Освобождение блоков.
 * 4. Перевыделение блока.
 * 5. Получение статистики.
 * 6. Диагностику (dump_stats и validate).
 *
 * Issue #61: использует новый статический API PersistMemoryManager.
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

    // ── 2. Создание менеджера (Issue #61: create() возвращает bool) ──────────
    if ( !pmm::PersistMemoryManager<>::create( memory, memory_size ) )
    {
        std::cerr << "Не удалось создать PersistMemoryManager\n";
        std::free( memory );
        return 1;
    }
    std::cout << "Менеджер создан. Управляемая область: " << memory_size / 1024 << " КБ\n\n";

    // ── 3. Выделение блоков (Issue #61: allocate_typed<uint8_t>(N)) ──────────
    pmm::pptr<uint8_t> block1 = pmm::PersistMemoryManager<>::allocate_typed<uint8_t>( 256 );
    pmm::pptr<uint8_t> block2 = pmm::PersistMemoryManager<>::allocate_typed<uint8_t>( 1024 );
    pmm::pptr<uint8_t> block3 = pmm::PersistMemoryManager<>::allocate_typed<uint8_t>( 4096 );

    if ( block1.is_null() || block2.is_null() || block3.is_null() )
    {
        std::cerr << "Ошибка выделения блоков\n";
        pmm::PersistMemoryManager<>::destroy();
        std::free( memory );
        return 1;
    }

    std::cout << "Выделено 3 блока:\n";
    std::cout << "  block1 (256 байт):  offset=" << block1.offset() << "\n";
    std::cout << "  block2 (1024 байт): offset=" << block2.offset() << "\n";
    std::cout << "  block3 (4096 байт): offset=" << block3.offset() << "\n\n";

    // Проверка выравнивания (Issue #59: гранулы по 16 байт гарантируют align=16)
    auto check_align = []( void* ptr, std::size_t align, const char* name )
    {
        bool ok = ( reinterpret_cast<std::uintptr_t>( ptr ) % align == 0 );
        std::cout << "  Выравнивание " << name << " на " << align << " байт: " << ( ok ? "OK" : "FAIL" ) << "\n";
        return ok;
    };
    bool aligns_ok = true;
    aligns_ok &= check_align( block1.get(), 16, "block1" );
    aligns_ok &= check_align( block2.get(), 16, "block2" );
    aligns_ok &= check_align( block3.get(), 16, "block3" );
    std::cout << "\n";

    // ── 4. Запись данных ──────────────────────────────────────────────────────
    std::memset( block1.get(), 0xAA, 256 );
    std::memset( block2.get(), 0xBB, 1024 );
    std::memset( block3.get(), 0xCC, 4096 );
    std::cout << "Данные записаны в блоки.\n\n";

    // ── 5. Статистика после выделений ─────────────────────────────────────────
    std::cout << "Статистика после выделений:\n";
    pmm::PersistMemoryManager<>::dump_stats( std::cout );
    std::cout << "\n";

    // Issue #61: get_stats() больше не принимает mgr
    pmm::MemoryStats stats = pmm::get_stats();
    std::cout << "Подробная статистика:\n"
              << "  Всего блоков     : " << stats.total_blocks << "\n"
              << "  Свободных блоков : " << stats.free_blocks << "\n"
              << "  Занятых блоков   : " << stats.allocated_blocks << "\n"
              << "  Крупнейший своб. : " << stats.largest_free << " байт\n\n";

    // ── 6. Информация о конкретном блоке (Issue #61: get_info() удалён) ──────
    // Используем block_data_size_bytes() для получения размера блока block2
    std::size_t block2_data_size = pmm::PersistMemoryManager<>::block_data_size_bytes( block2.offset() );
    std::cout << "Информация о block2:\n"
              << "  Размер данных    : " << block2_data_size << " байт\n\n";

    // ── 7. Освобождение блока 1 (Issue #61: deallocate_typed) ────────────────
    pmm::PersistMemoryManager<>::deallocate_typed( block1 );
    std::cout << "block1 освобождён.\n";
    std::cout << "Статистика после освобождения block1:\n";
    pmm::PersistMemoryManager<>::dump_stats( std::cout );
    std::cout << "\n";

    // ── 8. Перевыделение block2 (Issue #61: reallocate_typed, N — это кол-во T) ──
    pmm::pptr<uint8_t> block2_new = pmm::PersistMemoryManager<>::reallocate_typed( block2, 2048 );
    if ( block2_new.is_null() )
    {
        std::cerr << "Ошибка перевыделения block2\n";
    }
    else
    {
        std::cout << "block2 перевыделён (1024 -> 2048 байт): offset=" << block2_new.offset() << "\n\n";
        block2 = block2_new;
    }

    // ── 9. Валидация структур (Issue #61: статический вызов) ─────────────────
    bool valid = pmm::PersistMemoryManager<>::validate();
    std::cout << "Валидация структур менеджера: " << ( valid ? "OK" : "FAIL" ) << "\n\n";

    // ── 10. Освобождение оставшихся блоков ────────────────────────────────────
    pmm::PersistMemoryManager<>::deallocate_typed( block2 );
    pmm::PersistMemoryManager<>::deallocate_typed( block3 );
    std::cout << "Все блоки освобождены.\n";
    std::cout << "Финальная статистика:\n";
    pmm::PersistMemoryManager<>::dump_stats( std::cout );

    // ── 11. Уничтожение менеджера (Issue #61: после destroy() — free вручную) ─
    pmm::PersistMemoryManager<>::destroy();
    std::free( memory );

    std::cout << "\nПример завершён успешно.\n";
    return ( aligns_ok && valid ) ? 0 : 1;
}
