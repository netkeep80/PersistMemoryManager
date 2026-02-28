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
 */

#include "persist_memory_manager.h"

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

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, memory_size );
    if ( mgr1 == nullptr )
    {
        std::cerr << "Не удалось создать PersistMemoryManager\n";
        std::free( mem1 );
        return 1;
    }
    std::cout << "[A] Менеджер создан. Буфер: " << memory_size / 1024 << " КБ\n";

    // Выделяем несколько блоков
    const std::size_t size1 = 512;
    const std::size_t size2 = 1024;
    const std::size_t size3 = 256;

    void* p1 = mgr1->allocate( size1 );
    void* p2 = mgr1->allocate( size2, 32 ); // выравнивание 32
    void* p3 = mgr1->allocate( size3 );

    if ( p1 == nullptr || p2 == nullptr || p3 == nullptr )
    {
        std::cerr << "Ошибка выделения блоков\n";
        pmm::PersistMemoryManager::destroy();
        return 1;
    }

    // Записываем данные: p1 — строка, p2 — числа, p3 — паттерн
    std::strcpy( static_cast<char*>( p1 ), "Hello, PersistMemoryManager!" );
    int* arr2 = static_cast<int*>( p2 );
    for ( std::size_t i = 0; i < size2 / sizeof( int ); i++ )
    {
        arr2[i] = static_cast<int>( i * i );
    }
    std::memset( p3, 0xFF, size3 );

    std::cout << "[A] Выделено 3 блока. Данные записаны.\n";

    // Освобождаем p3 (чтобы показать, что свободные блоки тоже сохраняются)
    mgr1->deallocate( p3 );
    std::cout << "[A] Блок p3 освобождён (для демонстрации частично свободной кучи).\n";

    if ( !mgr1->validate() )
    {
        std::cerr << "Валидация перед сохранением провалилась\n";
        pmm::PersistMemoryManager::destroy();
        return 1;
    }

    std::cout << "\nСтатистика перед сохранением:\n";
    mgr1->dump_stats();

    // ─── Фаза B: Сохранение образа ───────────────────────────────────────────

    if ( !mgr1->save( IMAGE_FILE ) )
    {
        std::cerr << "Ошибка сохранения образа в файл: " << IMAGE_FILE << "\n";
        pmm::PersistMemoryManager::destroy();
        return 1;
    }
    std::cout << "\n[B] Образ сохранён в файл: " << IMAGE_FILE << "\n";

    // Запоминаем смещения выделенных указателей от начала буфера
    // (они останутся такими же в восстановленном образе)
    std::ptrdiff_t off1 = static_cast<std::uint8_t*>( p1 ) - static_cast<std::uint8_t*>( mem1 );
    std::ptrdiff_t off2 = static_cast<std::uint8_t*>( p2 ) - static_cast<std::uint8_t*>( mem1 );

    // Уничтожаем первый менеджер — имитируем завершение программы
    pmm::PersistMemoryManager::destroy();
    mgr1 = nullptr;
    std::cout << "[B] Первый менеджер уничтожен (имитация завершения программы).\n";

    // ─── Фаза C: Восстановление из файла ─────────────────────────────────────

    std::cout << "\n[C] Загрузка образа из файла...\n";

    void* mem2 = std::malloc( memory_size );
    if ( mem2 == nullptr )
    {
        std::cerr << "Не удалось выделить буфер для загрузки\n";
        return 1;
    }

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( IMAGE_FILE, mem2, memory_size );
    if ( mgr2 == nullptr )
    {
        std::cerr << "Не удалось загрузить образ из файла\n";
        std::free( mem2 );
        return 1;
    }

    if ( !mgr2->validate() )
    {
        std::cerr << "Валидация после загрузки провалилась\n";
        pmm::PersistMemoryManager::destroy();
        return 1;
    }

    std::cout << "[C] Образ успешно загружен и валиден.\n";
    std::cout << "\nСтатистика после загрузки:\n";
    mgr2->dump_stats();

    // ─── Фаза D: Проверка данных ──────────────────────────────────────────────

    // Восстанавливаем указатели по сохранённым смещениям
    char* q1 = reinterpret_cast<char*>( static_cast<std::uint8_t*>( mem2 ) + off1 );
    int*  q2 = reinterpret_cast<int*>( static_cast<std::uint8_t*>( mem2 ) + off2 );

    std::cout << "\n[D] Проверка данных:\n";

    // Проверяем строку в p1
    bool data_ok = true;
    if ( std::strcmp( q1, "Hello, PersistMemoryManager!" ) == 0 )
    {
        std::cout << "  p1 (строка)  : OK — \"" << q1 << "\"\n";
    }
    else
    {
        std::cout << "  p1 (строка)  : FAIL — данные повреждены\n";
        data_ok = false;
    }

    // Проверяем массив чисел в p2
    bool arr_ok = true;
    for ( std::size_t i = 0; i < size2 / sizeof( int ); i++ )
    {
        if ( q2[i] != static_cast<int>( i * i ) )
        {
            arr_ok = false;
            break;
        }
    }
    std::cout << "  p2 (массив)  : " << ( arr_ok ? "OK" : "FAIL" ) << "\n";
    data_ok &= arr_ok;

    // ─── Фаза E: Продолжение работы ──────────────────────────────────────────

    std::cout << "\n[E] Продолжение работы с восстановленным менеджером:\n";

    void* p_new = mgr2->allocate( 128 );
    if ( p_new != nullptr )
    {
        std::memset( p_new, 0xAB, 128 );
        std::cout << "  Новый блок выделен: " << p_new << "\n";
        mgr2->deallocate( p_new );
        std::cout << "  Новый блок освобождён.\n";
    }
    else
    {
        std::cout << "  Не удалось выделить новый блок.\n";
        data_ok = false;
    }

    if ( mgr2->validate() )
    {
        std::cout << "  Валидация финального состояния: OK\n";
    }
    else
    {
        std::cout << "  Валидация финального состояния: FAIL\n";
        data_ok = false;
    }

    // ─── Завершение ───────────────────────────────────────────────────────────

    pmm::PersistMemoryManager::destroy();
    std::remove( IMAGE_FILE );

    std::cout << "\n=== Демонстрация завершена: " << ( data_ok ? "УСПЕШНО" : "ОШИБКА" ) << " ===\n";
    return data_ok ? 0 : 1;
}
