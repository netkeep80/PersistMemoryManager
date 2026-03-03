/**
 * @file persist_avl_tree.h
 * @brief AVL-дерево свободных блоков для PersistMemoryManager (Issue #73)
 *
 * Все-статический класс PersistentAvlTree для управления деревом свободных блоков.
 * Принимает base_ptr и header* как явные параметры контекста —
 * не имеет неявной зависимости от синглтона PersistMemoryManager (Issue #73 AR-03).
 *
 * Ключ сортировки: (total_granules, block_index) — строгий порядок.
 * Поиск best-fit выполняется за O(log n).
 *
 * @version 1.0 (Issue #73 refactoring)
 */

#pragma once

#include "persist_memory_types.h"

#include <cassert>
#include <cstdint>
#include <limits>

namespace pmm
{

/// @brief Все-статический класс AVL-дерева свободных блоков (Issue #73 FR-02, AR-03).
/// Не зависит от PersistMemoryManager — принимает base_ptr и header как контекст.
class PersistentAvlTree
{
  public:
    PersistentAvlTree()                                      = delete;
    PersistentAvlTree( const PersistentAvlTree& )            = delete;
    PersistentAvlTree& operator=( const PersistentAvlTree& ) = delete;

    /// @brief Вставить блок в AVL-дерево свободных блоков.
    static void insert( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        detail::BlockHeader* blk = detail::block_at( base, blk_idx );
        blk->left_offset         = detail::kNoBlock;
        blk->right_offset        = detail::kNoBlock;
        blk->parent_offset       = detail::kNoBlock;
        blk->avl_height          = 1;
        if ( hdr->free_tree_root == detail::kNoBlock )
        {
            hdr->free_tree_root = blk_idx;
            return;
        }
        // Issue #59: cache total_gran once; compute blk size in granules before the traversal loop
        std::uint32_t total_gran = detail::byte_off_to_idx( hdr->total_size );
        std::uint32_t blk_gran =
            ( blk->next_offset != detail::kNoBlock ) ? ( blk->next_offset - blk_idx ) : ( total_gran - blk_idx );
        std::uint32_t cur = hdr->free_tree_root, parent = detail::kNoBlock;
        bool          go_left = false;
        while ( cur != detail::kNoBlock )
        {
            parent                 = cur;
            detail::BlockHeader* n = detail::block_at( base, cur );
            std::uint32_t        n_gran =
                ( n->next_offset != detail::kNoBlock ) ? ( n->next_offset - cur ) : ( total_gran - cur );
            bool smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left      = smaller;
            cur          = smaller ? n->left_offset : n->right_offset;
        }
        blk->parent_offset = parent;
        if ( go_left )
            detail::block_at( base, parent )->left_offset = blk_idx;
        else
            detail::block_at( base, parent )->right_offset = blk_idx;
        rebalance_up( base, hdr, parent );
    }

    /// @brief Удалить блок из AVL-дерева свободных блоков.
    static void remove( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        detail::BlockHeader* blk    = detail::block_at( base, blk_idx );
        std::uint32_t        parent = blk->parent_offset;
        std::uint32_t        left   = blk->left_offset;
        std::uint32_t        right  = blk->right_offset;
        std::uint32_t        rebal  = detail::kNoBlock;

        if ( left == detail::kNoBlock && right == detail::kNoBlock )
        {
            set_child( base, hdr, parent, blk_idx, detail::kNoBlock );
            rebal = parent;
        }
        else if ( left == detail::kNoBlock || right == detail::kNoBlock )
        {
            std::uint32_t child                            = ( left != detail::kNoBlock ) ? left : right;
            detail::block_at( base, child )->parent_offset = parent;
            set_child( base, hdr, parent, blk_idx, child );
            rebal = parent;
        }
        else
        {
            std::uint32_t        succ_idx    = min_node( base, right );
            detail::BlockHeader* succ        = detail::block_at( base, succ_idx );
            std::uint32_t        succ_parent = succ->parent_offset;
            std::uint32_t        succ_right  = succ->right_offset;

            if ( succ_parent != blk_idx )
            {
                set_child( base, hdr, succ_parent, succ_idx, succ_right );
                if ( succ_right != detail::kNoBlock )
                    detail::block_at( base, succ_right )->parent_offset = succ_parent;
                succ->right_offset                             = right;
                detail::block_at( base, right )->parent_offset = succ_idx;
                rebal                                          = succ_parent;
            }
            else
            {
                rebal = succ_idx;
            }
            succ->left_offset                             = left;
            detail::block_at( base, left )->parent_offset = succ_idx;
            succ->parent_offset                           = parent;
            set_child( base, hdr, parent, blk_idx, succ_idx );
            update_height( base, succ );
        }
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 0;
        rebalance_up( base, hdr, rebal );
    }

    /// @brief Найти наименьший блок >= needed гранул (best-fit, O(log n)).
    static std::uint32_t find_best_fit( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t needed_granules )
    {
        // Issue #59: cache total_gran once to avoid repeated hdr->total_size reads in the hot path
        std::uint32_t total_gran = detail::byte_off_to_idx( hdr->total_size );
        std::uint32_t cur = hdr->free_tree_root, result = detail::kNoBlock;
        while ( cur != detail::kNoBlock )
        {
            detail::BlockHeader* node = detail::block_at( base, cur );
            std::uint32_t        cur_gran =
                ( node->next_offset != detail::kNoBlock ) ? ( node->next_offset - cur ) : ( total_gran - cur );
            if ( cur_gran >= needed_granules )
            {
                result = cur;
                cur    = node->left_offset;
            }
            else
            {
                cur = node->right_offset;
            }
        }
        return result;
    }

  private:
    static std::int32_t height( std::uint8_t* base, std::uint32_t idx )
    {
        return ( idx == detail::kNoBlock ) ? 0 : detail::block_at( base, idx )->avl_height;
    }

    static void update_height( std::uint8_t* base, detail::BlockHeader* node )
    {
        std::int32_t h = 1 + std::max( height( base, node->left_offset ), height( base, node->right_offset ) );
        assert( h <= std::numeric_limits<std::int16_t>::max() ); // tree height must fit in int16_t
        node->avl_height = static_cast<std::int16_t>( h );
    }

    static std::int32_t balance_factor( std::uint8_t* base, detail::BlockHeader* node )
    {
        return height( base, node->left_offset ) - height( base, node->right_offset );
    }

    /// @brief Обновить ссылку parent → child в дереве.
    static void set_child( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t parent,
                           std::uint32_t old_child, std::uint32_t new_child )
    {
        if ( parent == detail::kNoBlock )
        {
            hdr->free_tree_root = new_child;
            return;
        }
        detail::BlockHeader* p = detail::block_at( base, parent );
        if ( p->left_offset == old_child )
            p->left_offset = new_child;
        else
            p->right_offset = new_child;
    }

    static std::uint32_t rotate_right( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t y_idx )
    {
        detail::BlockHeader* y     = detail::block_at( base, y_idx );
        std::uint32_t        x_idx = y->left_offset;
        detail::BlockHeader* x     = detail::block_at( base, x_idx );
        std::uint32_t        t2    = x->right_offset;

        x->right_offset  = y_idx;
        y->left_offset   = t2;
        x->parent_offset = y->parent_offset;
        y->parent_offset = x_idx;
        if ( t2 != detail::kNoBlock )
            detail::block_at( base, t2 )->parent_offset = y_idx;
        set_child( base, hdr, x->parent_offset, y_idx, x_idx );
        update_height( base, y );
        update_height( base, x );
        return x_idx;
    }

    static std::uint32_t rotate_left( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t x_idx )
    {
        detail::BlockHeader* x     = detail::block_at( base, x_idx );
        std::uint32_t        y_idx = x->right_offset;
        detail::BlockHeader* y     = detail::block_at( base, y_idx );
        std::uint32_t        t2    = y->left_offset;

        y->left_offset   = x_idx;
        x->right_offset  = t2;
        y->parent_offset = x->parent_offset;
        x->parent_offset = y_idx;
        if ( t2 != detail::kNoBlock )
            detail::block_at( base, t2 )->parent_offset = x_idx;
        set_child( base, hdr, y->parent_offset, x_idx, y_idx );
        update_height( base, x );
        update_height( base, y );
        return y_idx;
    }

    static void rebalance_up( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t node_idx )
    {
        std::uint32_t cur = node_idx;
        while ( cur != detail::kNoBlock )
        {
            detail::BlockHeader* node = detail::block_at( base, cur );
            update_height( base, node );
            std::int32_t bf = balance_factor( base, node );
            if ( bf > 1 )
            {
                if ( balance_factor( base, detail::block_at( base, node->left_offset ) ) < 0 )
                    rotate_left( base, hdr, node->left_offset );
                cur = rotate_right( base, hdr, cur );
            }
            else if ( bf < -1 )
            {
                if ( balance_factor( base, detail::block_at( base, node->right_offset ) ) > 0 )
                    rotate_right( base, hdr, node->right_offset );
                cur = rotate_left( base, hdr, cur );
            }
            cur = detail::block_at( base, cur )->parent_offset;
        }
    }

    static std::uint32_t min_node( std::uint8_t* base, std::uint32_t node_idx )
    {
        while ( node_idx != detail::kNoBlock )
        {
            std::uint32_t left = detail::block_at( base, node_idx )->left_offset;
            if ( left == detail::kNoBlock )
                break;
            node_idx = left;
        }
        return node_idx;
    }
};

} // namespace pmm
