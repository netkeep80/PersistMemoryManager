/**
 * @file pmm/diagnostics.h
 * @brief Structured diagnostics for verify and repair modes (Issue #245).
 *
 * Provides:
 *   - `RecoveryMode`     — enum to select verify-only vs. repair behaviour
 *   - `DiagnosticEntry`  — single violation record with type, affected block, action
 *   - `VerifyResult`     — aggregated result of a verify or repair pass
 *
 * The verify mode performs read-only diagnostics without modifying the image.
 * The repair mode performs the same diagnostics, then applies documented fixes.
 * Both modes populate a VerifyResult with structured diagnostic entries.
 *
 * @see block_state.h  — block-level verify_state / recover_state
 * @see allocator_policy.h — verify_linked_list, verify_counters, verify_free_tree_candidates
 * @see persist_memory_manager.h — PersistMemoryManager::verify, load
 * @version 1.0 (Issue #245)
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace pmm
{

/// @brief Mode for recovery operations: verify-only or repair.
enum class RecoveryMode : std::uint8_t
{
    Verify = 0, ///< Read-only diagnostics; no modifications to the image.
    Repair = 1, ///< Diagnose and then apply documented fixes.
};

/// @brief Type of structural violation detected during verify/repair.
enum class ViolationType : std::uint8_t
{
    None = 0,                 ///< No violation.
    BlockStateInconsistent,   ///< Block weight/root_offset mismatch (transitional state).
    PrevOffsetMismatch,       ///< prev_offset does not match expected value.
    CounterMismatch,          ///< Recomputed counter differs from stored value.
    FreeTreeStale,            ///< Free tree root or AVL fields need rebuild.
    ForestRegistryMissing,    ///< Forest registry not found or invalid.
    ForestDomainMissing,      ///< Required system domain not found.
    ForestDomainFlagsMissing, ///< System domain lacks required flags.
    HeaderCorruption,         ///< Magic, granule_size, or total_size mismatch.
};

/// @brief Action taken (or that would be taken) for a violation.
enum class DiagnosticAction : std::uint8_t
{
    NoAction = 0,   ///< No action (verify mode or no fix available).
    Repaired,       ///< Field was repaired to correct value.
    Rebuilt,        ///< Structure was rebuilt from scratch (AVL tree, counters).
    Aborted,        ///< Recovery aborted — corruption too severe.
};

/// @brief A single diagnostic entry describing one violation.
struct DiagnosticEntry
{
    ViolationType  type   = ViolationType::None;     ///< Kind of violation.
    DiagnosticAction action = DiagnosticAction::NoAction; ///< Action taken/proposed.
    std::uint64_t  block_index = 0;                  ///< Affected block granule index (0 if N/A).
    std::uint64_t  expected    = 0;                  ///< Expected value (interpretation depends on type).
    std::uint64_t  actual      = 0;                  ///< Actual value found.
};

/// @brief Maximum number of diagnostic entries stored in a VerifyResult.
///
/// Keeps VerifyResult on the stack without dynamic allocation.
/// Additional violations beyond this limit are counted but not detailed.
inline constexpr std::size_t kMaxDiagnosticEntries = 64;

/// @brief Aggregated result of a verify or repair pass.
struct VerifyResult
{
    RecoveryMode mode = RecoveryMode::Verify; ///< Mode used for this pass.
    bool         ok   = true;                 ///< true if no violations found.

    /// @brief Number of violations detected.
    std::size_t violation_count = 0;

    /// @brief Detailed entries (up to kMaxDiagnosticEntries).
    DiagnosticEntry entries[kMaxDiagnosticEntries] = {};

    /// @brief Number of entries stored (may be < violation_count if overflow).
    std::size_t entry_count = 0;

    /// @brief Add a diagnostic entry. Thread-unsafe — caller holds lock.
    void add( ViolationType type, DiagnosticAction action, std::uint64_t block_index = 0,
              std::uint64_t expected = 0, std::uint64_t actual = 0 ) noexcept
    {
        ok = false;
        violation_count++;
        if ( entry_count < kMaxDiagnosticEntries )
        {
            entries[entry_count].type        = type;
            entries[entry_count].action      = action;
            entries[entry_count].block_index = block_index;
            entries[entry_count].expected    = expected;
            entries[entry_count].actual      = actual;
            entry_count++;
        }
    }
};

} // namespace pmm
