#pragma once
#include <cstddef>
#include <cstdint>
namespace pmm
{
using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;
/*
## pmm-recoverymode
req: feat-004, fr-014, fr-024, qa-rec-001
*/
enum class RecoveryMode : uint8_t
{
    Verify = 0,
    Repair = 1,
};
/*
## pmm-violationtype
req: feat-004, fr-014, fr-024, qa-rec-001
*/
enum class ViolationType : uint8_t
{
    None = 0,
    BlockStateInconsistent,
    PrevOffsetMismatch,
    CounterMismatch,
    FreeTreeStale,
    ForestRegistryMissing,
    ForestDomainMissing,
    ForestDomainFlagsMissing,
    HeaderCorruption,
};
/*
## pmm-diagnosticaction
req: feat-004, fr-014, fr-024, qa-rec-001
*/
enum class DiagnosticAction : uint8_t
{
    NoAction = 0,
    Repaired,
    Rebuilt,
    Aborted,
};
/*
## pmm-diagnosticentry
req: feat-004, fr-014, fr-024, qa-rec-001
*/
struct DiagnosticEntry
{
    ViolationType    type        = ViolationType::None;
    DiagnosticAction action      = DiagnosticAction::NoAction;
    uint64_t         block_index = 0;
    uint64_t         expected    = 0;
    uint64_t         actual      = 0;
};
inline constexpr size_t kMaxDiagnosticEntries = 64;
/*
## pmm-verifyresult
req: feat-004, fr-014, fr-024, qa-rec-001, feat-010, fr-015, if-010, qa-diag-001
*/
struct VerifyResult
{
    RecoveryMode    mode                           = RecoveryMode::Verify;
    bool            ok                             = true;
    size_t          violation_count                = 0;
    DiagnosticEntry entries[kMaxDiagnosticEntries] = {};
    size_t          entry_count                    = 0;
    void            add( ViolationType type, DiagnosticAction action, uint64_t block_index = 0, uint64_t expected = 0,
                         uint64_t actual = 0 ) noexcept
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
}
