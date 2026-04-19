// ─── Verify / Repair mixin ──────────────────────────────────────
// Included inside PersistMemoryManager<ConfigT, InstanceId> private section.
// Provides verify_image_unlocked() — read-only structural diagnostics.

/**
 * @brief Verify structural integrity of the loaded image without modifications.
 *
 * Performs read-only checks on:
 *   1. Block state consistency (weight/root_offset mismatches)
 *   2. Linked list prev_offset correctness
 *   3. Counter consistency (block_count, free_count, alloc_count, used_size)
 *   4. Free tree root consistency (free_tree_root vs actual free block count)
 *   5. Forest registry and system domain presence/flags
 *
 * @param result  VerifyResult to populate with diagnostic entries.
 */
static void verify_image_unlocked( VerifyResult& result ) noexcept
{
    result.mode = RecoveryMode::Verify;
    result.ok   = true;

    const std::uint8_t*                          base = _backend.base_ptr();
    const detail::ManagerHeader<address_traits>* hdr  = get_header_c( base );

    // 1. Header validation
    if ( hdr->magic != kMagic )
    {
        result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, static_cast<std::uint64_t>( kMagic ),
                    static_cast<std::uint64_t>( hdr->magic ) );
        return; // Cannot proceed
    }
    if ( hdr->total_size != _backend.total_size() )
    {
        result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, _backend.total_size(),
                    static_cast<std::uint64_t>( hdr->total_size ) );
        return; // Cannot proceed — addressing is unreliable
    }
    if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
    {
        result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, address_traits::granule_size,
                    static_cast<std::uint64_t>( hdr->granule_size ) );
        return; // Cannot proceed — block sizes are meaningless
    }

    // 2. Full block header validation (next/prev bounds, node_type, data range)
    {
        using BlockState = BlockStateBase<address_traits>;
        index_type idx   = hdr->first_block_offset;
        while ( idx != address_traits::no_block )
        {
            if ( !detail::validate_block_index<address_traits>( hdr->total_size, idx ) )
                break;
            detail::validate_block_header_full<address_traits>( base, hdr->total_size, idx, result );
            const void* blk_ptr = base + static_cast<std::size_t>( idx ) * address_traits::granule_size;
            idx                  = BlockState::get_next_offset( blk_ptr );
        }
    }

    // 3. Block state consistency (transitional states)
    allocator::verify_block_states( base, hdr, result );

    // 4. Linked list prev_offset
    allocator::verify_linked_list( base, hdr, result );

    // 5. Counter consistency
    allocator::verify_counters( base, hdr, result );

    // 6. Free tree root consistency
    allocator::verify_free_tree( base, hdr, result );

    // 7. Forest registry validation
    verify_forest_registry_unlocked( result );
}

/**
 * @brief Verify forest registry and system domain integrity.
 *
 * @param result  VerifyResult to populate with diagnostic entries.
 */
static void verify_forest_registry_unlocked( VerifyResult& result ) noexcept
{
    const forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
    {
        result.add( ViolationType::ForestRegistryMissing, DiagnosticAction::NoAction );
        return;
    }
    if ( reg->magic != detail::kForestRegistryMagic || reg->version != detail::kForestRegistryVersion )
    {
        result.add( ViolationType::ForestRegistryMissing, DiagnosticAction::NoAction, 0,
                    static_cast<std::uint64_t>( detail::kForestRegistryMagic ),
                    static_cast<std::uint64_t>( reg->magic ) );
        return;
    }

    static constexpr const char* kRequired[] = { detail::kSystemDomainFreeTree, detail::kSystemDomainSymbols,
                                                 detail::kSystemDomainRegistry, detail::kServiceNameLegacyRoot };
    for ( const char* name : kRequired )
    {
        const forest_domain* rec = find_domain_by_name_unlocked( name );
        if ( rec == nullptr )
        {
            result.add( ViolationType::ForestDomainMissing, DiagnosticAction::NoAction );
            continue;
        }
        if ( ( rec->flags & detail::kForestDomainFlagSystem ) == 0 )
        {
            result.add( ViolationType::ForestDomainFlagsMissing, DiagnosticAction::NoAction );
        }
    }
}
