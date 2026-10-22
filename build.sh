#!/usr/bin/env bash
# Build Dread Bass for the Korg microKORG 2 without Docker.
#
#   ./build.sh            -> dist/DreadBass.mk2unit and dist/DreadMtr.mk2unit
#
# Korg's arm-unknown-linux-gnueabihf toolchain (the same download as Korg's
# Docker image) and the logue SDK are fetched into .deps/ on first run.
# Linux x86_64 / aarch64 hosts (and GitHub Actions). On a Mac, use Korg's
# Docker environment instead (see README).
# Set DREAD_STRICT=1 (as CI does) to fail instead of skipping.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="$ROOT/.deps"
SDK_DIR="$DEPS/logue-sdk"
SDK_REPO="https://github.com/korginc/logue-sdk"
SDK_COMMIT="e78b16c6068b1c30fc072f2aebb8da5eef6c005c"
STRICT="${DREAD_STRICT:-0}"

mkdir -p "$DEPS" "$ROOT/dist"

if [ ! -d "$SDK_DIR/platform/microkorg2" ]; then
  echo "==> Fetching logue SDK"
  git clone -q "$SDK_REPO" "$SDK_DIR"
  git -C "$SDK_DIR" checkout -q "$SDK_COMMIT"
fi

HOST="$(uname -s)-$(uname -m)"

# Same archives and checksums as logue-sdk/docker/docker-app/builder/get_microkorg2_toolchain.sh
case "$HOST" in
  Linux-x86_64)
    URL="https://www.dropbox.com/s/3curhjx9yip29os/drumlogue-toolchain-amd64.tar.bz2?dl=1"
    SHA1="6b2a403195fbac0f7ee8d53bea4ed17b8c14e541" ;;
  Linux-aarch64)
    URL="https://www.dropbox.com/s/ioysa018ruopp72/drumlogue-toolchain-aarch64.tar.bz2?dl=1"
    SHA1="d907b640967e8ea40baa82ff55d8813716028aa9" ;;
  *)
    URL="" ;;
esac
TC_DIR="$DEPS/mk2-toolchain"

GCC="$(find "$TC_DIR" -type f -name 'arm-unknown-linux-gnueabihf-gcc' -path '*/bin/*' 2>/dev/null | head -1 || true)"
if [ -z "$GCC" ] && [ -n "$URL" ]; then
  echo "==> Downloading Korg microKORG 2 toolchain"
  ARCHIVE="$DEPS/mk2-toolchain.tar.bz2"
  if curl -fsSL -o "$ARCHIVE" "$URL"; then
    GOT="$( (sha1sum "$ARCHIVE" 2>/dev/null || shasum -a 1 "$ARCHIVE") | cut -d' ' -f1)"
    if [ "$GOT" = "$SHA1" ]; then
      mkdir -p "$TC_DIR" && tar xjf "$ARCHIVE" -C "$TC_DIR"
    else
      echo "    (checksum mismatch: $GOT)"
    fi
    rm -f "$ARCHIVE"
  else
    echo "    (download unavailable)"
  fi
  GCC="$(find "$TC_DIR" -type f -name 'arm-unknown-linux-gnueabihf-gcc' -path '*/bin/*' 2>/dev/null | head -1 || true)"
fi
if [ -z "$GCC" ]; then
  if [ "$STRICT" = "1" ]; then
    echo "microKORG 2 toolchain required (DREAD_STRICT=1) but not available"; exit 1
  fi
  echo "==> Korg's microKORG 2 toolchain isn't available on this machine."
  echo "    Push to GitHub (Actions builds it) or use Korg's Docker environment (see README)."
  exit 0
fi
echo "==> microKORG 2 toolchain: $("$GCC" --version | head -1)"

PLAT="$SDK_DIR/platform/microkorg2"
build_one() {  # dir, output name
  # clean first: the Makefile doesn't track the shared core/ header
  make -s -C "$1" clean >/dev/null
  make -C "$1" install \
    CROSS_COMPILE="${GCC%gcc}" \
    COMMON_INC_PATH="$PLAT/common" \
    COMMON_SRC_PATH="$PLAT/common" \
    PLATFORM_COMMON_PATH="$SDK_DIR/platform/common" \
    INSTALLDIR="$ROOT/dist"
  echo "==> Done: dist/$2"
}

echo "==> Building Dread Bass for microKORG 2"
build_one "$ROOT/microkorg2" DreadBass.mk2unit

# Load-meter build (same DSP, CPU load shown in the MODE display)
echo "==> Building DreadMtr (Dread Bass + CPU load meter) for microKORG 2"
build_one "$ROOT/tools/mk2-meter" DreadMtr.mk2unit
