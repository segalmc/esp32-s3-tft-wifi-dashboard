#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "========================================"
echo " ESP32 Hot Water - Git Checkpoint"
echo "========================================"

# Never mix pre-staged changes into this checkpoint.
if ! git diff --cached --quiet; then
    echo "ERROR: Git already contains staged changes."
    echo "Review them before continuing."
    exit 1
fi

echo
echo "Current branch:"
git branch --show-current

echo
echo "Changes to preserve:"
git status --short

echo
read -r -p "Create checkpoint commit? [y/N] " ANSWER

if [[ "$ANSWER" != "y" && "$ANSWER" != "Y" ]]; then
    echo "Cancelled. No changes made."
    exit 0
fi

# Stage all tracked modifications and the two project scripts.
git add -u
git add tools/validate_s3_liveness.sh
git add tools/save_project_checkpoint.sh

git diff --cached --check

git commit \
    -m "Checkpoint: telemetry liveness and secure OTA validation"

echo
echo "========================================"
echo " CHECKPOINT CREATED"
echo "========================================"

git log -1 --oneline
echo
git status --short

echo
echo "No firmware upload or device reset performed."
