#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"
TARGET="native"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
CLEAN=0
VERBOSE=0
SHOW_HELP=0
if [[ $# -gt 0 && "$1" != -* ]]; then
TARGET="$1"
shift
fi
while [[ $# -gt 0 ]]; do
case "$1" in
-c|--clean)
CLEAN=1
shift
;;
-j*)
JOBS="${1#-j}"
[[ -z "$JOBS" ]] && { echo "[build] Error: -j requires a value"; exit 1; }
shift
;;
--jobs)
[[ $# -lt 2 ]] && { echo "[build] Error: --jobs requires a value"; exit 1; }
JOBS="$2"
shift 2
;;
-v|--verbose)
VERBOSE=1
shift
;;
-h|--help)
            SHOW_HELP=1
            shift
            ;;
        *)
            echo "[build] Unknown option: $1"
            exit 1
            ;;
    esac
done
if [[ "$SHOW_HELP" -eq 1 ]]; then
    cat <<'EOF'
Catalyst Build Script
Usage: ./scripts/build.sh [TARGET] [OPTIONS]
Targets:
  native
  release
  release-linux
  release-win
  linux-x86-64
  linux-sse41
  linux-avx2
  linux-bmi2
  linux-avx512
  linux-avx512vnni
  win-x86-64
  win-sse41
  win-avx2
  win-bmi2
  win-avx512
  win-avx512vnni
  pgo
  debug
  datagen
Options:
  -c, --clean
  -j, --jobs N
  -v, --verbose
  -h, --help
EOF
    exit 0
fi
VALID_TARGETS=(
    native
    release
    release-linux
    release-win
    pgo
    debug
    datagen
    linux-x86-64
    linux-sse41
    linux-avx2
    linux-bmi2
    linux-avx512
    linux-avx512vnni
    win-x86-64
    win-sse41
    win-avx2
    win-bmi2
    win-avx512
    win-avx512vnni
)
valid=0
for t in "${VALID_TARGETS[@]}"; do
    if [[ "$TARGET" == "$t" ]]; then
        valid=1
        break
    fi
done
if [[ "$valid" -eq 0 ]]; then
    echo "[build] Error: Unknown target '${TARGET}'"
    exit 1
fi
if [[ "$CLEAN" -eq 1 ]]; then
    echo "[build] Cleaning..."
    make clean
fi
MAKEFLAGS="-j${JOBS}"
[[ "$VERBOSE" -eq 1 ]] && MAKEFLAGS+=" V=1"
export MAKEFLAGS
echo "[build] Target: ${TARGET}"
echo "[build] Jobs  : ${JOBS}"
echo
make "$TARGET"
echo
echo "[build] Done. Binaries in: ${PROJECT_ROOT}/bin/"
ls -lh "${PROJECT_ROOT}/bin/" 2>/dev/null || echo "[build] No binaries found"