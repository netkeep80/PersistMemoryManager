/**
 * @file test_allocate.cpp
 * @brief Тесты выделения памяти (Фаза 1)
 *
 * Проверяет корректность работы PersistMemoryManager::allocate():
 * - выделение блоков разных размеров;
 * - соблюдение выравнивания;
 * - возврат nullptr при нехватке памяти;
 * - проверка целостности структур после выделений.
 */

#include "persist_memory_manager.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

// ─── Вспомогательные макросы ──────────────────────────────────────────────────

#define PMM_TEST( expr )                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #expr << "\n";                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while ( false )

#define PMM_RUN( name, fn )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

// ─── Тестовые функции ─────────────────────────────────────────────────────────

/**
 * @brief create() возвращает ненулевой указатель при достаточном размере буфера.
 */
static bool test_create_basic()
{
    const std::size_t size = 64 * 1024; // 64 КБ
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief create() возвращает nullptr при слишком маленьком буфере.
 */
static bool test_create_too_small()
{
    const std::size_t size = 128; // Меньше kMinMemorySize (4096)
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr == nullptr );

    std::free( mem );
    return true;
}

/**
 * @brief create() возвращает nullptr при nullptr-буфере.
 */
static bool test_create_null()
{
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( nullptr, 64 * 1024 );
    PMM_TEST( mgr == nullptr );
    return true;
}

/**
 * @brief Выделение одного блока небольшого размера.
 */
static bool test_allocate_single_small()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 64 );
    PMM_TEST( ptr != nullptr );
    // Должен быть выровнен на 16 байт по умолчанию
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr ) % 16 == 0 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Выделение блока с нестандартным выравниванием (32 байта).
 */
static bool test_allocate_alignment_32()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 128, 32 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr ) % 32 == 0 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Выделение блока с нестандартным выравниванием (64 байта).
 */
static bool test_allocate_alignment_64()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 256, 64 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr ) % 64 == 0 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Выделение нескольких блоков — статистика корректна.
 */
static bool test_allocate_multiple()
{
    const std::size_t size = 256 * 1024; // 256 КБ
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int num = 10;
    void*     ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = mgr->allocate( 1024 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    PMM_TEST( mgr->validate() );

    // Все указатели разные
    for ( int i = 0; i < num; i++ )
    {
        for ( int j = i + 1; j < num; j++ )
        {
            PMM_TEST( ptrs[i] != ptrs[j] );
        }
    }

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief allocate(0) возвращает nullptr.
 */
static bool test_allocate_zero()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 0 );
    PMM_TEST( ptr == nullptr );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Запрос на слишком большой блок возвращает nullptr.
 */
static bool test_allocate_out_of_memory()
{
    const std::size_t size = 8 * 1024; // 8 КБ
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 100 * 1024 * 1024 ); // 100 МБ — не влезет
    PMM_TEST( ptr == nullptr );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Неверное выравнивание (не степень двойки) возвращает nullptr.
 */
static bool test_allocate_invalid_alignment()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 64, 17 ); // 17 — не степень двойки
    PMM_TEST( ptr == nullptr );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Запись и чтение данных из выделенного блока — нет перекрытий.
 */
static bool test_allocate_write_read()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr1 = mgr->allocate( 128 );
    void* ptr2 = mgr->allocate( 256 );
    PMM_TEST( ptr1 != nullptr );
    PMM_TEST( ptr2 != nullptr );

    // Записываем паттерн в первый блок
    std::memset( ptr1, 0xAA, 128 );
    // Записываем паттерн во второй блок
    std::memset( ptr2, 0xBB, 256 );

    // Проверяем, что данные не перекрылись
    const std::uint8_t* p1 = static_cast<const std::uint8_t*>( ptr1 );
    const std::uint8_t* p2 = static_cast<const std::uint8_t*>( ptr2 );
    for ( std::size_t i = 0; i < 128; i++ )
    {
        PMM_TEST( p1[i] == 0xAA );
    }
    for ( std::size_t i = 0; i < 256; i++ )
    {
        PMM_TEST( p2[i] == 0xBB );
    }

    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief total_size(), used_size(), free_size() возвращают корректные значения.
 */
static bool test_allocate_metrics()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    PMM_TEST( mgr->total_size() == size );
    PMM_TEST( mgr->used_size() > 0 );
    PMM_TEST( mgr->free_size() < size );
    PMM_TEST( mgr->used_size() + mgr->free_size() <= size );

    std::size_t used_before = mgr->used_size();

    void* ptr = mgr->allocate( 512 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( mgr->used_size() > used_before );

    mgr->destroy();
    std::free( mem );
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_allocate ===\n";
    bool all_passed = true;

    PMM_RUN( "create_basic", test_create_basic );
    PMM_RUN( "create_too_small", test_create_too_small );
    PMM_RUN( "create_null", test_create_null );
    PMM_RUN( "allocate_single_small", test_allocate_single_small );
    PMM_RUN( "allocate_alignment_32", test_allocate_alignment_32 );
    PMM_RUN( "allocate_alignment_64", test_allocate_alignment_64 );
    PMM_RUN( "allocate_multiple", test_allocate_multiple );
    PMM_RUN( "allocate_zero", test_allocate_zero );
    PMM_RUN( "allocate_out_of_memory", test_allocate_out_of_memory );
    PMM_RUN( "allocate_invalid_alignment", test_allocate_invalid_alignment );
    PMM_RUN( "allocate_write_read", test_allocate_write_read );
    PMM_RUN( "allocate_metrics", test_allocate_metrics );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
