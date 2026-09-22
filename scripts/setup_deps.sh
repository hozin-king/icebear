#!/usr/bin/env bash
set -euo pipefail

# setup_deps.sh — installs a portable toolchain for building Icebear's
# flashable module zip (and the Gradle :core/:desktop builds).
#
# Works on GNU/Linux and on Windows via Git Bash / MSYS2 / Cygwin. Every
# download lands in build/tools/ (gitignored), nothing touches your system
# installs, no admin rights needed. An environment file
# (build/tools/env.sh) is written so scripts/build_module.sh can pick up
# the right cmake / NDK / JDK automatically.
#
#   NDK r27c   -> clang + android.toolchain.cmake (same version as CI)
#   CMake      -> host build & Android cross build
#   Ninja      -> the generator android.toolchain.cmake prefers
#   JDK 21     -> the Gradle toolchain for :core and :desktop

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$ROOT_DIR/build/tools"
FORCE=0
SKIP_JDK=0

usage() {
  cat <<'EOF'
Usage: scripts/setup_deps.sh [--force] [--skip-jdk]

Installs what's needed to build Icebear's flashable module zip:
  Android NDK (clang + android.toolchain.cmake), CMake, Ninja, JDK 21.

Everything lands in build/tools/ (gitignored), portable, no admin rights.
Afterwards run:

  bash scripts/build_module.sh
EOF
  exit 0
}

for arg in "$@"; do
  case "$arg" in
    --help | -h) usage ;;
    --force) FORCE=1 ;;
    --skip-jdk) SKIP_JDK=1 ;;
    *) echo "Unknown option: $arg" >&2; exit 1 ;;
  esac
done

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------

SYSTEM="$(uname -s)"
MACHINE="$(uname -m)"

case "$SYSTEM" in
  Linux*) OS="linux" ;;
  MINGW* | MSYS* | CYGWIN*) OS="windows" ;;
  *)
    echo "Unsupported OS: $SYSTEM (this script supports Linux and Windows via Git Bash/MSYS2/Cygwin)." >&2
    exit 1
    ;;
esac

case "$MACHINE" in
  x86_64 | amd64) ARCH="x86_64" ;;
  aarch64 | arm64) ARCH="aarch64" ;;
  *)
    echo "Unsupported architecture: $MACHINE" >&2
    exit 1
    ;;
esac

# ---------------------------------------------------------------------------
# Versions and download URLs
# ---------------------------------------------------------------------------

NDK_VERSION="r27c"                 # matches .github/workflows/build.yml
NDK_URL="https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-${OS}.zip"

CMAKE_VERSION="4.4.2"
case "$OS" in
  linux)
    [ "$ARCH" = "aarch64" ] && CMAKE_PLATFORM="linux-aarch64" || CMAKE_PLATFORM="linux-x86_64"
    CMAKE_EXT="tar.gz"
    ;;
  windows)
    CMAKE_PLATFORM="windows-x86_64"
    CMAKE_EXT="zip"
    ;;
esac
CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${CMAKE_PLATFORM}.${CMAKE_EXT}"

NINJA_VERSION="1.12.1"
case "$OS" in
  linux)
    [ "$ARCH" = "aarch64" ] && NINJA_ASSET="ninja-linux-aarch64.zip" || NINJA_ASSET="ninja-linux.zip"
    ;;
  windows)
    NINJA_ASSET="ninja-win.zip"
    ;;
esac
NINJA_URL="https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/${NINJA_ASSET}"

JDK_OS="$OS"
[ "$OS" = "linux" ] && [ "$ARCH" = "aarch64" ] && JDK_ARCH="aarch64" || JDK_ARCH="x64"
JDK_URL="https://api.adoptium.net/v3/binary/latest/21/ga/${JDK_OS}/${JDK_ARCH}/jdk/hotspot/normal/eclipse?project=jdk"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

dl() {
  local url="$1" out="$2"
  echo ">> downloading $(basename "$out")"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --retry-delay 2 -o "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$out" "$url"
  else
    echo "Neither curl nor wget is available." >&2
    return 1
  fi
}

# The Windows Store "python3" stub passes command -v but isn't real Python.
python3_ok() {
  python3 -c 'import sys' >/dev/null 2>&1
}

unpack_zip() {
  local file="$1" dest="$2"
  mkdir -p "$dest"
  if command -v unzip >/dev/null 2>&1; then
    (cd "$dest" && unzip -q "$file")
  elif python3_ok; then
    (cd "$dest" && python3 -m zipfile -e "$file" .)
  elif (tar --version 2>/dev/null | grep -qi bsdtar); then
    (cd "$dest" && tar -xf "$file")
  else
    echo "No tool available to extract $file: need unzip, python3, or bsdtar." >&2
    return 1
  fi
}

# Single top-level directory inside an extraction dir (NDK, CMake, JDK all
# unpack to one version-named folder).
top_dir() {
  local dir="$1" count name
  count="$(ls -1 "$dir" | wc -l | tr -d ' ')"
  name="$(ls -1 "$dir" | head -1)"
  if [ "$count" -ne 1 ] || [ -z "$name" ]; then
    echo "Unexpected layout in $dir" >&2
    return 1
  fi
  printf '%s/%s' "$dir" "$name"
}

# ---------------------------------------------------------------------------
# Install each tool (skip if already present unless --force)
# ---------------------------------------------------------------------------

mkdir -p "$TOOLS_DIR"
PKG_DIR="$TOOLS_DIR/pkgs"
mkdir -p "$PKG_DIR"

need() { [ "$FORCE" = "1" ] || [ ! -d "$1" ]; }

# --- Android NDK -----------------------------------------------------------
NDK_DIR="$TOOLS_DIR/ndk/android-ndk-$NDK_VERSION"
if need "$NDK_DIR"; then
  NDK_ZIP="$PKG_DIR/android-ndk-$NDK_VERSION.zip"
  [ -f "$NDK_ZIP" ] || dl "$NDK_URL" "$NDK_ZIP"
  echo ">> extracting NDK $NDK_VERSION (this is the big one)"
  unpack_zip "$NDK_ZIP" "$PKG_DIR/ndk-unpack"
  mkdir -p "$TOOLS_DIR/ndk"
  rm -rf "$NDK_DIR"
  mv "$(top_dir "$PKG_DIR/ndk-unpack")" "$NDK_DIR"
else
  echo "-- NDK already present at $NDK_DIR"
fi
if [ ! -f "$NDK_DIR/build/cmake/android.toolchain.cmake" ]; then
  echo "NDK looks incomplete at $NDK_DIR, rerun with --force." >&2
  exit 1
fi

# --- CMake ------------------------------------------------------------------
CMAKE_DIR="$(top_dir "$TOOLS_DIR/cmake" 2>/dev/null || true)"
if need "$TOOLS_DIR/cmake" || [ ! "$(ls -1 "$TOOLS_DIR/cmake" 2>/dev/null | wc -l | tr -d ' ')" = "1" ]; then
  CMAKE_ARC="$PKG_DIR/cmake-${CMAKE_VERSION}-${CMAKE_PLATFORM}.${CMAKE_EXT}"
  [ -f "$CMAKE_ARC" ] || dl "$CMAKE_URL" "$CMAKE_ARC"
  echo ">> extracting cmake $CMAKE_VERSION"
  rm -rf "$TOOLS_DIR/cmake"
  mkdir -p "$TOOLS_DIR/cmake"
  if [ "$CMAKE_EXT" = "zip" ]; then
    unpack_zip "$CMAKE_ARC" "$TOOLS_DIR/cmake"
  else
    tar -xzf "$CMAKE_ARC" -C "$TOOLS_DIR/cmake"
  fi
else
  echo "-- cmake already present at $(top_dir "$TOOLS_DIR/cmake")"
fi
CMAKE_DIR="$(top_dir "$TOOLS_DIR/cmake")"
[ -x "$CMAKE_DIR/bin/cmake" ] || { echo "cmake binary missing at $CMAKE_DIR/bin" >&2; exit 1; }

# --- Ninja ------------------------------------------------------------------
NINJA_DIR="$TOOLS_DIR/ninja"
if need "$NINJA_DIR"; then
  NINJA_ZIP="$PKG_DIR/$NINJA_ASSET"
  [ -f "$NINJA_ZIP" ] || dl "$NINJA_URL" "$NINJA_ZIP"
  echo ">> extracting ninja $NINJA_VERSION"
  rm -rf "$NINJA_DIR"
  unpack_zip "$NINJA_ZIP" "$NINJA_DIR"
  # ninja-win.zip extracts to ninja.exe at the root; linux zip to "ninja".
  chmod +x "$NINJA_DIR/ninja"* 2>/dev/null || true
else
  echo "-- ninja already present at $NINJA_DIR"
fi
if [ ! -x "$NINJA_DIR/ninja" ] && [ ! -x "$NINJA_DIR/ninja.exe" ]; then
  echo "ninja binary missing in $NINJA_DIR, rerun with --force." >&2
  exit 1
fi

# --- JDK 21 (optional, for :core / :desktop builds) ------------------------
JDK_DIR=""
if [ "$SKIP_JDK" = "1" ]; then
  echo "-- skipping JDK (--skip-jdk)"
else
  if need "$TOOLS_DIR/jdk"; then
    JDK_EXT="$([ "$OS" = "windows" ] && echo zip || echo tar.gz)"
    JDK_ARC="$PKG_DIR/temurin-jdk-21.$JDK_EXT"
    [ -f "$JDK_ARC" ] || dl "$JDK_URL" "$JDK_ARC"
    echo ">> extracting JDK 21"
    rm -rf "$TOOLS_DIR/jdk"
    mkdir -p "$TOOLS_DIR/jdk"
    if [ "$JDK_EXT" = "zip" ]; then
      unpack_zip "$JDK_ARC" "$TOOLS_DIR/jdk"
    else
      tar -xzf "$JDK_ARC" -C "$TOOLS_DIR/jdk"
    fi
  else
    echo "-- JDK already present"
  fi
  JDK_DIR="$(top_dir "$TOOLS_DIR/jdk")"
  [ -x "$JDK_DIR/bin/java" ] || [ -x "$JDK_DIR/bin/java.exe" ] || { echo "JDK looks broken at $JDK_DIR" >&2; exit 1; }
fi

# ---------------------------------------------------------------------------
# Write the environment file the build scripts source
# ---------------------------------------------------------------------------

ENV_FILE="$TOOLS_DIR/env.sh"
{
  echo "# Generated by scripts/setup_deps.sh. Source it before building with:"
  echo "#   . \"$ENV_FILE\""
  echo "export ANDROID_NDK_HOME=\"$NDK_DIR\""
  echo "export JAVA_HOME=\"$JDK_DIR\""
  echo "export PATH=\"$CMAKE_DIR/bin:$NINJA_DIR${JDK_DIR:+:$JDK_DIR/bin}:\$PATH\""
} > "$ENV_FILE"

echo
echo "Toolchain ready."
echo "  NDK:   $NDK_DIR"
echo "  CMake: $CMAKE_DIR"
echo "  Ninja: $NINJA_DIR"
[ -n "$JDK_DIR" ] && echo "  JDK:   $JDK_DIR"
echo
echo "Next step:"
echo "  bash scripts/build_module.sh"
echo
echo "Env file for your shell (scripts/build_module.sh sources it automatically):"
echo "  . \"$ENV_FILE\""