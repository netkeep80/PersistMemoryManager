/**
 * @file pmm/forest_domain_mixin.inc
 * @brief Private forest/domain registry methods for PersistMemoryManager.
 *
 * This file is textually included inside the private section of
 * PersistMemoryManager<ConfigT, InstanceId>. It is NOT a standalone
 * translation unit — all type aliases, static members, and helper
 * functions from the enclosing class are available without qualification.
 *
 * Extracted to keep persist_memory_manager.h under the 1500-line CI limit.
 *
 * @see persist_memory_manager.h
 * @see forest_registry.h
 */

// ─── Forest registry accessor ────────────────────────────────────────────────

static forest_registry* forest_registry_root_unlocked() noexcept
{
    if ( !_initialized || _backend.base_ptr() == nullptr )
        return nullptr;
    detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
    if ( hdr->root_offset == address_traits::no_block ||
         !is_valid_user_offset_unlocked( hdr->root_offset, sizeof( forest_registry ) ) )
        return nullptr;
    auto* reg = reinterpret_cast<forest_registry*>( _backend.base_ptr() + static_cast<std::size_t>( hdr->root_offset ) *
                                                                              address_traits::granule_size );
    if ( reg->magic != detail::kForestRegistryMagic || reg->version != detail::kForestRegistryVersion ||
         reg->domain_count > detail::kMaxForestDomains )
        return nullptr;
    return reg;
}

// ─── Domain lookup helpers ────────────────────────────────────────────────────

static forest_domain* find_domain_by_name_unlocked( const char* name ) noexcept
{
    if ( !detail::forest_domain_name_fits( name ) )
        return nullptr;
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return nullptr;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( detail::forest_domain_name_equals( reg->domains[i], name ) )
            return &reg->domains[i];
    }
    return nullptr;
}

static forest_domain* find_domain_by_binding_unlocked( index_type binding_id ) noexcept
{
    if ( binding_id == 0 )
        return nullptr;
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return nullptr;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].binding_id == binding_id )
            return &reg->domains[i];
    }
    return nullptr;
}

static forest_domain* find_domain_by_symbol_unlocked( pptr<pstringview> symbol ) noexcept
{
    if ( symbol.is_null() )
        return nullptr;
    pstringview* sym = resolve( symbol );
    if ( sym == nullptr )
        return nullptr;
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return nullptr;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].symbol_offset == symbol.offset() ||
             std::strncmp( reg->domains[i].name, sym->c_str(), detail::kForestDomainNameCapacity ) == 0 )
        {
            reg->domains[i].symbol_offset = symbol.offset();
            return &reg->domains[i];
        }
    }
    return nullptr;
}

static index_type domain_root_offset_unlocked( const forest_domain*                         rec,
                                               const detail::ManagerHeader<address_traits>* hdr ) noexcept
{
    if ( rec == nullptr || hdr == nullptr )
        return 0;
    if ( rec->binding_kind == detail::kForestBindingFreeTree )
        return ( hdr->free_tree_root == address_traits::no_block ) ? static_cast<index_type>( 0 ) : hdr->free_tree_root;
    return rec->root_offset;
}

// ─── Legacy root helpers ──────────────────────────────────────────────────────

static index_type get_legacy_root_offset_unlocked() noexcept
{
    forest_registry* reg = forest_registry_root_unlocked();
    return ( reg != nullptr ) ? reg->legacy_root_offset : static_cast<index_type>( 0 );
}

static void set_legacy_root_offset_unlocked( index_type off ) noexcept
{
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg != nullptr )
        reg->legacy_root_offset = off;
}

// ─── Symbol domain helpers ────────────────────────────────────────────────────

static forest_domain* symbol_domain_record_unlocked() noexcept
{
    return find_domain_by_name_unlocked( detail::kSystemDomainSymbols );
}

static index_type symbol_domain_root_offset_unlocked() noexcept
{
    forest_domain* rec = symbol_domain_record_unlocked();
    return ( rec != nullptr ) ? rec->root_offset : static_cast<index_type>( 0 );
}

static void reset_symbol_domain_unlocked() noexcept
{
    forest_domain* rec = symbol_domain_record_unlocked();
    if ( rec != nullptr )
        rec->root_offset = 0;
}

// ─── Domain registration ─────────────────────────────────────────────────────

static bool register_domain_unlocked( const char* name, std::uint8_t flags, std::uint8_t binding_kind,
                                      index_type initial_root ) noexcept
{
    if ( !detail::forest_domain_name_fits( name ) )
        return false;

    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;

    if ( forest_domain* existing = find_domain_by_name_unlocked( name ) )
    {
        existing->flags |= flags;
        existing->binding_kind = binding_kind;
        if ( binding_kind == detail::kForestBindingDirectRoot && initial_root != 0 )
            existing->root_offset = initial_root;
        if ( existing->symbol_offset == 0 )
        {
            pptr<pstringview> symbol = intern_symbol_unlocked( name );
            if ( !symbol.is_null() )
                existing->symbol_offset = symbol.offset();
        }
        return true;
    }

    if ( reg->domain_count >= detail::kMaxForestDomains )
        return false;

    forest_domain rec{};
    if ( !detail::forest_domain_name_copy( rec, name ) )
        return false;

    rec.binding_id = reg->next_binding_id++;
    rec.root_offset =
        ( binding_kind == detail::kForestBindingDirectRoot ) ? initial_root : static_cast<index_type>( 0 );
    rec.binding_kind  = binding_kind;
    rec.flags         = flags;
    rec.symbol_offset = 0;

    pptr<pstringview> symbol = intern_symbol_unlocked( name );
    if ( !symbol.is_null() )
        rec.symbol_offset = symbol.offset();

    reg->domains[reg->domain_count++] = rec;
    return true;
}

// ─── Symbol interning ─────────────────────────────────────────────────────────

static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
{
    if ( s == nullptr )
        s = "";

    forest_domain* symbol_domain = symbol_domain_record_unlocked();
    if ( symbol_domain == nullptr )
        return pptr<pstringview>();

    pptr<pstringview> found = detail::avl_find<pptr<pstringview>>(
        symbol_domain->root_offset,
        [&]( pptr<pstringview> cur ) -> int
        {
            pstringview* obj = resolve( cur );
            return ( obj != nullptr ) ? std::strcmp( s, obj->c_str() ) : 0;
        },
        []( pptr<pstringview> p ) -> pstringview* { return resolve( p ); } );
    if ( !found.is_null() )
        return found;

    std::uint32_t len        = static_cast<std::uint32_t>( std::strlen( s ) );
    std::size_t   alloc_size = offsetof( pstringview, str ) + static_cast<std::size_t>( len ) + 1;
    void*         raw        = allocate_unlocked( alloc_size );
    if ( raw == nullptr )
        return pptr<pstringview>();

    std::uint8_t*     base = _backend.base_ptr();
    pptr<pstringview> new_node( detail::ptr_to_granule_idx<address_traits>( base, raw ) );
    // Use memcpy to avoid UB on potentially misaligned raw pointer (ASan/UBSan fix).
    std::memcpy( raw, &len, sizeof( len ) );
    char* str_dst = static_cast<char*>( raw ) + offsetof( pstringview, str );
    std::memcpy( str_dst, s, static_cast<std::size_t>( len ) + 1 );

    detail::avl_init_node( new_node );
    if ( !lock_block_permanent_unlocked( raw ) )
        return pptr<pstringview>();

    // Re-derive c_str() pointer for comparisons using offset-based access.
    const char* new_str = static_cast<const char*>( raw ) + offsetof( pstringview, str );
    detail::avl_insert(
        new_node, symbol_domain->root_offset,
        [&]( pptr<pstringview> cur ) -> bool
        {
            pstringview* cur_obj = resolve( cur );
            return ( cur_obj != nullptr ) && ( std::strcmp( new_str, cur_obj->c_str() ) < 0 );
        },
        []( pptr<pstringview> p ) -> pstringview* { return resolve( p ); } );

    return new_node;
}

// ─── Bootstrap: system symbols ────────────────────────────────────────────────

static bool bootstrap_system_symbols_unlocked() noexcept
{
    static constexpr const char* kBootstrapSymbols[] = {
        detail::kSystemDomainFreeTree,     detail::kSystemDomainSymbols,          detail::kSystemDomainRegistry,
        detail::kSystemTypeForestRegistry, detail::kSystemTypeForestDomainRecord, detail::kSystemTypePstringview,
        detail::kServiceNameLegacyRoot,    detail::kServiceNameDomainRoot,        detail::kServiceNameDomainSymbol,
    };

    for ( const char* sym : kBootstrapSymbols )
    {
        if ( intern_symbol_unlocked( sym ).is_null() )
            return false;
    }

    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].name[0] == '\0' )
            continue;
        if ( reg->domains[i].symbol_offset != 0 )
            continue;
        pptr<pstringview> symbol = intern_symbol_unlocked( reg->domains[i].name );
        if ( symbol.is_null() )
            return false;
        reg->domains[i].symbol_offset = symbol.offset();
    }

    return true;
}

// ─── Bootstrap: create forest registry root ───────────────────────────────────

static bool create_forest_registry_root_unlocked( index_type legacy_root_offset ) noexcept
{
    static constexpr std::size_t kGranSz = address_traits::granule_size;

    void* raw = allocate_unlocked( sizeof( forest_registry ) + ( kGranSz - 1 ) );
    if ( raw == nullptr )
    {
        if ( _last_error == PmmError::Ok )
            _last_error = PmmError::OutOfMemory;
        return false;
    }

    std::uint8_t*    base        = _backend.base_ptr();
    std::size_t      raw_off     = static_cast<std::size_t>( static_cast<std::uint8_t*>( raw ) - base );
    std::size_t      aligned_off = ( raw_off + ( kGranSz - 1 ) ) & ~( kGranSz - 1 );
    forest_registry* reg         = reinterpret_cast<forest_registry*>( base + aligned_off );
    if ( reg == nullptr )
    {
        _last_error = PmmError::InvalidPointer;
        return false;
    }

    std::memset( reg, 0, sizeof( forest_registry ) );
    reg->magic              = detail::kForestRegistryMagic;
    reg->version            = detail::kForestRegistryVersion;
    reg->domain_count       = 0;
    reg->legacy_root_offset = legacy_root_offset;
    reg->next_binding_id    = 1;

    if ( !lock_block_permanent_unlocked( raw ) )
    {
        _last_error = PmmError::InvalidPointer;
        return false;
    }

    get_header( _backend.base_ptr() )->root_offset =
        detail::ptr_to_granule_idx<address_traits>( _backend.base_ptr(), reg );

    if ( !register_domain_unlocked( detail::kSystemDomainFreeTree, detail::kForestDomainFlagSystem,
                                    detail::kForestBindingFreeTree, 0 ) )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    if ( !register_domain_unlocked( detail::kSystemDomainSymbols, detail::kForestDomainFlagSystem,
                                    detail::kForestBindingDirectRoot, 0 ) )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                    detail::kForestBindingDirectRoot, get_header( _backend.base_ptr() )->root_offset ) )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    if ( !bootstrap_system_symbols_unlocked() )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    return true;
}

static bool bootstrap_forest_registry_unlocked() noexcept
{
    return create_forest_registry_root_unlocked( 0 );
}

// ─── Bootstrap: invariant verification ───────────────────────────

/// @brief Verify that all bootstrap invariants hold.
static bool validate_bootstrap_invariants_unlocked() noexcept
{
    if ( !_initialized )
        return false;
    std::uint8_t* base = _backend.base_ptr();
    if ( base == nullptr )
        return false;
    const auto* hdr = get_header_c( base );
    // 1. Manager header valid
    if ( hdr->magic != kMagic )
        return false;
    if ( hdr->total_size != _backend.total_size() )
        return false;
    if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
        return false;
    // 2. Forest registry exists and is valid
    const forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;
    if ( reg->magic != detail::kForestRegistryMagic )
        return false;
    if ( reg->version != detail::kForestRegistryVersion )
        return false;
    if ( reg->domain_count < 3 )
        return false; // at least free_tree, symbols, registry
    // 3. System domains present with correct flags
    static constexpr const char* kRequired[] = { detail::kSystemDomainFreeTree, detail::kSystemDomainSymbols,
                                                 detail::kSystemDomainRegistry };
    for ( const char* name : kRequired )
    {
        const forest_domain* rec = find_domain_by_name_unlocked( name );
        if ( rec == nullptr )
            return false;
        if ( ( rec->flags & detail::kForestDomainFlagSystem ) == 0 )
            return false;
        if ( rec->symbol_offset == 0 )
            return false; // symbol must be interned
    }
    // 4. Free tree domain has binding kind kForestBindingFreeTree
    const forest_domain* free_rec = find_domain_by_name_unlocked( detail::kSystemDomainFreeTree );
    if ( free_rec->binding_kind != detail::kForestBindingFreeTree )
        return false;
    // 5. Symbol dictionary root is non-zero (at least bootstrap symbols exist)
    if ( symbol_domain_root_offset_unlocked() == 0 )
        return false;
    // 6. Registry domain root matches header root_offset
    const forest_domain* reg_rec = find_domain_by_name_unlocked( detail::kSystemDomainRegistry );
    if ( reg_rec->root_offset != hdr->root_offset )
        return false;
    return true;
}

// ─── Bootstrap: validate or create forest registry on load ────────────────────

static bool validate_or_bootstrap_forest_registry_unlocked() noexcept
{
    detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
    if ( forest_registry_root_unlocked() != nullptr )
    {
        if ( !register_domain_unlocked( detail::kSystemDomainFreeTree, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingFreeTree, 0 ) )
            return false;
        if ( !register_domain_unlocked( detail::kSystemDomainSymbols, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingDirectRoot, symbol_domain_root_offset_unlocked() ) )
            return false;
        if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingDirectRoot, hdr->root_offset ) )
            return false;

        if ( forest_domain* free_rec = find_domain_by_name_unlocked( detail::kSystemDomainFreeTree ) )
        {
            free_rec->flags |= detail::kForestDomainFlagSystem;
            free_rec->binding_kind = detail::kForestBindingFreeTree;
            free_rec->root_offset  = 0;
        }
        if ( forest_domain* symbols_rec = find_domain_by_name_unlocked( detail::kSystemDomainSymbols ) )
        {
            symbols_rec->flags |= detail::kForestDomainFlagSystem;
            symbols_rec->binding_kind = detail::kForestBindingDirectRoot;
        }
        if ( forest_domain* registry_rec = find_domain_by_name_unlocked( detail::kSystemDomainRegistry ) )
        {
            registry_rec->flags |= detail::kForestDomainFlagSystem;
            registry_rec->binding_kind = detail::kForestBindingDirectRoot;
            registry_rec->root_offset  = hdr->root_offset;
        }
        return bootstrap_system_symbols_unlocked();
    }

    index_type legacy_root = 0;
    if ( hdr->root_offset != address_traits::no_block &&
         is_valid_user_offset_unlocked( hdr->root_offset, sizeof( std::uint32_t ) ) )
    {
        if ( !is_valid_user_offset_unlocked( hdr->root_offset, sizeof( forest_registry ) ) )
        {
            legacy_root = hdr->root_offset;
        }
        else
        {
            auto* candidate = reinterpret_cast<const forest_registry*>(
                _backend.base_ptr() + static_cast<std::size_t>( hdr->root_offset ) * address_traits::granule_size );
            if ( candidate->magic != detail::kForestRegistryMagic )
                legacy_root = hdr->root_offset;
        }
    }

    hdr->root_offset = address_traits::no_block;
    return create_forest_registry_root_unlocked( legacy_root );
}

// ─── Free block tree traversal ────────────────────────────────────────────────

/// @brief Recursive in-order traversal of the AVL free block tree.
template <typename Callback>
static void for_each_free_block_inorder( const std::uint8_t* base, const detail::ManagerHeader<address_traits>* hdr,
                                         index_type node_idx, int depth, Callback&& callback ) noexcept
{
    using BlockState = BlockStateBase<address_traits>;
    // Use address_traits::granule_size for correct byte offset computations.
    static constexpr std::size_t kGranSz = address_traits::granule_size;
    if ( node_idx == address_traits::no_block )
        return;
    if ( static_cast<std::size_t>( node_idx ) * kGranSz + sizeof( Block<address_traits> ) > hdr->total_size )
        return;
    const void*                  blk_raw = base + static_cast<std::size_t>( node_idx ) * kGranSz;
    const Block<address_traits>* blk     = reinterpret_cast<const Block<address_traits>*>( blk_raw );

    index_type left_off   = BlockState::get_left_offset( blk_raw );
    index_type right_off  = BlockState::get_right_offset( blk_raw );
    index_type parent_off = BlockState::get_parent_offset( blk_raw );

    // Visit left subtree first (smaller blocks)
    for_each_free_block_inorder( base, hdr, left_off, depth + 1, callback );

    // Visit current node
    index_type    total_gran = detail::block_total_granules( base, hdr, blk );
    FreeBlockView view;
    view.offset        = static_cast<std::ptrdiff_t>( static_cast<std::size_t>( node_idx ) * kGranSz );
    view.total_size    = static_cast<std::size_t>( total_gran ) * kGranSz;
    view.free_size     = static_cast<std::size_t>( total_gran - kBlockHdrGranules ) * kGranSz;
    view.left_offset   = ( left_off != address_traits::no_block )
                             ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( left_off ) * kGranSz )
                             : -1;
    view.right_offset  = ( right_off != address_traits::no_block )
                             ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( right_off ) * kGranSz )
                             : -1;
    view.parent_offset = ( parent_off != address_traits::no_block )
                             ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( parent_off ) * kGranSz )
                             : -1;
    view.avl_height    = BlockState::get_avl_height( blk_raw );
    view.avl_depth     = depth;
    callback( view );

    // Visit right subtree (larger blocks)
    for_each_free_block_inorder( base, hdr, right_off, depth + 1, callback );
}
