#!/usr/bin/env bash
# Builds ReXGlue's Release and RelWithDebInfo librexruntime.so and librexgpu-xenos.so (the Vulkan GPU plugin) from the
# pinned v0.10.0 source with this project's patches and installs them into an extracted linux-amd64 SDK folder.
# tools/build.sh and tools/package-linux.sh run this themselves, and the CMake build checks the SDK with --check; see
# docs/build-workflow.md.
#
#   tools/build-patched-rexruntime.sh SDK_DIR [WORK_DIR]    build and install (network needed the first time)
#   tools/build-patched-rexruntime.sh --check SDK_DIR [--config CONFIG] [--staged DIR]   verify only
#   tools/build-patched-rexruntime.sh --list                print the patches applied here, in order, and the fingerprint
#   tools/build-patched-rexruntime.sh --stress SDK_DIR BUILD_DIR   run the allocator stress test (see below)
#   tools/build-patched-rexruntime.sh --loader-test SDK_DIR BUILD_DIR   run the RenderDoc detection test (see below)
#
# --check verifies the patched libraries in SDK_DIR and, with --staged, that DIR holds the SDK's runtime and GPU plugin
# of CONFIG (Debug, RelWithDebInfo or Release; default Release). The Debug libraries stay stock, so for Debug only the
# staged copies are checked. WORK_DIR holds the fetched source and its build (default: out/rexglue-sdk-src in this
# repository). Needs git, clang-20, clang++-20, cmake 3.25+, ninja, binutils and the packages installed by
# tools/setup-linux-build-env.sh. --stress and --loader-test run crackdown_physical_heap_stress and
# crackdown_renderdoc_detect_tests
# from an already configured Release project build in BUILD_DIR, beside the installed runtime; --loader-test also runs
# its test against the stock runtime (from the SDK download kept beside SDK_DIR), which must fail.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH_DIR="$ROOT/patches/rexglue-sdk"
PATCH_PLATFORM=linux
STAMP_NAME=rexruntime-patch.txt
LIBS=(lib/librexruntime.so lib/librexruntimerd.so lib/librexgpu-xenos.so lib/librexgpu-xenosrd.so)
# How the patched files are built; part of the fingerprint, so changing them invalidates the installed files.
CC=clang-20
CXX=clang++-20
BUILD_FLAGS=-march=x86-64-v2
LINK_RPATH='$ORIGIN:$ORIGIN/../lib'
BUILD_TARGETS=(rexruntime rexgpu-xenos)
BUILD_CONFIGS=(RelWithDebInfo Release)
# The runtime loads the GPU plugin through these.
PLUGIN_ENTRY_POINTS=(rex_gpu_abi_version rex_gpu_create)

fail() { echo "ERROR: $*" >&2; exit 1; }
# baseline.env holds KEY=VALUE lines whose keys may contain slashes, so it is read, not sourced.
baseline() { sed -n "s|^$1=||p" "$PATCH_DIR/baseline.env" | tr -d '\r'; }
SDK_VERSION="$(baseline SDK_VERSION)"
SDK_TAG="$(baseline SDK_TAG)"
SDK_COMMIT="$(baseline SDK_COMMIT)"
SDK_REPO="$(baseline SDK_REPO)"
[ -n "$SDK_VERSION" ] && [ -n "$SDK_TAG" ] && [ -n "$SDK_COMMIT" ] && [ -n "$SDK_REPO" ] ||
  fail "patches/rexglue-sdk/baseline.env lacks the SDK pin"
sha() { sha256sum "$1" | cut -d' ' -f1; }
text_sha() { sha256sum | cut -d' ' -f1; }
join_by() { local IFS="$1"; shift; echo "$*"; }

# The patches in the order they are applied: NNNN-name.patch applies everywhere, NNNN-name.windows.patch only on
# Windows and NNNN-name.linux.patch only here. Sorted by name; the numbers are unique, so the order is unambiguous.
PATCHES=()
collect_patches() {
  local file name number
  local -A numbers=()
  local -a names=()
  shopt -s nullglob
  for file in "$PATCH_DIR"/*.patch; do
    name="$(basename "$file")"
    [[ "$name" =~ ^([0-9]{4})-[a-z0-9]+(-[a-z0-9]+)*(\.(windows|linux))?\.patch$ ]] ||
      fail "patches/rexglue-sdk/$name is not named NNNN-name[.windows|.linux].patch"
    number="${BASH_REMATCH[1]}"
    [ -z "${numbers[$number]:-}" ] ||
      fail "patches/rexglue-sdk/$name and ${numbers[$number]} share the number $number"
    numbers[$number]="$name"
    case "$name" in
      *.windows.patch) ;;
      *) names+=("$name") ;;
    esac
  done
  shopt -u nullglob
  [ "${#names[@]}" -gt 0 ] || fail "no patches in patches/rexglue-sdk"
  mapfile -t PATCHES < <(printf '%s\n' "${names[@]}" | LC_ALL=C sort)
}
collect_patches

compiler() { "$CXX" --version | sed -n 1p; }
patch_names() { join_by , "${PATCHES[@]}"; }
patch_set_sha() {  # every patch's name and hash, in order
  local name
  for name in "${PATCHES[@]}"; do echo "$name $(sha "$PATCH_DIR/$name")"; done | text_sha
}
inputs_sha() {  # everything the installed files are built from
  {
    echo "sdk_version=$SDK_VERSION"
    echo "sdk_tag=$SDK_TAG"
    echo "sdk_commit=$SDK_COMMIT"
    echo "platform=$PATCH_PLATFORM"
    echo "compiler=$(compiler)"
    echo "build_flags=$BUILD_FLAGS"
    echo "link_rpath=$LINK_RPATH"
    echo "stock_files=$(grep -E '^SHA256_lib/librex' "$PATCH_DIR/baseline.env" | tr -d '\r' | LC_ALL=C sort | text_sha)"
    echo "build_targets=$(join_by , "${BUILD_TARGETS[@]}")"
    echo "build_configs=$(join_by , "${BUILD_CONFIGS[@]}")"
    local name
    for name in "${PATCHES[@]}"; do echo "patch $name $(sha "$PATCH_DIR/$name")"; done
  } | text_sha
}
official_sha() {  # lib: the SHA-256 of the stock release file, lower case
  local hash
  hash="$(baseline "SHA256_$1" | tr 'A-F' 'a-f')"
  [ -n "$hash" ] || fail "patches/rexglue-sdk/baseline.env has no stock hash for $1"
  echo "$hash"
}
lib_config() {  # lib
  case "$1" in
    *rd.so) echo RelWithDebInfo ;;
    *) echo Release ;;
  esac
}
built_lib() { echo "$SRC/out/linux-amd64/$(lib_config "$1")/$(basename "$1")"; }

inputs_change() {  # stamp: which inputs differ from the ones the stamp was written for
  local old current added removed name
  old="$(sed -n 's/^patches=//p' "$1")"
  current="$(patch_names)"
  added=""; removed=""
  for name in ${current//,/ }; do case ",$old," in *",$name,"*) ;; *) added="$added $name" ;; esac; done
  for name in ${old//,/ }; do case ",$current," in *",$name,"*) ;; *) removed="$removed $name" ;; esac; done
  if [ -n "$added" ]; then echo "patches added:$added"
  elif [ -n "$removed" ]; then echo "patches removed or renamed:$removed"
  elif [ "$(sed -n 's/^patch_sha256=//p' "$1")" != "$(patch_set_sha)" ]; then echo "a patch changed"
  elif [ "$(sed -n 's/^compiler=//p' "$1")" != "$(compiler)" ]; then echo "compiler changed: $(compiler)"
  else echo "ReXGlue baseline or build settings changed"; fi
}

check_stamp() {  # SDK_DIR
  local sdk="$1" stamp="$1/$STAMP_NAME" problems=0 lib actual
  command -v "$CXX" >/dev/null || { echo "  $CXX not found" >&2; return 1; }
  [ -f "$stamp" ] || { echo "  $STAMP_NAME is missing" >&2; return 1; }
  [ "$(sed -n 's/^sdk_commit=//p' "$stamp")" = "$SDK_COMMIT" ] ||
    { echo "  libraries were not built from ReXGlue $SDK_TAG ($SDK_COMMIT)" >&2; problems=1; }
  [ "$(sed -n 's/^inputs_sha256=//p' "$stamp")" = "$(inputs_sha)" ] ||
    { echo "  libraries were built from other inputs than the current ones ($(inputs_change "$stamp"))" >&2; problems=1; }
  for lib in "${LIBS[@]}"; do
    grep -q "^[0-9a-f]\{64\}  $lib\$" "$stamp" || { echo "  $lib is not in $STAMP_NAME" >&2; problems=1; continue; }
    [ -f "$sdk/$lib" ] || { echo "  missing: $lib" >&2; problems=1; continue; }
    actual="$(sha "$sdk/$lib")"
    grep -qx "$actual  $lib" "$stamp" || { echo "  $lib does not match the patched build" >&2; problems=1; }
    [ "$actual" != "$(official_sha "$lib")" ] || { echo "  $lib is the stock ReXGlue release file" >&2; problems=1; }
  done
  return "$problems"
}

check_staged() {  # SDK_DIR CONFIG DIR: DIR holds the SDK's runtime and GPU plugin of CONFIG
  local suffix name problems=0
  case "$2" in
    Debug) suffix=d ;;
    RelWithDebInfo) suffix=rd ;;
    Release) suffix= ;;
    *) fail "unknown configuration '$2'" ;;
  esac
  for name in "librexruntime$suffix.so" "librexgpu-xenos$suffix.so"; do
    if [ ! -f "$3/$name" ]; then
      echo "  $name is not next to the program in $3" >&2
      problems=1
    elif ! cmp -s "$1/lib/$name" "$3/$name"; then
      echo "  $name next to the program is not the one in $1" >&2
      problems=1
    fi
  done
  return "$problems"
}

stock_runtime() {  # SDK_DIR OUT_FILE: the stock Release runtime, from the SDK download kept beside SDK_DIR
  local zip
  zip="$(dirname "$1")/rexglue-sdk-$SDK_VERSION-linux-amd64.zip"
  [ -f "$zip" ] || return 1
  if command -v unzip >/dev/null; then
    unzip -p "$zip" linux-amd64/lib/librexruntime.so > "$2"
  else
    python3 -I -c 'import sys, zipfile; sys.stdout.buffer.write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))' \
      "$zip" linux-amd64/lib/librexruntime.so > "$2"
  fi
  [ "$(sha "$2")" = "$(official_sha lib/librexruntime.so)" ]
}

require_release_build() {  # BUILD_DIR TEST: the test must load the Release runtime the stamp records
  readelf -d "$1/$2" | grep -qF '[librexruntime.so]' || fail "$1 is not a Release project build"
}

if [ "${1:-}" = --list ]; then
  printf '%s\n' "${PATCHES[@]}"
  echo "fingerprint $(inputs_sha)"
  exit 0
fi

if [ "${1:-}" = --check ]; then
  SDK="${2:-}"
  [ -n "$SDK" ] || fail "usage: $0 --check SDK_DIR [--config CONFIG] [--staged DIR]"
  shift 2
  CONFIG=Release
  STAGED=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --config) CONFIG="${2:?--config needs a configuration}"; shift ;;
      --staged) STAGED="${2:?--staged needs a folder}"; shift ;;
      *) fail "unexpected argument $1" ;;
    esac
    shift
  done
  case "$CONFIG" in Debug | RelWithDebInfo | Release) ;; *) fail "unknown configuration '$CONFIG'" ;; esac
  problems=0
  if [ "$CONFIG" != Debug ]; then check_stamp "$SDK" || problems=1; fi
  if [ -n "$STAGED" ]; then check_staged "$SDK" "$CONFIG" "$STAGED" || problems=1; fi
  [ "$problems" = 0 ] ||
    fail "$SDK or the build folder does not hold this branch's ReXGlue runtime and GPU plugin ($CONFIG); run tools/build.sh, or tools/build-patched-rexruntime.sh $SDK"
  [ "$CONFIG" = Debug ] || echo "ReXGlue patches: current (${#PATCHES[@]} patches, fingerprint $(inputs_sha | cut -c1-12))"
  exit 0
fi

if [ "${1:-}" = --loader-test ]; then
  [ -n "${2:-}" ] && [ -n "${3:-}" ] || fail "usage: $0 --loader-test SDK_DIR BUILD_DIR"
  SDK="$2"
  BUILD="$3"
  check_stamp "$SDK" || fail "$SDK does not hold the patched ReXGlue runtime and GPU plugin"
  cmake --build "$BUILD" --target crackdown_renderdoc_detect_tests
  require_release_build "$BUILD" crackdown_renderdoc_detect_tests
  stage_loader_test() {  # STAGE_DIR: the test and its fake RenderDoc, without a runtime
    rm -rf "$1"
    mkdir -p "$1/renderdoc"
    cp "$BUILD/crackdown_renderdoc_detect_tests" "$1/"
    cp "$BUILD/renderdoc/librenderdoc.so" "$1/renderdoc/"
  }
  stage="$BUILD/renderdoc_detect"
  stage_loader_test "$stage"
  cp "$SDK/lib/librexruntime.so" "$stage/"
  echo "== Patched runtime (must pass)"
  (cd "$stage" && ./crackdown_renderdoc_detect_tests)
  # The stock runtime must fail by loading the discoverable fake itself, which shows the test detects the bug.
  stock="$BUILD/renderdoc_detect_stock"
  stage_loader_test "$stock"
  stock_runtime "$SDK" "$stock/librexruntime.so" ||
    fail "no stock librexruntime.so for the control run: keep rexglue-sdk-$SDK_VERSION-linux-amd64.zip beside $SDK"
  echo "== Stock runtime (must fail)"
  if output="$(cd "$stock" && ./crackdown_renderdoc_detect_tests 2>&1)"; then
    echo "$output"
    fail "the test passed against the stock runtime, so it does not detect the bug"
  fi
  echo "$output"
  grep -qxE 'FAIL: (absent|not loaded): CreateIfConnected returns nullptr' <<< "$output" ||
    fail "the stock runtime failed the test, but not by loading RenderDoc itself"
  exit 0
fi

if [ "${1:-}" = --stress ]; then
  [ -n "${2:-}" ] && [ -n "${3:-}" ] || fail "usage: $0 --stress SDK_DIR BUILD_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime and GPU plugin"
  cmake --build "$3" --target crackdown_physical_heap_stress
  require_release_build "$3" crackdown_physical_heap_stress
  # Run beside the installed runtime, not the copy the build left in BUILD_DIR, which may be stale.
  stage="$3/physical_heap_stress"
  mkdir -p "$stage"
  cp "$3/crackdown_physical_heap_stress" "$2/lib/librexruntime.so" "$stage/"
  (cd "$stage" && ./crackdown_physical_heap_stress | grep -E '^(view|PASS|FAIL)')
  exit 0
fi

SDK="${1:-}"
[ -n "$SDK" ] && [ -d "$SDK/lib/cmake/rexglue" ] ||
  fail "usage: $0 SDK_DIR [WORK_DIR] (SDK_DIR is the extracted linux-amd64 SDK)"
SRC="${2:-$ROOT/out/rexglue-sdk-src}"

for tool in git "$CC" "$CXX" cmake ninja sha256sum readelf nm; do
  command -v "$tool" >/dev/null || fail "$tool not found"
done

if [ ! -d "$SRC/.git" ]; then
  echo "== Fetching ReXGlue $SDK_TAG from $SDK_REPO (network) into $SRC"
  mkdir -p "$(dirname "$SRC")"
  git clone --branch "$SDK_TAG" --depth 1 --recurse-submodules --shallow-submodules --jobs 8 "$SDK_REPO" "$SRC"
fi
COMMIT="$(git -C "$SRC" rev-parse HEAD)"
[ "$COMMIT" = "$SDK_COMMIT" ] || fail "$SRC is at $COMMIT; expected $SDK_TAG $SDK_COMMIT (delete the folder to fetch it again)"

echo "== Patch ($(patch_names))"
# Another branch may have left other patches in the tree, in any tracked file.
git -C "$SRC" checkout -- .
git -C "$SRC" clean -fdq -- src include tests cmake
for name in "${PATCHES[@]}"; do
  git -C "$SRC" apply --check "$PATCH_DIR/$name" || fail "$name no longer applies to ReXGlue $SDK_TAG"
  git -C "$SRC" apply "$PATCH_DIR/$name"
done

echo "== Build ${BUILD_TARGETS[*]} (${BUILD_CONFIGS[*]})"
# -ffile-prefix-map keeps the build folder out of the libraries' log strings; the RUNPATH is the stock libraries'
# instead of the build folder and the empty entries CMake would add.
FLAGS="$BUILD_FLAGS -ffile-prefix-map=$SRC=."
(
  cd "$SRC"
  cmake --preset linux-amd64 "-DCMAKE_C_COMPILER=$CC" "-DCMAKE_CXX_COMPILER=$CXX" \
    "-DCMAKE_C_FLAGS=$FLAGS" "-DCMAKE_CXX_FLAGS=$FLAGS" -DCMAKE_SKIP_RPATH=ON \
    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-rpath,'$LINK_RPATH'" >/dev/null
  for config in "${BUILD_CONFIGS[@]}"; do
    cmake --build out/build/linux-amd64 --config "$config" --target "${BUILD_TARGETS[@]}" --parallel
  done
)
for lib in "${LIBS[@]}"; do
  BUILT="$(built_lib "$lib")"
  [ -f "$BUILT" ] || fail "ReXGlue build output is missing $BUILT"
  [ "$(sha "$BUILT")" != "$(official_sha "$lib")" ] || fail "$(basename "$lib") came out identical to the stock release; the patches were not built in"
  # The RelWithDebInfo debug info names the source folder; only the Release files are packaged.
  if [ "$(lib_config "$lib")" = Release ]; then
    for path in "$SRC" "$SDK" "$ROOT"; do
      ! grep -qaF "$path" "$BUILT" || fail "$(basename "$lib") embeds a local folder ($path)"
    done
  fi
  DYNAMIC="$(readelf -d "$BUILT" 2>/dev/null)"
  grep -qF "[$LINK_RPATH]" <<< "$DYNAMIC" || fail "$(basename "$lib") does not have the RUNPATH $LINK_RPATH"
  case "$lib" in
    lib/librexgpu-xenos*)
      grep -qE '\(NEEDED\).*\[librexruntime(rd)?\.so\]' <<< "$DYNAMIC" || fail "$(basename "$lib") does not link the ReXGlue runtime"
      EXPORTS="$(nm -D --defined-only "$BUILT" | awk '{print $3}')"
      for symbol in "${PLUGIN_ENTRY_POINTS[@]}"; do
        grep -qx "$symbol" <<< "$EXPORTS" || fail "$(basename "$lib") does not export $symbol"
      done
      ;;
  esac
done

rm -f "$SDK/$STAMP_NAME"
for lib in "${LIBS[@]}"; do cp "$(built_lib "$lib")" "$SDK/$lib"; done
{
  echo "sdk_version=$SDK_VERSION"
  echo "sdk_commit=$SDK_COMMIT"
  echo "inputs_sha256=$(inputs_sha)"
  echo "compiler=$(compiler)"
  echo "patch_sha256=$(patch_set_sha)"
  echo "patches=$(patch_names)"
  for lib in "${LIBS[@]}"; do echo "$(sha "$SDK/$lib")  $lib"; done
} > "$SDK/$STAMP_NAME"
check_stamp "$SDK" || fail "the installed libraries failed their own check"
echo "Patched ReXGlue runtime and GPU plugin installed:"
cat "$SDK/$STAMP_NAME"
