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
    const char* sym_str = pstringview_c_str_unlocked( symbol );
    if ( sym_str == nullptr )
        return nullptr;
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return nullptr;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].symbol_offset == symbol.offset() ||
             std::strncmp( reg->domains[i].name, sym_str, detail::kForestDomainNameCapacity ) == 0 )
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
            const char* cur_str = pstringview_c_str_unlocked( cur );
            return ( cur_str != nullptr ) ? std::strcmp( s, cur_str ) : 0;
        },
        []( pptr<pstringview> p ) -> const char* { return pstringview_c_str_unlocked( p ); } );
    if ( !found.is_null() )
        return found;

    std::uint32_t len        = static_cast<std::uint32_t>( std::strlen( s ) );
    std::size_t   alloc_size = offsetof( pstringview, str ) + static_cast<std::size_t>( len ) + 1;
    void*         raw        = allocate_unlocked( alloc_size );
    if ( raw == nullptr )
        return pptr<pstringview>();

    pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );
    void*             public_raw = raw_user_ptr_from_pptr( new_node );
    if ( public_raw == nullptr )
    {
        deallocate_unlocked( raw );
        return pptr<pstringview>();
    }
    // Use memcpy to avoid UB on potentially misaligned raw pointer (ASan/UBSan fix).
    std::memcpy( public_raw, &len, sizeof( len ) );
    char* str_dst = static_cast<char*>( public_raw ) + offsetof( pstringview, str );
    std::memcpy( str_dst, s, static_cast<std::size_t>( len ) + 1 );

    detail::avl_init_node( new_node );
    if ( !lock_block_permanent_unlocked( public_raw ) )
        return pptr<pstringview>();

    // Re-derive c_str() pointer for comparisons using offset-based access.
    const char* new_str = static_cast<const char*>( public_raw ) + offsetof( pstringview, str );
    detail::avl_insert(
        new_node, symbol_domain->root_offset,
        [&]( pptr<pstringview> cur ) -> bool
        {
            const char* cur_str = pstringview_c_str_unlocked( cur );
            return ( cur_str != nullptr ) && ( std::strcmp( new_str, cur_str ) < 0 );
        },
        []( pptr<pstringview> p ) -> const char* { return pstringview_c_str_unlocked( p ); } );

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

/// @brief Find the mutable block header for a user-data pointer (or nullptr).
static pmm::Block<address_traits>* find_block_from_user_ptr( void* ptr ) noexcept
{
    std::uint8_t*                          base = _backend.base_ptr();
    detail::ManagerHeader<address_traits>* hdr  = get_header( base );
    if constexpr ( sizeof( Block<address_traits> ) % address_traits::granule_size != 0 )
    {
        constexpr std::size_t rounded_header_size =
            static_cast<std::size_t>( kBlockHdrGranules ) * address_traits::granule_size;
        if ( ptr != nullptr && base != nullptr )
        {
            auto* raw = static_cast<std::uint8_t*>( ptr );
            if ( raw >= base + rounded_header_size && raw < base + static_cast<std::size_t>( hdr->total_size ) )
            {
                std::uint8_t* cand = raw - rounded_header_size;
                if ( ( static_cast<std::size_t>( cand - base ) % address_traits::granule_size ) == 0 &&
                     cand + sizeof( Block<address_traits> ) <= base + static_cast<std::size_t>( hdr->total_size ) &&
                     BlockStateBase<address_traits>::get_weight( cand ) != 0 )
                    return reinterpret_cast<pmm::Block<address_traits>*>( cand );
            }
        }
    }
    return detail::header_from_ptr_t<address_traits>( base, ptr, static_cast<std::size_t>( hdr->total_size ) );
}

/// @brief Find the const block header for a user-data pointer.
/// Returns nullptr if ptr is out of range or the block header is invalid.
static const pmm::Block<address_traits>* find_block_from_user_ptr( const void* ptr ) noexcept
{
    const std::uint8_t* base = _backend.base_ptr();
    const auto*         hdr  = get_header_c( base );
    if constexpr ( sizeof( Block<address_traits> ) % address_traits::granule_size != 0 )
    {
        constexpr std::size_t rounded_header_size =
            static_cast<std::size_t>( kBlockHdrGranules ) * address_traits::granule_size;
        if ( ptr != nullptr && base != nullptr )
        {
            const auto* raw = static_cast<const std::uint8_t*>( ptr );
            if ( raw >= base + rounded_header_size && raw < base + static_cast<std::size_t>( hdr->total_size ) )
            {
                const std::uint8_t* cand = raw - rounded_header_size;
                if ( ( static_cast<std::size_t>( cand - base ) % address_traits::granule_size ) == 0 &&
                     cand + sizeof( Block<address_traits> ) <= base + static_cast<std::size_t>( hdr->total_size ) &&
                     BlockStateBase<address_traits>::get_weight( cand ) != 0 )
                    return reinterpret_cast<const pmm::Block<address_traits>*>( cand );
            }
        }
    }
    return detail::header_from_ptr_t<address_traits>( const_cast<std::uint8_t*>( base ), const_cast<void*>( ptr ),
                                                      static_cast<std::size_t>( hdr->total_size ) );
}

// ─── raw ↔ pptr helpers ───────────────────────────────────────

/// @brief Convert a raw user-data pointer returned by allocate() into a canonical public pptr<T>.
/// Caller must ensure raw != nullptr and _initialized before calling.
/// Returns null pptr if the pointer is not within the managed region.
template <typename T> static pptr<T> make_pptr_from_raw( void* raw ) noexcept
{
    if ( raw == nullptr || !_initialized )
        return pptr<T>();
    std::uint8_t* base     = _backend.base_ptr();
    auto*         raw_byte = static_cast<std::uint8_t*>( raw );
    if ( base == nullptr || raw_byte < base || raw_byte >= base + _backend.total_size() )
        return pptr<T>();
    pmm::Block<address_traits>* blk = find_block_from_user_ptr( raw );
    if ( blk == nullptr )
        return pptr<T>();
    index_type blk_idx = detail::block_idx_t<address_traits>( base, blk );
    if ( blk_idx > std::numeric_limits<index_type>::max() - kBlockHdrGranules )
        return pptr<T>();
    return pptr<T>( static_cast<index_type>( blk_idx + kBlockHdrGranules ) );
}

// ─── blk_raw helpers ──────────────────────────────────────────
// base + (offset - kBlockHdrGranules) * granule_size → block header before public user data.

/// @brief Return a const pointer to the block header for the given pptr.
/// Returns nullptr if offset is invalid (would place block header before base).
template <typename T> static const void* block_raw_ptr_from_pptr( pptr<T> p ) noexcept
{
    const std::uint8_t* base = _backend.base_ptr();
    if ( p.offset() < kBlockHdrGranules )
        return nullptr;
    std::size_t blk_off = static_cast<std::size_t>( p.offset() - kBlockHdrGranules ) * address_traits::granule_size;
    if ( blk_off + sizeof( Block<address_traits> ) > _backend.total_size() )
        return nullptr;
    return base + blk_off;
}

/// @brief Return a mutable pointer to the block header for the given pptr.
/// Returns nullptr if offset is invalid (would place block header before base).
template <typename T> static void* block_raw_mut_ptr_from_pptr( pptr<T> p ) noexcept
{
    std::uint8_t* base = _backend.base_ptr();
    if ( p.offset() < kBlockHdrGranules )
        return nullptr;
    std::size_t blk_off = static_cast<std::size_t>( p.offset() - kBlockHdrGranules ) * address_traits::granule_size;
    if ( blk_off + sizeof( Block<address_traits> ) > _backend.total_size() )
        return nullptr;
    return base + blk_off;
}

template <typename T> static constexpr index_type block_idx_from_pptr( pptr<T> p ) noexcept
{
    constexpr index_type kHdrGranules = static_cast<index_type>(
        ( sizeof( Block<address_traits> ) + address_traits::granule_size - 1 ) / address_traits::granule_size );
    return static_cast<index_type>( p.offset() - kHdrGranules );
}

template <typename T> static void* raw_user_ptr_from_pptr( pptr<T> p ) noexcept
{
    if ( p.is_null() || !_initialized )
        return nullptr;

    std::uint8_t* base     = _backend.base_ptr();
    std::size_t   byte_off = static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
    if ( byte_off + sizeof( T ) > _backend.total_size() )
        return nullptr;
    return base + byte_off;
}

template <typename T> static void* raw_block_user_ptr_from_pptr( pptr<T> p ) noexcept
{
    if ( p.is_null() || !_initialized )
        return nullptr;

    std::uint8_t* base = _backend.base_ptr();
    if constexpr ( sizeof( Block<address_traits> ) % address_traits::granule_size == 0 )
    {
        return raw_user_ptr_from_pptr( p );
    }
    else
    {
        constexpr index_type kHdrGranules = static_cast<index_type>(
            ( sizeof( Block<address_traits> ) + address_traits::granule_size - 1 ) / address_traits::granule_size );
        if ( p.offset() < kHdrGranules )
            return nullptr;
        std::size_t blk_off = static_cast<std::size_t>( p.offset() - kHdrGranules ) * address_traits::granule_size;
        if ( blk_off + sizeof( Block<address_traits> ) > _backend.total_size() )
            return nullptr;
        return base + blk_off + sizeof( Block<address_traits> );
    }
}

static const char* pstringview_c_str_unlocked( pptr<pstringview> p ) noexcept
{
    const void* raw = raw_user_ptr_from_pptr( p );
    if ( raw == nullptr )
        return nullptr;
    return static_cast<const char*>( raw ) + offsetof( pstringview, str );
}
