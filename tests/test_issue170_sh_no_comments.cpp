/**
 * @file test_issue170_sh_no_comments.cpp
 * @brief Self-sufficiency test for pmm_no_comments.h.
 *
 * Verifies that the comment-stripped single-header file pmm_no_comments.h is
 * fully self-contained and functionally equivalent to the full pmm.h:
 * the user can copy one file into their project and use PMM without any other
 * dependencies.
 *
 * This file intentionally does not use any other includes from include/pmm/.
 */

#include "pmm_no_comments.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE( "test_issue170_sh_no_comments", "[test_issue170_sh_no_comments]" )
{
    using MyHeap = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;

    REQUIRE( !MyHeap::is_initialized() );
    bool created = MyHeap::create( 32 * 1024 );
    REQUIRE( created );
    REQUIRE( MyHeap::is_initialized() );
    REQUIRE( MyHeap::total_size() >= 32 * 1024 );

    void* ptr = MyHeap::allocate( 256 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xBB, 256 );
    MyHeap::deallocate( ptr );

    MyHeap::pptr<int> p = MyHeap::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 99;
    REQUIRE( *p.resolve() == 99 );
    MyHeap::deallocate_typed( p );

    MyHeap::destroy();
    REQUIRE( !MyHeap::is_initialized() );
}
