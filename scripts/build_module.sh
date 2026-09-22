#!/usr/bin/env bash
set -euo pipefail

# build_module.sh — one command from clean checkout to a flashable module zip:
#   builds libicebear_translator.so with the NDK, then zips module/ into
#   build/icebear-module.zip. Sources the toolchain written by
#   scripts/setup_deps.sh if present; otherwise uses whatever's on your PATH.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="$ROOT_DIR/build/tools/env.sh"

if [ -f "$ENV_FILE" ]; then
  echo ">> Sourcing toolchain from $ENV_FILE"
  # shellcheck disable=SC1090
  . "$ENV_FILE"
fi

if [ -z "${ANDROID_NDK_HOME:-}" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
  echo "! ANDROID_NDK_HOME isn't set (or points at a missing directory)." >&2
  echo "  Run scripts/setup_deps.sh first, or export ANDROID_NDK_HOME yourself." >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "! cmake isn't on PATH." >&2
  echo "  Run scripts/setup_deps.sh first, or install cmake so it's on PATH." >&2
  exit 1
fi

echo
echo "== Building the native translator (.so) =="
bash "$ROOT_DIR/scripts/build_native.sh"

echo
echo "== Packaging the flashable module =="
bash "$ROOT_DIR/scripts/package_module.sh"

echo
echo "Done. Flash this in your root manager app (Magisk/KernelSU/APatch):"
echo "  $ROOT_DIR/build/icebear-module.zip"