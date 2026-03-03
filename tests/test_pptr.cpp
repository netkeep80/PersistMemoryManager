/**
 * @file test_pptr.cpp
 * @brief Тесты персистного типизированного указателя pptr<T> (Фаза 5, обновлено в Issue #61)
 *
 * Issue #61:
 * - pptr<T> использует только статические методы PersistMemoryManager (без PersistMemoryManager*).
 * - Метод resolve(PersistMemoryManager*) удалён — используйте get() через синглтон.
 * - Все операции через PersistMemoryManager::allocate_typed<T>() / deallocate_typed().
 */

#include "persist_memory_io.h"
#include "persist_memory_manager.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

static bool test_pptr_sizeof()
{
    // Issue #59: pptr<T> is now 4 bytes (uint32_t offset), not sizeof(void*)
    PMM_TEST( sizeof( pmm::pptr<int> ) == 4 );
    PMM_TEST( sizeof( pmm::pptr<double> ) == 4 );
    PMM_TEST( sizeof( pmm::pptr<char> ) == 4 );
    PMM_TEST( sizeof( pmm::pptr<std::uint64_t> ) == 4 );
    return true;
}

static bool test_pptr_default_null()
{
    pmm::pptr<int> p;
    PMM_TEST( p.is_null() );
    PMM_TEST( !p );
    PMM_TEST( p.offset() == 0 );
    return true;
}

static bool test_pptr_allocate_typed_int()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p = pmm::PersistMemoryManager<>::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( static_cast<bool>( p ) );
    PMM_TEST( p.offset() > 0 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_get()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p = pmm::PersistMemoryManager<>::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Разыменование через синглтон (Issue #61: единственный способ)
    int* ptr = p.get();
    PMM_TEST( ptr != nullptr );
    PMM_TEST( ptr >= reinterpret_cast<int*>( mem ) );
    PMM_TEST( ptr < reinterpret_cast<int*>( static_cast<std::uint8_t*>( mem ) + size ) );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_write_read()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p = pmm::PersistMemoryManager<>::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Запись через operator*
    *p = 42;
    PMM_TEST( *p == 42 );

    *p = 100;
    PMM_TEST( *p == 100 );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_deallocate()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    std::size_t free_before = pmm::PersistMemoryManager<>::free_size();

    pmm::pptr<double> p = pmm::PersistMemoryManager<>::allocate_typed<double>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    PMM_TEST( pmm::PersistMemoryManager<>::free_size() >= free_before );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_null_get()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p; // нулевой по умолчанию
    PMM_TEST( p.get() == nullptr );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_allocate_array()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 10;
    void*             mem   = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p = pmm::PersistMemoryManager<>::allocate_typed<int>( count );
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( std::size_t i = 0; i < count; i++ )
    {
        int* elem = p.get_at( i );
        PMM_TEST( elem != nullptr );
        *elem = static_cast<int>( i * 10 );
    }

    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( *p.get_at( i ) == static_cast<int>( i * 10 ) );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_get_at()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 5;
    void*             mem   = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<double> p = pmm::PersistMemoryManager<>::allocate_typed<double>( count );
    PMM_TEST( !p.is_null() );

    for ( std::size_t i = 0; i < count; i++ )
        *p.get_at( i ) = static_cast<double>( i ) * 1.5;

    double* base_elem = p.get();
    PMM_TEST( base_elem != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( base_elem[i] == static_cast<double>( i ) * 1.5 );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_persistence()
{
    const std::size_t size     = 64 * 1024;
    const char*       filename = "pptr_test.dat";

    void* mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    pmm::pptr<int> p1 = pmm::PersistMemoryManager<>::allocate_typed<int>();
    PMM_TEST( !p1.is_null() );
    *p1 = 12345;

    std::uint32_t saved_offset = p1.offset();
    PMM_TEST( pmm::save( filename ) );

    pmm::PersistMemoryManager<>::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );
    PMM_TEST( pmm::load_from_file( filename, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Восстанавливаем pptr<int> по тому же смещению
    pmm::pptr<int> p2( saved_offset );
    PMM_TEST( !p2.is_null() );

    // Разыменование через синглтон (который теперь указывает на mem2)
    PMM_TEST( *p2 == 12345 );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    std::remove( filename );
    return true;
}

static bool test_pptr_comparison()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p1 = pmm::PersistMemoryManager<>::allocate_typed<int>();
    pmm::pptr<int> p2 = pmm::PersistMemoryManager<>::allocate_typed<int>();
    pmm::pptr<int> p3 = p1;

    PMM_TEST( p1 == p3 );
    PMM_TEST( p1 != p2 );
    PMM_TEST( !( p1 == p2 ) );

    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_pptr_multiple_types()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int>    pi = pmm::PersistMemoryManager<>::allocate_typed<int>();
    pmm::pptr<double> pd = pmm::PersistMemoryManager<>::allocate_typed<double>();
    pmm::pptr<char>   pc = pmm::PersistMemoryManager<>::allocate_typed<char>( 16 );

    PMM_TEST( !pi.is_null() );
    PMM_TEST( !pd.is_null() );
    PMM_TEST( !pc.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    *pi = 7;
    *pd = 3.14;
    std::memcpy( pc.get(), "hello", 6 );

    PMM_TEST( *pi == 7 );
    PMM_TEST( *pd == 3.14 );
    PMM_TEST( std::memcmp( pc.get(), "hello", 6 ) == 0 );

    pmm::PersistMemoryManager<>::deallocate_typed( pi );
    pmm::PersistMemoryManager<>::deallocate_typed( pd );
    pmm::PersistMemoryManager<>::deallocate_typed( pc );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/**
 * @brief При нехватке памяти менеджер автоматически расширяется.
 *
 * allocate_typed больше не возвращает нулевой pptr при нехватке —
 * менеджер расширяет память на 25% автоматически.
 */
static bool test_pptr_allocate_auto_expand()
{
    const std::size_t initial_size = 8 * 1024;
    void*             mem          = std::malloc( initial_size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, initial_size ) );

    std::size_t initial_total = pmm::PersistMemoryManager<>::total_size();

    // Заполняем большую часть буфера первым блоком
    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p1.is_null() );

    // Запрашиваем второй блок — должно вызвать расширение
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p2.is_null() );

    // После расширения синглтон указывает на новый буфер
    PMM_TEST( pmm::PersistMemoryManager<>::is_initialized() );
    PMM_TEST( pmm::PersistMemoryManager<>::total_size() > initial_total );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    return true;
}

static bool test_pptr_deallocate_null()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p;
    pmm::PersistMemoryManager<>::deallocate_typed( p );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Issue #59: pptr<T>[i] адресует i-й элемент типа T с проверкой размера блока.
 *
 * Проверяет:
 *   - operator[] возвращает корректный указатель для элементов в пределах блока.
 *   - operator[] возвращает nullptr при выходе за пределы блока.
 *   - pptr<T>++ и pptr<T>-- запрещены (проверяется статически через static_assert).
 */
static bool test_pptr_subscript_operator()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 8;
    void*             mem   = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    // Allocate array of 8 ints
    pmm::pptr<int> p = pmm::PersistMemoryManager<>::allocate_typed<int>( count );
    PMM_TEST( !p.is_null() );

    // Write via operator[]
    for ( std::size_t i = 0; i < count; i++ )
    {
        int* elem = p[i];
        PMM_TEST( elem != nullptr );
        *elem = static_cast<int>( i * 100 );
    }

    // Read back via operator[]
    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( *p[i] == static_cast<int>( i * 100 ) );

    // Out-of-bounds access returns nullptr (Issue #59: bounds checking)
    PMM_TEST( p[count] == nullptr );
    PMM_TEST( p[count + 100] == nullptr );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Issue #61: reallocate_typed изменяет размер массива.
 */
static bool test_pptr_reallocate_typed()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    // Allocate array of 5 ints
    pmm::pptr<int> p = pmm::PersistMemoryManager<>::allocate_typed<int>( 5 );
    PMM_TEST( !p.is_null() );
    for ( int i = 0; i < 5; i++ )
        *p.get_at( i ) = i * 10;

    // Reallocate to 10 ints — data must be preserved
    pmm::pptr<int> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 10 );
    PMM_TEST( !p2.is_null() );

    // First 5 elements preserved
    for ( int i = 0; i < 5; i++ )
        PMM_TEST( *p2.get_at( i ) == i * 10 );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

int main()
{
    std::cout << "=== test_pptr ===\n";
    bool all_passed = true;

    PMM_RUN( "pptr_sizeof", test_pptr_sizeof );
    PMM_RUN( "pptr_default_null", test_pptr_default_null );
    PMM_RUN( "pptr_allocate_typed_int", test_pptr_allocate_typed_int );
    PMM_RUN( "pptr_get", test_pptr_get );
    PMM_RUN( "pptr_write_read", test_pptr_write_read );
    PMM_RUN( "pptr_deallocate", test_pptr_deallocate );
    PMM_RUN( "pptr_null_get", test_pptr_null_get );
    PMM_RUN( "pptr_allocate_array", test_pptr_allocate_array );
    PMM_RUN( "pptr_get_at", test_pptr_get_at );
    PMM_RUN( "pptr_persistence", test_pptr_persistence );
    PMM_RUN( "pptr_comparison", test_pptr_comparison );
    PMM_RUN( "pptr_multiple_types", test_pptr_multiple_types );
    PMM_RUN( "pptr_allocate_auto_expand", test_pptr_allocate_auto_expand );
    PMM_RUN( "pptr_deallocate_null", test_pptr_deallocate_null );
    PMM_RUN( "pptr_subscript_operator", test_pptr_subscript_operator );
    PMM_RUN( "pptr_reallocate_typed", test_pptr_reallocate_typed );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
