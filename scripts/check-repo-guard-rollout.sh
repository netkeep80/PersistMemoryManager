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

expected_action = "netkeep80/repo-guard@b1d16b8eb69755898c248d6e3dde31fa03d1becc"
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
