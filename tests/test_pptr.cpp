/**
 * @file test_pptr.cpp
 * @brief Тесты персистентного типизированного указателя pptr<T, ManagerT> (Issue #102 — новый API)
 *
 * Issue #102: использует AbstractPersistMemoryManager через pmm_presets.h.
 *   - pptr<T, ManagerT> без ManagerT=void по умолчанию.
 *   - Разыменование через *p и p->field, не через p.resolve().
 *   - Нет operator*, operator->, get_at(), operator[].
 *   - Нет reallocate_typed() в новом API.
 * Issue #164: удалены избыточные методы pptr (get_tree_left и др.),
 *   используется tree_node() API для работы с узлами AVL-дерева.
 */

#include "pmm_single_threaded_heap.h"

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

using Mgr = pmm::presets::SingleThreadedHeap;

static bool test_pptr_sizeof()
{
    // Issue #102: pptr<T, ManagerT> is 4 bytes (uint32_t granule index)
    PMM_TEST( sizeof( Mgr::pptr<int> ) == 4 );
    PMM_TEST( sizeof( Mgr::pptr<double> ) == 4 );
    PMM_TEST( sizeof( Mgr::pptr<char> ) == 4 );
    PMM_TEST( sizeof( Mgr::pptr<std::uint64_t> ) == 4 );
    return true;
}

static bool test_pptr_default_null()
{
    Mgr::pptr<int> p;
    PMM_TEST( p.is_null() );
    PMM_TEST( !p );
    PMM_TEST( p.offset() == 0 );
    return true;
}

static bool test_pptr_allocate_typed_int()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( static_cast<bool>( p ) );
    PMM_TEST( p.offset() > 0 );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_resolve()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Разыменование через *p и p->field
    *p = 42;
    PMM_TEST( *p == 42 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_write_read()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Write/read via operator*
    *p = 42;
    PMM_TEST( *p == 42 );

    *p = 100;
    PMM_TEST( *p == 100 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_deallocate()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    std::size_t free_before = pmm.free_size();

    Mgr::pptr<double> p = pmm.allocate_typed<double>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    PMM_TEST( pmm.free_size() >= free_before );

    pmm.destroy();
    return true;
}

static bool test_pptr_null_resolve()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p; // null by default
    PMM_TEST( !p );   // null pptr evaluates to false

    pmm.destroy();
    return true;
}

static bool test_pptr_allocate_array()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 10;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>( count );
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm.is_initialized() );

    int* arr = pmm.resolve_at( p, 0 );
    PMM_TEST( arr != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<int>( i * 10 );

    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( pmm.resolve_at( p, i )[0] == static_cast<int>( i * 10 ) );

    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

static bool test_pptr_resolve_at()
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 5;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<double> p = pmm.allocate_typed<double>( count );
    PMM_TEST( !p.is_null() );

    double* arr = pmm.resolve_at( p, 0 );
    PMM_TEST( arr != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<double>( i ) * 1.5;

    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( pmm.resolve_at( p, i )[0] == static_cast<double>( i ) * 1.5 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_pptr_persistence()
{
    const std::size_t size     = 64 * 1024;
    const char*       filename = "pptr_test.dat";

    // Use distinct InstanceId values to simulate two separate manager "sessions"
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 200>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 201>;

    PMM_TEST( Mgr1::create( size ) );

    Mgr1::pptr<int> p1 = Mgr1::allocate_typed<int>();
    PMM_TEST( !p1.is_null() );
    *p1 = 12345;

    std::uint32_t saved_offset = p1.offset();
    PMM_TEST( pmm::save_manager<Mgr1>( filename ) );
    Mgr1::destroy();

    PMM_TEST( Mgr2::create( size ) );
    PMM_TEST( pmm::load_manager_from_file<Mgr2>( filename ) );
    PMM_TEST( Mgr2::is_initialized() );

    // Restore pptr by saved offset
    Mgr2::pptr<int> p2( saved_offset );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( *p2 == 12345 );

    Mgr2::deallocate_typed( p2 );
    Mgr2::destroy();
    std::remove( filename );
    return true;
}

static bool test_pptr_comparison()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> p2 = pmm.allocate_typed<int>();
    Mgr::pptr<int> p3 = p1;

    PMM_TEST( p1 == p3 );
    PMM_TEST( p1 != p2 );
    PMM_TEST( !( p1 == p2 ) );

    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    pmm.destroy();
    return true;
}

static bool test_pptr_multiple_types()
{
    const std::size_t size = 256 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int>    pi = pmm.allocate_typed<int>();
    Mgr::pptr<double> pd = pmm.allocate_typed<double>();
    Mgr::pptr<char>   pc = pmm.allocate_typed<char>( 16 );

    PMM_TEST( !pi.is_null() );
    PMM_TEST( !pd.is_null() );
    PMM_TEST( !pc.is_null() );
    PMM_TEST( pmm.is_initialized() );

    *pi = 7;
    *pd = 3.14;
    std::memcpy( pmm.resolve_at( pc, 0 ), "hello", 6 );

    PMM_TEST( *pi == 7 );
    PMM_TEST( *pd == 3.14 );
    PMM_TEST( std::memcmp( pmm.resolve_at( pc, 0 ), "hello", 6 ) == 0 );

    pmm.deallocate_typed( pi );
    pmm.deallocate_typed( pd );
    pmm.deallocate_typed( pc );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/**
 * @brief При нехватке памяти менеджер (HeapStorage) автоматически расширяется.
 *
 * Uses unique InstanceId (202) to start with a fresh backend of exactly 8K.
 */
static bool test_pptr_allocate_auto_expand()
{
    using MgrExpand = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 202>;

    const std::size_t initial_size = 8 * 1024;

    PMM_TEST( MgrExpand::create( initial_size ) );

    std::size_t initial_total = MgrExpand::total_size();

    // Fill most of initial buffer
    MgrExpand::pptr<std::uint8_t> p1 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p1.is_null() );

    // Request second large block — should trigger expansion
    MgrExpand::pptr<std::uint8_t> p2 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p2.is_null() );

    PMM_TEST( MgrExpand::is_initialized() );
    PMM_TEST( MgrExpand::total_size() > initial_total );

    MgrExpand::destroy();
    return true;
}

static bool test_pptr_deallocate_null()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p;
    pmm.deallocate_typed( p ); // deallocating null should be safe
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/**
 * @brief Тест методов работы с узлом AVL-дерева через pptr::tree_node() (Issue #164).
 *
 * Проверяет начальное состояние (нет связей), установку и чтение
 * левого/правого/родительского потомков дерева через tree_node() API.
 */
static bool test_pptr_tree_node_links()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> node1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> node2 = pmm.allocate_typed<int>();
    Mgr::pptr<int> node3 = pmm.allocate_typed<int>();

    PMM_TEST( !node1.is_null() );
    PMM_TEST( !node2.is_null() );
    PMM_TEST( !node3.is_null() );

    const auto no_block = pmm::DefaultAddressTraits::no_block;

    // Начальное состояние: нет связей (no_block в полях tree_node)
    PMM_TEST( node1.tree_node().get_left() == no_block );
    PMM_TEST( node1.tree_node().get_right() == no_block );
    PMM_TEST( node1.tree_node().get_parent() == no_block );
    PMM_TEST( node2.tree_node().get_left() == no_block );
    PMM_TEST( node2.tree_node().get_right() == no_block );
    PMM_TEST( node2.tree_node().get_parent() == no_block );

    // Построить дерево: node1 — корень, node2 — левый, node3 — правый
    node1.tree_node().set_left( node2.offset() );
    node1.tree_node().set_right( node3.offset() );
    node2.tree_node().set_parent( node1.offset() );
    node3.tree_node().set_parent( node1.offset() );

    // Проверить связи
    PMM_TEST( node1.tree_node().get_left() == node2.offset() );
    PMM_TEST( node1.tree_node().get_right() == node3.offset() );
    PMM_TEST( node2.tree_node().get_parent() == node1.offset() );
    PMM_TEST( node3.tree_node().get_parent() == node1.offset() );

    // Потомки node2 и node3 по-прежнему null (no_block)
    PMM_TEST( node2.tree_node().get_left() == no_block );
    PMM_TEST( node2.tree_node().get_right() == no_block );
    PMM_TEST( node3.tree_node().get_left() == no_block );
    PMM_TEST( node3.tree_node().get_right() == no_block );

    pmm.deallocate_typed( node1 );
    pmm.deallocate_typed( node2 );
    pmm.deallocate_typed( node3 );
    pmm.destroy();
    return true;
}

/**
 * @brief Тест высоты AVL-узла через pptr::tree_node() (Issue #164).
 */
static bool test_pptr_tree_node_height()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> node1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> node2 = pmm.allocate_typed<int>();

    PMM_TEST( !node1.is_null() );
    PMM_TEST( !node2.is_null() );

    // Начальная высота = 0 (узел не в дереве после выделения)
    PMM_TEST( node1.tree_node().get_height() == 0 );
    PMM_TEST( node2.tree_node().get_height() == 0 );

    // Установить высоту
    node1.tree_node().set_height( 2 );
    node2.tree_node().set_height( 1 );

    PMM_TEST( node1.tree_node().get_height() == 2 );
    PMM_TEST( node2.tree_node().get_height() == 1 );

    pmm.deallocate_typed( node1 );
    pmm.deallocate_typed( node2 );
    pmm.destroy();
    return true;
}

/**
 * @brief Тест методов дерева — получение веса блока (Issue #164).
 */
static bool test_pptr_tree_node_weight()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Для выделенного блока вес > 0 (размер данных в гранулах)
    PMM_TEST( p.tree_node().get_weight() > 0 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

/**
 * @brief Тест использования pptr для пользовательского AVL-дерева (Issue #164).
 *
 * Строит простое бинарное дерево поиска из 5 узлов через tree_node() API,
 * проверяет корректность связей и высот.
 */
static bool test_pptr_user_avl_tree()
{
    using LocalMgr         = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 203>;
    const std::size_t size = 128 * 1024;

    PMM_TEST( LocalMgr::create( size ) );

    // Выделить 5 узлов для дерева
    LocalMgr::pptr<int> n1 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n2 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n3 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n4 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n5 = LocalMgr::allocate_typed<int>();

    PMM_TEST( !n1.is_null() );
    PMM_TEST( !n2.is_null() );
    PMM_TEST( !n3.is_null() );
    PMM_TEST( !n4.is_null() );
    PMM_TEST( !n5.is_null() );

    // Записать данные в узлы
    *n1 = 30;
    *n2 = 20;
    *n3 = 40;
    *n4 = 10;
    *n5 = 25;

    const auto no_block = pmm::DefaultAddressTraits::no_block;

    // Построить AVL-дерево вручную:
    //       n1(30)
    //      L       R
    //   n2(20)   n3(40)
    //   L    R
    // n4(10) n5(25)

    n1.tree_node().set_left( n2.offset() );
    n1.tree_node().set_right( n3.offset() );
    n1.tree_node().set_parent( no_block );
    n1.tree_node().set_height( 3 );

    n2.tree_node().set_left( n4.offset() );
    n2.tree_node().set_right( n5.offset() );
    n2.tree_node().set_parent( n1.offset() );
    n2.tree_node().set_height( 2 );

    n3.tree_node().set_left( no_block );
    n3.tree_node().set_right( no_block );
    n3.tree_node().set_parent( n1.offset() );
    n3.tree_node().set_height( 1 );

    n4.tree_node().set_left( no_block );
    n4.tree_node().set_right( no_block );
    n4.tree_node().set_parent( n2.offset() );
    n4.tree_node().set_height( 1 );

    n5.tree_node().set_left( no_block );
    n5.tree_node().set_right( no_block );
    n5.tree_node().set_parent( n2.offset() );
    n5.tree_node().set_height( 1 );

    // Проверить структуру дерева
    PMM_TEST( n1.tree_node().get_left() == n2.offset() );
    PMM_TEST( n1.tree_node().get_right() == n3.offset() );
    PMM_TEST( n1.tree_node().get_parent() == no_block );
    PMM_TEST( n1.tree_node().get_height() == 3 );

    PMM_TEST( n2.tree_node().get_left() == n4.offset() );
    PMM_TEST( n2.tree_node().get_right() == n5.offset() );
    PMM_TEST( n2.tree_node().get_parent() == n1.offset() );
    PMM_TEST( n2.tree_node().get_height() == 2 );

    PMM_TEST( n3.tree_node().get_left() == no_block );
    PMM_TEST( n3.tree_node().get_right() == no_block );
    PMM_TEST( n3.tree_node().get_parent() == n1.offset() );
    PMM_TEST( n3.tree_node().get_height() == 1 );

    // Проверить данные в узлах (не затронуты операциями дерева)
    PMM_TEST( *n1 == 30 );
    PMM_TEST( *n2 == 20 );
    PMM_TEST( *n3 == 40 );
    PMM_TEST( *n4 == 10 );
    PMM_TEST( *n5 == 25 );

    LocalMgr::deallocate_typed( n1 );
    LocalMgr::deallocate_typed( n2 );
    LocalMgr::deallocate_typed( n3 );
    LocalMgr::deallocate_typed( n4 );
    LocalMgr::deallocate_typed( n5 );
    LocalMgr::destroy();
    return true;
}

/**
 * @brief Тест pptr::tree_node() — прямой доступ к TreeNode через ссылку (Issue #138, #164).
 *
 * Проверяет, что pptr::tree_node() возвращает ссылку на TreeNode в заголовке блока,
 * и что TreeNode-методы (get_left, set_left, get_right, set_right, get_parent,
 * set_parent, get_height, set_height) работают корректно.
 */
static bool test_pptr_tree_node_ref()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<int> n1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> n2 = pmm.allocate_typed<int>();
    Mgr::pptr<int> n3 = pmm.allocate_typed<int>();

    PMM_TEST( !n1.is_null() );
    PMM_TEST( !n2.is_null() );
    PMM_TEST( !n3.is_null() );

    // Доступ к TreeNode через pptr::tree_node()
    auto& tn1 = n1.tree_node();
    auto& tn2 = n2.tree_node();
    auto& tn3 = n3.tree_node();

    // Начальное состояние: связи через tree_node() — no_block
    const auto no_block = pmm::DefaultAddressTraits::no_block;
    PMM_TEST( tn1.get_left() == no_block );
    PMM_TEST( tn1.get_right() == no_block );
    PMM_TEST( tn1.get_parent() == no_block );
    PMM_TEST( tn2.get_left() == no_block );
    PMM_TEST( tn2.get_right() == no_block );

    // Построить дерево через tree_node(): n1 — корень, n2 — левый, n3 — правый
    tn1.set_left( n2.offset() );
    tn1.set_right( n3.offset() );
    tn2.set_parent( n1.offset() );
    tn3.set_parent( n1.offset() );
    tn1.set_height( 2 );
    tn2.set_height( 1 );
    tn3.set_height( 1 );

    // Проверить через tree_node()
    PMM_TEST( tn1.get_left() == n2.offset() );
    PMM_TEST( tn1.get_right() == n3.offset() );
    PMM_TEST( tn2.get_parent() == n1.offset() );
    PMM_TEST( tn3.get_parent() == n1.offset() );
    PMM_TEST( tn1.get_height() == 2 );
    PMM_TEST( tn2.get_height() == 1 );
    PMM_TEST( tn3.get_height() == 1 );

    pmm.deallocate_typed( n1 );
    pmm.deallocate_typed( n2 );
    pmm.deallocate_typed( n3 );
    pmm.destroy();
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
    PMM_RUN( "pptr_null_resolve", test_pptr_null_resolve );
    PMM_RUN( "pptr_allocate_array", test_pptr_allocate_array );
    PMM_RUN( "pptr_resolve_at", test_pptr_resolve_at );
    PMM_RUN( "pptr_persistence", test_pptr_persistence );
    PMM_RUN( "pptr_comparison", test_pptr_comparison );
    PMM_RUN( "pptr_multiple_types", test_pptr_multiple_types );
    PMM_RUN( "pptr_allocate_auto_expand", test_pptr_allocate_auto_expand );
    PMM_RUN( "pptr_deallocate_null", test_pptr_deallocate_null );
    PMM_RUN( "pptr_tree_node_links", test_pptr_tree_node_links );
    PMM_RUN( "pptr_tree_node_height", test_pptr_tree_node_height );
    PMM_RUN( "pptr_tree_node_weight", test_pptr_tree_node_weight );
    PMM_RUN( "pptr_user_avl_tree", test_pptr_user_avl_tree );
    PMM_RUN( "pptr_tree_node_ref", test_pptr_tree_node_ref );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
