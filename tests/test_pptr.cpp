/**
 * @file test_pptr.cpp
 * @brief Тесты персистного типизированного указателя pptr<T> (Фаза 5, обновлено в Фазе 7)
 *
 * Фаза 7:
 * - pptr<T> разыменовывается без явного менеджера через operator* и operator->
 * - Синглтон устанавливается автоматически в create() и load()
 * - destroy() освобождает буфер
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
    PMM_TEST( sizeof( pmm::pptr<int> ) == sizeof( void* ) );
    PMM_TEST( sizeof( pmm::pptr<double> ) == sizeof( void* ) );
    PMM_TEST( sizeof( pmm::pptr<char> ) == sizeof( void* ) );
    PMM_TEST( sizeof( pmm::pptr<std::uint64_t> ) == sizeof( void* ) );
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
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int> p = mgr->allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( static_cast<bool>( p ) );
    PMM_TEST( p.offset() > 0 );
    PMM_TEST( mgr->validate() );

    mgr->deallocate_typed( p );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_resolve()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int> p = mgr->allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Разыменование через синглтон (новый API)
    int* ptr = p.get();
    PMM_TEST( ptr != nullptr );
    PMM_TEST( ptr >= reinterpret_cast<int*>( mem ) );
    PMM_TEST( ptr < reinterpret_cast<int*>( static_cast<std::uint8_t*>( mem ) + size ) );

    // Разыменование через явный менеджер (обратная совместимость)
    int* ptr2 = p.resolve( mgr );
    PMM_TEST( ptr2 == ptr );

    mgr->deallocate_typed( p );
    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_write_read()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int> p = mgr->allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Запись через operator*
    *p = 42;
    PMM_TEST( *p == 42 );

    *p = 100;
    PMM_TEST( *p == 100 );

    mgr->deallocate_typed( p );
    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_deallocate()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    std::size_t free_before = mgr->free_size();

    pmm::pptr<double> p = mgr->allocate_typed<double>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( mgr->validate() );

    mgr->deallocate_typed( p );
    PMM_TEST( mgr->validate() );

    PMM_TEST( mgr->free_size() >= free_before );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_resolve_null()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int> p; // нулевой по умолчанию
    PMM_TEST( p.get() == nullptr );

    // resolve() с nullptr менеджером тоже возвращает nullptr
    pmm::pptr<int>             p2       = mgr->allocate_typed<int>();
    pmm::PersistMemoryManager* null_mgr = nullptr;
    PMM_TEST( p2.resolve( null_mgr ) == nullptr );

    mgr->deallocate_typed( p2 );
    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_allocate_array()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 10;
    void*             mem   = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int> p = mgr->allocate_typed<int>( count );
    PMM_TEST( !p.is_null() );
    PMM_TEST( mgr->validate() );

    for ( std::size_t i = 0; i < count; i++ )
    {
        int* elem = p.get_at( i );
        PMM_TEST( elem != nullptr );
        *elem = static_cast<int>( i * 10 );
    }

    for ( std::size_t i = 0; i < count; i++ )
    {
        PMM_TEST( *p.get_at( i ) == static_cast<int>( i * 10 ) );
    }

    mgr->deallocate_typed( p );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_resolve_at()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 5;
    void*             mem   = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<double> p = mgr->allocate_typed<double>( count );
    PMM_TEST( !p.is_null() );

    for ( std::size_t i = 0; i < count; i++ )
    {
        *p.get_at( i ) = static_cast<double>( i ) * 1.5;
    }

    double* base_elem = p.get();
    PMM_TEST( base_elem != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
    {
        PMM_TEST( base_elem[i] == static_cast<double>( i ) * 1.5 );
    }

    mgr->deallocate_typed( p );
    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_persistence()
{
    const std::size_t size     = 64 * 1024;
    const char*       filename = "pptr_test.dat";

    void* mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );
    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    pmm::pptr<int> p1 = mgr1->allocate_typed<int>();
    PMM_TEST( !p1.is_null() );
    *p1 = 12345;

    std::ptrdiff_t saved_offset = p1.offset();
    PMM_TEST( pmm::save( mgr1, filename ) );

    pmm::PersistMemoryManager::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );
    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( filename, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    // Восстанавливаем pptr<int> по тому же смещению
    pmm::pptr<int> p2( saved_offset );
    PMM_TEST( !p2.is_null() );

    // Разыменование через синглтон (который теперь указывает на mgr2)
    PMM_TEST( *p2 == 12345 );

    mgr2->deallocate_typed( p2 );
    pmm::PersistMemoryManager::destroy();
    std::remove( filename );
    return true;
}

static bool test_pptr_comparison()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int> p1 = mgr->allocate_typed<int>();
    pmm::pptr<int> p2 = mgr->allocate_typed<int>();
    pmm::pptr<int> p3 = p1;

    PMM_TEST( p1 == p3 );
    PMM_TEST( p1 != p2 );
    PMM_TEST( !( p1 == p2 ) );

    mgr->deallocate_typed( p1 );
    mgr->deallocate_typed( p2 );
    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_multiple_types()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int>    pi = mgr->allocate_typed<int>();
    pmm::pptr<double> pd = mgr->allocate_typed<double>();
    pmm::pptr<char>   pc = mgr->allocate_typed<char>( 16 );

    PMM_TEST( !pi.is_null() );
    PMM_TEST( !pd.is_null() );
    PMM_TEST( !pc.is_null() );
    PMM_TEST( mgr->validate() );

    *pi = 7;
    *pd = 3.14;
    std::memcpy( pc.get(), "hello", 6 );

    PMM_TEST( *pi == 7 );
    PMM_TEST( *pd == 3.14 );
    PMM_TEST( std::memcmp( pc.get(), "hello", 6 ) == 0 );

    mgr->deallocate_typed( pi );
    mgr->deallocate_typed( pd );
    mgr->deallocate_typed( pc );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief Фаза 7: при нехватке памяти менеджер автоматически расширяется.
 *
 * allocate_typed больше не возвращает нулевой pptr при нехватке —
 * менеджер расширяет память на 25% автоматически.
 */
static bool test_pptr_allocate_auto_expand()
{
    // Используем 8 КБ буфер — как в test_allocate_auto_expand
    const std::size_t initial_size = 8 * 1024;
    void*             mem          = std::malloc( initial_size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    PMM_TEST( mgr != nullptr );

    std::size_t initial_total = mgr->total_size();

    // Заполняем большую часть буфера первым блоком
    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager::instance()->allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p1.is_null() );

    // Запрашиваем второй блок — должно вызвать расширение
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager::instance()->allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p2.is_null() );

    // После расширения синглтон указывает на новый буфер
    pmm::PersistMemoryManager* mgr2 = pmm::PersistMemoryManager::instance();
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->total_size() > initial_total );
    PMM_TEST( mgr2->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_pptr_deallocate_null()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    pmm::pptr<int> p;
    mgr->deallocate_typed( p );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

int main()
{
    std::cout << "=== test_pptr ===\n";
    bool all_passed = true;

    PMM_RUN( "pptr_sizeof", test_pptr_sizeof );
    PMM_RUN( "pptr_default_null", test_pptr_default_null );
    PMM_RUN( "pptr_allocate_typed_int", test_pptr_allocate_typed_int );
    PMM_RUN( "pptr_resolve", test_pptr_resolve );
    PMM_RUN( "pptr_write_read", test_pptr_write_read );
    PMM_RUN( "pptr_deallocate", test_pptr_deallocate );
    PMM_RUN( "pptr_resolve_null", test_pptr_resolve_null );
    PMM_RUN( "pptr_allocate_array", test_pptr_allocate_array );
    PMM_RUN( "pptr_resolve_at", test_pptr_resolve_at );
    PMM_RUN( "pptr_persistence", test_pptr_persistence );
    PMM_RUN( "pptr_comparison", test_pptr_comparison );
    PMM_RUN( "pptr_multiple_types", test_pptr_multiple_types );
    PMM_RUN( "pptr_allocate_auto_expand", test_pptr_allocate_auto_expand );
    PMM_RUN( "pptr_deallocate_null", test_pptr_deallocate_null );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
