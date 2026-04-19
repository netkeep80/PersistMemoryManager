#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workflow="$repo_root/.github/workflows/repo-guard.yml"
policy="$repo_root/repo-policy.json"

python3 - "$workflow" "$policy" <<'PY'
import json
import pathlib
import sys

workflow_path = pathlib.Path(sys.argv[1])
policy_path = pathlib.Path(sys.argv[2])

workflow = workflow_path.read_text(encoding="utf-8")
policy = json.loads(policy_path.read_text(encoding="utf-8"))

expected_action = "netkeep80/repo-guard@c8491872866c33e0ecd04f809dcfd0489047d13f"
required_governance_paths = {
    ".github/workflows/repo-guard.yml",
    ".github/workflows/docs-consistency.yml",
    ".github/PULL_REQUEST_TEMPLATE.md",
    ".github/ISSUE_TEMPLATE/change-contract.yml",
    "scripts/check-repo-guard-rollout.sh",
}

checks = [
    (
        expected_action in workflow,
        f"repo-guard workflow must use the pinned reusable Action {expected_action}",
    ),
    ("mode: check-pr" in workflow, "repo-guard workflow must run check-pr mode"),
    (
        "enforcement: advisory" in workflow,
        "repo-guard workflow must run in advisory enforcement mode",
    ),
    ("fetch-depth: 0" in workflow, "repo-guard workflow must use full checkout history"),
    (
        "continue-on-error" not in workflow,
        "repo-guard workflow must not mask runtime/configuration failures with continue-on-error",
    ),
    (
        "git clone https://github.com/netkeep80/repo-guard.git" not in workflow,
        "repo-guard workflow must not use the legacy manual clone integration",
    ),
    (
        "npm ci" not in workflow and "npm install" not in workflow,
        "repo-guard workflow must not install repo-guard manually",
    ),
    (
        "node /tmp/repo-guard/src/repo-guard.mjs" not in workflow,
        "repo-guard workflow must not invoke the cloned CLI directly",
    ),
]

policy_mode = policy.get("enforcement", {}).get("mode")
checks.append((policy_mode == "advisory", "repo-policy.json must default to advisory enforcement"))

governance_paths = set(policy.get("paths", {}).get("governance_paths", []))
missing_governance = sorted(required_governance_paths - governance_paths)
checks.append(
    (
        not missing_governance,
        "repo-policy.json governance_paths must include: " + ", ".join(missing_governance),
    )
)

expected_surfaces = {"kernel", "tests", "docs", "governance", "release", "generated"}
surfaces = set(policy.get("surfaces", {}))
checks.append((expected_surfaces <= surfaces, "repo-policy.json surfaces must include PMM v1 surfaces"))

expected_change_classes = {
    "governance",
    "docs-comments-cleanup",
    "kernel-compaction",
    "kernel-hardening",
    "extraction-prep",
    "generated-refresh",
    "release",
}
change_classes = set(policy.get("change_classes", []))
checks.append((expected_change_classes <= change_classes, "repo-policy.json change_classes must include PMM v1 types"))

new_file_rules = policy.get("new_file_rules", {})
checks.append((expected_change_classes <= set(new_file_rules), "new_file_rules must cover PMM v1 classes"))

new_file_classes = policy.get("new_file_classes", {})
checks.append(
    (
        "kernel_module" in new_file_classes,
        "new_file_classes must distinguish legitimate kernel modules from forbidden shard files",
    )
)

for change_type in ("kernel-compaction", "extraction-prep"):
    rule = new_file_rules.get(change_type, {})
    allowed_classes = set(rule.get("allow_classes", []))
    max_per_class = rule.get("max_per_class", {})
    checks.append(
        (
            "kernel_module" in allowed_classes and max_per_class.get("kernel_module") == 1,
            f"{change_type} must allow exactly one legitimate kernel module extraction file",
        )
    )

change_type_rules = policy.get("change_type_rules", {})
checks.append((expected_change_classes <= set(change_type_rules), "change_type_rules must cover PMM v1 types"))
checks.append(("surface_matrix" not in policy, "surface_matrix is intentionally deferred in this rollout"))

registry_rules = policy.get("registry_rules", [])
checks.append(
    (
        any(rule.get("id") == "canonical-docs-sync" for rule in registry_rules),
        "repo-policy.json registry_rules must include canonical-docs-sync",
    )
)

missing_files = [
    path for path in required_governance_paths if not (policy_path.parent / path).exists()
]
checks.append(
    (
        not missing_files,
        "governed repo-guard rollout files must exist: " + ", ".join(sorted(missing_files)),
    )
)

failures = [message for ok, message in checks if not ok]
if failures:
    print("Repo-guard rollout check failed:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print("Repo-guard rollout wiring is current.")
PY
