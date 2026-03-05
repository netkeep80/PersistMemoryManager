/**
 * @file persistence_demo.cpp
 * @brief Демонстрация персистентности PersistMemoryManager (Фаза 3)
 *
 * Демонстрирует:
 * 1. Создание менеджера, выделение блоков и запись данных.
 * 2. Сохранение образа памяти в файл (save).
 * 3. Уничтожение первого менеджера и освобождение буфера.
 * 4. Загрузку образа из файла в новый буфер (load_from_file).
 * 5. Проверку, что данные и метаданные полностью восстановлены.
 * 6. Продолжение работы с восстановленным менеджером (allocate/deallocate).
 *
 * Issue #61: использует новый статический API PersistMemoryManager.
 */

#include "pmm/io.h"
#include "pmm/legacy_manager.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

static const char* IMAGE_FILE = "heap_image.dat";

int main()
{
    std::cout << "=== PersistMemoryManager — Демонстрация персистентности ===\n\n";

    // ─── Фаза A: Создание и наполнение ───────────────────────────────────────

    const std::size_t memory_size = 256 * 1024; // 256 КБ
    void*             mem1        = std::malloc( memory_size );
    if ( mem1 == nullptr )
    {
        std::cerr << "Не удалось выделить системную память\n";
        return 1;
    }

    // Issue #61: create() возвращает bool
    if ( !pmm::PersistMemoryManager<>::create( mem1, memory_size ) )
    {
        std::cerr << "Не удалось создать PersistMemoryManager\n";
        std::free( mem1 );
        return 1;
    }
    std::cout << "[A] Менеджер создан. Буфер: " << memory_size / 1024 << " КБ\n";

    // Выделяем несколько блоков (Issue #61: allocate_typed<uint8_t>)
    const std::size_t size1 = 512;
    const std::size_t size2 = 1024;
    const std::size_t size3 = 256;

    pmm::pptr<uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<uint8_t>( size1 );
    pmm::pptr<uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<uint8_t>( size2 );
    pmm::pptr<uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<uint8_t>( size3 );

    if ( p1.is_null() || p2.is_null() || p3.is_null() )
    {
        std::cerr << "Ошибка выделения блоков\n";
        pmm::PersistMemoryManager<>::destroy();
        std::free( mem1 );
        return 1;
    }

    // Записываем данные: p1 — строка, p2 — числа, p3 — паттерн
    std::strcpy( reinterpret_cast<char*>( p1.get() ), "Hello, PersistMemoryManager!" );

    // Записываем квадраты через int-интерпретацию p2
    int* arr2 = reinterpret_cast<int*>( p2.get() );
    for ( std::size_t i = 0; i < size2 / sizeof( int ); i++ )
    {
        arr2[i] = static_cast<int>( i * i );
    }
    std::memset( p3.get(), 0xFF, size3 );

    std::cout << "[A] Выделено 3 блока. Данные записаны.\n";

    // Освобождаем p3 (чтобы показать, что свободные блоки тоже сохраняются)
    pmm::PersistMemoryManager<>::deallocate_typed( p3 );
    std::cout << "[A] Блок p3 освобождён (для демонстрации частично свободной кучи).\n";

    if ( !pmm::PersistMemoryManager<>::validate() )
    {
        std::cerr << "Валидация перед сохранением провалилась\n";
        pmm::PersistMemoryManager<>::destroy();
        std::free( mem1 );
        return 1;
    }

    std::cout << "\nСтатистика перед сохранением:\n";
    pmm::PersistMemoryManager<>::dump_stats( std::cout );

    // Запоминаем смещения (гранульные индексы) выделенных блоков
    // (они останутся такими же в восстановленном образе)
    std::uint32_t off1 = p1.offset();
    std::uint32_t off2 = p2.offset();

    // ─── Фаза B: Сохранение образа ───────────────────────────────────────────

    // Issue #61: pmm::save() больше не принимает mgr
    if ( !pmm::save( IMAGE_FILE ) )
    {
        std::cerr << "Ошибка сохранения образа в файл: " << IMAGE_FILE << "\n";
        pmm::PersistMemoryManager<>::destroy();
        std::free( mem1 );
        return 1;
    }
    std::cout << "\n[B] Образ сохранён в файл: " << IMAGE_FILE << "\n";

    // Уничтожаем первый менеджер — имитируем завершение программы
    // Issue #61: после destroy() нужно вручную освободить буфер
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );
    mem1 = nullptr;
    std::cout << "[B] Первый менеджер уничтожен (имитация завершения программы).\n";

    // ─── Фаза C: Восстановление из файла ─────────────────────────────────────

    std::cout << "\n[C] Загрузка образа из файла...\n";

    void* mem2 = std::malloc( memory_size );
    if ( mem2 == nullptr )
    {
        std::cerr << "Не удалось выделить буфер для загрузки\n";
        return 1;
    }

    // Issue #61: load_from_file() возвращает bool вместо PersistMemoryManager*
    if ( !pmm::load_from_file( IMAGE_FILE, mem2, memory_size ) )
    {
        std::cerr << "Не удалось загрузить образ из файла\n";
        std::free( mem2 );
        return 1;
    }

    if ( !pmm::PersistMemoryManager<>::validate() )
    {
        std::cerr << "Валидация после загрузки провалилась\n";
        pmm::PersistMemoryManager<>::destroy();
        std::free( mem2 );
        return 1;
    }

    std::cout << "[C] Образ успешно загружен и валиден.\n";
    std::cout << "\nСтатистика после загрузки:\n";
    pmm::PersistMemoryManager<>::dump_stats( std::cout );

    // ─── Фаза D: Проверка данных ──────────────────────────────────────────────

    // Восстанавливаем pptr по сохранённым гранульным индексам
    pmm::pptr<uint8_t> q1( off1 );
    pmm::pptr<uint8_t> q2( off2 );

    std::cout << "\n[D] Проверка данных:\n";

    // Проверяем строку в p1
    bool data_ok = true;
    if ( std::strcmp( reinterpret_cast<const char*>( q1.get() ), "Hello, PersistMemoryManager!" ) == 0 )
    {
        std::cout << "  p1 (строка)  : OK — \"" << reinterpret_cast<const char*>( q1.get() ) << "\"\n";
    }
    else
    {
        std::cout << "  p1 (строка)  : FAIL — данные повреждены\n";
        data_ok = false;
    }

    // Проверяем массив чисел в p2
    bool arr_ok = true;
    int* q2_int = reinterpret_cast<int*>( q2.get() );
    for ( std::size_t i = 0; i < size2 / sizeof( int ); i++ )
    {
        if ( q2_int[i] != static_cast<int>( i * i ) )
        {
            arr_ok = false;
            break;
        }
    }
    std::cout << "  p2 (массив)  : " << ( arr_ok ? "OK" : "FAIL" ) << "\n";
    data_ok &= arr_ok;

    // ─── Фаза E: Продолжение работы ──────────────────────────────────────────

    std::cout << "\n[E] Продолжение работы с восстановленным менеджером:\n";

    pmm::pptr<uint8_t> p_new = pmm::PersistMemoryManager<>::allocate_typed<uint8_t>( 128 );
    if ( !p_new.is_null() )
    {
        std::memset( p_new.get(), 0xAB, 128 );
        std::cout << "  Новый блок выделен: offset=" << p_new.offset() << "\n";
        pmm::PersistMemoryManager<>::deallocate_typed( p_new );
        std::cout << "  Новый блок освобождён.\n";
    }
    else
    {
        std::cout << "  Не удалось выделить новый блок.\n";
        data_ok = false;
    }

    if ( pmm::PersistMemoryManager<>::validate() )
    {
        std::cout << "  Валидация финального состояния: OK\n";
    }
    else
    {
        std::cout << "  Валидация финального состояния: FAIL\n";
        data_ok = false;
    }

    // ─── Завершение ───────────────────────────────────────────────────────────

    // Issue #61: после destroy() нужно вручную освободить буфер
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    std::remove( IMAGE_FILE );

    std::cout << "\n=== Демонстрация завершена: " << ( data_ok ? "УСПЕШНО" : "ОШИБКА" ) << " ===\n";
    return data_ok ? 0 : 1;
}
