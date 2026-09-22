#!/usr/bin/env bash
set -euo pipefail

# Packages module/ (including its WebUI) into a flashable zip. Needs
# libicebear_translator.so to already exist, see scripts/build_native.sh.
#
# Zip creation is tool-agnostic so this works on stock Linux and Windows
# Git Bash alike: uses `zip` if present, else python3's stdlib zipfile,
# else bsdtar (bundled with Windows/macOS). scripts/setup_deps.sh doesn't
# need to install anything extra for this step.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SO_PATH="$ROOT_DIR/native/build/libicebear_translator.so"

if [ ! -f "$SO_PATH" ]; then
  echo "Run scripts/build_native.sh first, no translator .so found." >&2
  exit 1
fi

mkdir -p "$ROOT_DIR/module/system/lib64"
cp "$SO_PATH" "$ROOT_DIR/module/system/lib64/"

mkdir -p "$ROOT_DIR/build"
OUT="$ROOT_DIR/build/icebear-module.zip"
rm -f "$OUT"

# Everything module/ ships, at the zip's top level (a Magisk-style module
# zip has no parent folder). Keep this list in sync with module/README.md.
MODULE_ENTRIES=(
  customize.sh
  module.prop
  post-fs-data.sh
  service.sh
  system
  system.prop
  uninstall.sh
  webroot
)

pack_with_zip() {
  (cd "$ROOT_DIR/module" && zip -r -X "$OUT" . -x "README.md")
}

pack_with_python() {
  (cd "$ROOT_DIR/module" && python3 -m zipfile -c "$OUT" "${MODULE_ENTRIES[@]}")
}

pack_with_bsdtar() {
  local tar_bin="$1"
  (cd "$ROOT_DIR/module" && "$tar_bin" --exclude="README.md" -a -cf "$OUT" .)
}

# The Windows Store "python3" stub passes command -v but isn't real Python.
python3_ok() {
  python3 -c 'import sys' >/dev/null 2>&1
}

# bsdtar is bundled with Windows (System32\tar.exe) and macOS; GNU tar is not
# (it can't write .zip at all), so only trust an explicit bsdtar.
bsdtar_bin() {
  if (tar --version 2>/dev/null | grep -qi bsdtar); then
    command -v tar
  elif [ -n "${WINDIR:-}" ] && [ -x "$WINDIR/System32/tar.exe" ]; then
    printf '%s' "$WINDIR/System32/tar.exe"
  fi
}

# Sanity-check the result is actually a zip (PK\x03\x04 magic), so a
# silently-wrong packer (GNU tar writing .zip, a fake python3) gets caught.
is_zip() {
  [ -f "$OUT" ] || return 1
  [ "$(head -c 2 "$OUT")" = "PK" ]
}

pack() {
  if command -v zip >/dev/null 2>&1; then pack_with_zip && return; fi
  if python3_ok; then pack_with_python && return; fi
  local tar_bin
  if tar_bin="$(bsdtar_bin)"; then pack_with_bsdtar "$tar_bin" && return; fi
  echo "No usable zip, python3, or bsdtar available; can't package the module." >&2
  echo "Run scripts/setup_deps.sh (or install one of those tools)." >&2
  exit 1
}

pack
if ! is_zip; then
  echo "$OUT doesn't look like a zip; the packer failed." >&2
  exit 1
fi

echo "Wrote $OUT"