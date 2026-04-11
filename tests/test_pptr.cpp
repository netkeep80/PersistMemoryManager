/**
 * @file test_pptr.cpp
 * @brief Тесты персистентного типизированного указателя pptr<T, ManagerT>
 *
 *   - pptr<T, ManagerT> без ManagerT=void по умолчанию.
 *   - Разыменование через *p и p->field, не через p.resolve().
 *   - Нет operator*, operator->, get_at(), operator[].
 *   - Нет reallocate_typed() в новом API.
 *   используется tree_node() API для работы с узлами AVL-дерева.
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

using Mgr = pmm::presets::SingleThreadedHeap;

TEST_CASE( "pptr_sizeof", "[test_pptr]" )
{
    // Pptr<T, ManagerT> is 4 bytes (uint32_t granule index)
    REQUIRE( sizeof( Mgr::pptr<int> ) == 4 );
    REQUIRE( sizeof( Mgr::pptr<double> ) == 4 );
    REQUIRE( sizeof( Mgr::pptr<char> ) == 4 );
    REQUIRE( sizeof( Mgr::pptr<std::uint64_t> ) == 4 );
}

TEST_CASE( "pptr_default_null", "[test_pptr]" )
{
    Mgr::pptr<int> p;
    REQUIRE( p.is_null() );
    REQUIRE( !p );
    REQUIRE( p.offset() == 0 );
}

TEST_CASE( "pptr_allocate_typed_int", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( static_cast<bool>( p ) );
    REQUIRE( p.offset() > 0 );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    pmm.destroy();
}

TEST_CASE( "pptr_resolve", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // Разыменование через *p и p->field
    *p = 42;
    REQUIRE( *p == 42 );

    pmm.deallocate_typed( p );
    pmm.destroy();
}

TEST_CASE( "pptr_write_read", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // Write/read via operator*
    *p = 42;
    REQUIRE( *p == 42 );

    *p = 100;
    REQUIRE( *p == 100 );

    pmm.deallocate_typed( p );
    pmm.destroy();
}

TEST_CASE( "pptr_deallocate", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    std::size_t free_before = pmm.free_size();

    Mgr::pptr<double> p = pmm.allocate_typed<double>();
    REQUIRE( !p.is_null() );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    REQUIRE( pmm.is_initialized() );

    REQUIRE( pmm.free_size() >= free_before );

    pmm.destroy();
}

TEST_CASE( "pptr_null_resolve", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p; // null by default
    REQUIRE( !p );    // null pptr evaluates to false

    pmm.destroy();
}

TEST_CASE( "pptr_allocate_array", "[test_pptr]" )
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 10;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>( count );
    REQUIRE( !p.is_null() );
    REQUIRE( pmm.is_initialized() );

    int* arr = pmm.resolve_at( p, 0 );
    REQUIRE( arr != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<int>( i * 10 );

    for ( std::size_t i = 0; i < count; i++ )
        REQUIRE( pmm.resolve_at( p, i )[0] == static_cast<int>( i * 10 ) );

    pmm.deallocate_typed( p );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

TEST_CASE( "pptr_resolve_at", "[test_pptr]" )
{
    const std::size_t size  = 256 * 1024;
    const std::size_t count = 5;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<double> p = pmm.allocate_typed<double>( count );
    REQUIRE( !p.is_null() );

    double* arr = pmm.resolve_at( p, 0 );
    REQUIRE( arr != nullptr );
    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<double>( i ) * 1.5;

    for ( std::size_t i = 0; i < count; i++ )
        REQUIRE( pmm.resolve_at( p, i )[0] == static_cast<double>( i ) * 1.5 );

    pmm.deallocate_typed( p );
    pmm.destroy();
}

TEST_CASE( "pptr_persistence", "[test_pptr]" )
{
    const std::size_t size     = 64 * 1024;
    const char*       filename = "pptr_test.dat";

    // Use distinct InstanceId values to simulate two separate manager "sessions"
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 200>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 201>;

    REQUIRE( Mgr1::create( size ) );

    Mgr1::pptr<int> p1 = Mgr1::allocate_typed<int>();
    REQUIRE( !p1.is_null() );
    *p1 = 12345;

    std::uint32_t saved_offset = p1.offset();
    REQUIRE( pmm::save_manager<Mgr1>( filename ) );
    Mgr1::destroy();

    REQUIRE( Mgr2::create( size ) );
    REQUIRE( pmm::load_manager_from_file<Mgr2>( filename, pmm::VerifyResult{} ) );
    REQUIRE( Mgr2::is_initialized() );

    // Restore pptr by saved offset
    Mgr2::pptr<int> p2( saved_offset );
    REQUIRE( !p2.is_null() );
    REQUIRE( *p2 == 12345 );

    Mgr2::deallocate_typed( p2 );
    Mgr2::destroy();
    std::remove( filename );
}

TEST_CASE( "pptr_comparison", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> p2 = pmm.allocate_typed<int>();
    Mgr::pptr<int> p3 = p1;

    REQUIRE( p1 == p3 );
    REQUIRE( p1 != p2 );
    REQUIRE( !( p1 == p2 ) );

    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    pmm.destroy();
}

TEST_CASE( "pptr_multiple_types", "[test_pptr]" )
{
    const std::size_t size = 256 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int>    pi = pmm.allocate_typed<int>();
    Mgr::pptr<double> pd = pmm.allocate_typed<double>();
    Mgr::pptr<char>   pc = pmm.allocate_typed<char>( 16 );

    REQUIRE( !pi.is_null() );
    REQUIRE( !pd.is_null() );
    REQUIRE( !pc.is_null() );
    REQUIRE( pmm.is_initialized() );

    *pi = 7;
    *pd = 3.14;
    std::memcpy( pmm.resolve_at( pc, 0 ), "hello", 6 );

    REQUIRE( *pi == 7 );
    REQUIRE( *pd == 3.14 );
    REQUIRE( std::memcmp( pmm.resolve_at( pc, 0 ), "hello", 6 ) == 0 );

    pmm.deallocate_typed( pi );
    pmm.deallocate_typed( pd );
    pmm.deallocate_typed( pc );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/**
 * @brief При нехватке памяти менеджер (HeapStorage) автоматически расширяется.
 *
 * Uses unique InstanceId (202) to start with a fresh backend of exactly 8K.
 */
TEST_CASE( "pptr_allocate_auto_expand", "[test_pptr]" )
{
    using MgrExpand = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 202>;

    const std::size_t initial_size = 8 * 1024;

    REQUIRE( MgrExpand::create( initial_size ) );

    std::size_t initial_total = MgrExpand::total_size();

    // Fill most of initial buffer
    MgrExpand::pptr<std::uint8_t> p1 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    REQUIRE( !p1.is_null() );

    // Request second large block — should trigger expansion
    MgrExpand::pptr<std::uint8_t> p2 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    REQUIRE( !p2.is_null() );

    REQUIRE( MgrExpand::is_initialized() );
    REQUIRE( MgrExpand::total_size() > initial_total );

    MgrExpand::destroy();
}

TEST_CASE( "pptr_deallocate_null", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p;
    pmm.deallocate_typed( p ); // deallocating null should be safe
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/**
 * @brief Тест методов работы с узлом AVL-дерева через pptr::tree_node().
 *
 * Проверяет начальное состояние (нет связей), установку и чтение
 * левого/правого/родительского потомков дерева через tree_node() API.
 */
TEST_CASE( "pptr_tree_node_links", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> node1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> node2 = pmm.allocate_typed<int>();
    Mgr::pptr<int> node3 = pmm.allocate_typed<int>();

    REQUIRE( !node1.is_null() );
    REQUIRE( !node2.is_null() );
    REQUIRE( !node3.is_null() );

    const auto no_block = pmm::DefaultAddressTraits::no_block;

    // Начальное состояние: нет связей (no_block в полях tree_node)
    REQUIRE( node1.tree_node().get_left() == no_block );
    REQUIRE( node1.tree_node().get_right() == no_block );
    REQUIRE( node1.tree_node().get_parent() == no_block );
    REQUIRE( node2.tree_node().get_left() == no_block );
    REQUIRE( node2.tree_node().get_right() == no_block );
    REQUIRE( node2.tree_node().get_parent() == no_block );

    // Построить дерево: node1 — корень, node2 — левый, node3 — правый
    node1.tree_node().set_left( node2.offset() );
    node1.tree_node().set_right( node3.offset() );
    node2.tree_node().set_parent( node1.offset() );
    node3.tree_node().set_parent( node1.offset() );

    // Проверить связи
    REQUIRE( node1.tree_node().get_left() == node2.offset() );
    REQUIRE( node1.tree_node().get_right() == node3.offset() );
    REQUIRE( node2.tree_node().get_parent() == node1.offset() );
    REQUIRE( node3.tree_node().get_parent() == node1.offset() );

    // Потомки node2 и node3 по-прежнему null (no_block)
    REQUIRE( node2.tree_node().get_left() == no_block );
    REQUIRE( node2.tree_node().get_right() == no_block );
    REQUIRE( node3.tree_node().get_left() == no_block );
    REQUIRE( node3.tree_node().get_right() == no_block );

    pmm.deallocate_typed( node1 );
    pmm.deallocate_typed( node2 );
    pmm.deallocate_typed( node3 );
    pmm.destroy();
}

/**
 * @brief Тест высоты AVL-узла через pptr::tree_node().
 */
TEST_CASE( "pptr_tree_node_height", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> node1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> node2 = pmm.allocate_typed<int>();

    REQUIRE( !node1.is_null() );
    REQUIRE( !node2.is_null() );

    // Начальная высота = 0 (узел не в дереве после выделения)
    REQUIRE( node1.tree_node().get_height() == 0 );
    REQUIRE( node2.tree_node().get_height() == 0 );

    // Установить высоту
    node1.tree_node().set_height( 2 );
    node2.tree_node().set_height( 1 );

    REQUIRE( node1.tree_node().get_height() == 2 );
    REQUIRE( node2.tree_node().get_height() == 1 );

    pmm.deallocate_typed( node1 );
    pmm.deallocate_typed( node2 );
    pmm.destroy();
}

/**
 * @brief Тест методов дерева — получение веса блока.
 */
TEST_CASE( "pptr_tree_node_weight", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // Для выделенного блока вес > 0 (размер данных в гранулах)
    REQUIRE( p.tree_node().get_weight() > 0 );

    pmm.deallocate_typed( p );
    pmm.destroy();
}

/**
 * @brief Тест использования pptr для пользовательского AVL-дерева.
 *
 * Строит простое бинарное дерево поиска из 5 узлов через tree_node() API,
 * проверяет корректность связей и высот.
 */
TEST_CASE( "pptr_user_avl_tree", "[test_pptr]" )
{
    using LocalMgr         = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 203>;
    const std::size_t size = 128 * 1024;

    REQUIRE( LocalMgr::create( size ) );

    // Выделить 5 узлов для дерева
    LocalMgr::pptr<int> n1 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n2 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n3 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n4 = LocalMgr::allocate_typed<int>();
    LocalMgr::pptr<int> n5 = LocalMgr::allocate_typed<int>();

    REQUIRE( !n1.is_null() );
    REQUIRE( !n2.is_null() );
    REQUIRE( !n3.is_null() );
    REQUIRE( !n4.is_null() );
    REQUIRE( !n5.is_null() );

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
    REQUIRE( n1.tree_node().get_left() == n2.offset() );
    REQUIRE( n1.tree_node().get_right() == n3.offset() );
    REQUIRE( n1.tree_node().get_parent() == no_block );
    REQUIRE( n1.tree_node().get_height() == 3 );

    REQUIRE( n2.tree_node().get_left() == n4.offset() );
    REQUIRE( n2.tree_node().get_right() == n5.offset() );
    REQUIRE( n2.tree_node().get_parent() == n1.offset() );
    REQUIRE( n2.tree_node().get_height() == 2 );

    REQUIRE( n3.tree_node().get_left() == no_block );
    REQUIRE( n3.tree_node().get_right() == no_block );
    REQUIRE( n3.tree_node().get_parent() == n1.offset() );
    REQUIRE( n3.tree_node().get_height() == 1 );

    // Проверить данные в узлах (не затронуты операциями дерева)
    REQUIRE( *n1 == 30 );
    REQUIRE( *n2 == 20 );
    REQUIRE( *n3 == 40 );
    REQUIRE( *n4 == 10 );
    REQUIRE( *n5 == 25 );

    LocalMgr::deallocate_typed( n1 );
    LocalMgr::deallocate_typed( n2 );
    LocalMgr::deallocate_typed( n3 );
    LocalMgr::deallocate_typed( n4 );
    LocalMgr::deallocate_typed( n5 );
    LocalMgr::destroy();
}

/**
 * @brief Тест pptr::tree_node() — прямой доступ к TreeNode через ссылку.
 *
 * Проверяет, что pptr::tree_node() возвращает ссылку на TreeNode в заголовке блока,
 * и что TreeNode-методы (get_left, set_left, get_right, set_right, get_parent,
 * set_parent, get_height, set_height) работают корректно.
 */
TEST_CASE( "pptr_tree_node_ref", "[test_pptr]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<int> n1 = pmm.allocate_typed<int>();
    Mgr::pptr<int> n2 = pmm.allocate_typed<int>();
    Mgr::pptr<int> n3 = pmm.allocate_typed<int>();

    REQUIRE( !n1.is_null() );
    REQUIRE( !n2.is_null() );
    REQUIRE( !n3.is_null() );

    // Доступ к TreeNode через pptr::tree_node()
    auto& tn1 = n1.tree_node();
    auto& tn2 = n2.tree_node();
    auto& tn3 = n3.tree_node();

    // Начальное состояние: связи через tree_node() — no_block
    const auto no_block = pmm::DefaultAddressTraits::no_block;
    REQUIRE( tn1.get_left() == no_block );
    REQUIRE( tn1.get_right() == no_block );
    REQUIRE( tn1.get_parent() == no_block );
    REQUIRE( tn2.get_left() == no_block );
    REQUIRE( tn2.get_right() == no_block );

    // Построить дерево через tree_node(): n1 — корень, n2 — левый, n3 — правый
    tn1.set_left( n2.offset() );
    tn1.set_right( n3.offset() );
    tn2.set_parent( n1.offset() );
    tn3.set_parent( n1.offset() );
    tn1.set_height( 2 );
    tn2.set_height( 1 );
    tn3.set_height( 1 );

    // Проверить через tree_node()
    REQUIRE( tn1.get_left() == n2.offset() );
    REQUIRE( tn1.get_right() == n3.offset() );
    REQUIRE( tn2.get_parent() == n1.offset() );
    REQUIRE( tn3.get_parent() == n1.offset() );
    REQUIRE( tn1.get_height() == 2 );
    REQUIRE( tn2.get_height() == 1 );
    REQUIRE( tn3.get_height() == 1 );

    pmm.deallocate_typed( n1 );
    pmm.deallocate_typed( n2 );
    pmm.deallocate_typed( n3 );
    pmm.destroy();
}
