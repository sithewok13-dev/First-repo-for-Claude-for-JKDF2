#!/usr/bin/env bash
# Builds the emulator cores used by the arcade as WebAssembly modules.
#
# Each core is compiled from pinned upstream sources with Emscripten and linked
# against our deterministic frontend (frontend/fe.cpp). The same .js/.wasm pair
# is loaded by the server's emulation worker (Node) and by browsers.
#
# Usage: native/build-cores.sh [core...]      (default: all)
# Env:   SRC_DIR   where upstream sources are cloned  (default: native/.src)
#        OUT_DIR   output directory                    (default: native/build)
#        EMSDK     path to an activated emsdk           (default: emcc on PATH)
#        JOBS      parallel compile jobs                (default: nproc)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${SRC_DIR:-$HERE/.src}"
OUT_DIR="${OUT_DIR:-$HERE/build}"
JOBS="${JOBS:-$(nproc)}"

# Pinned upstream revisions (update deliberately: a new core build is a new
# compatibility target for checkpoints and records).
FBNEO_REPO=https://github.com/libretro/FBNeo
FBNEO_REV=7a276b6bb3d52f9211ee4d9dbe4b6801474c4a84
FCEUMM_REPO=https://github.com/libretro/libretro-fceumm
FCEUMM_REV=7a542dab1e87679921962a9f056186eca425c0c2

if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
  # shellcheck disable=SC1091
  source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
fi
command -v emcc >/dev/null || { echo "emcc not found; install emsdk and set EMSDK" >&2; exit 1; }

mkdir -p "$SRC_DIR" "$OUT_DIR"

fetch() { # name repo rev
  local dir="$SRC_DIR/$1"
  if [ ! -d "$dir/.git" ]; then
    git init -q "$dir"
    git -C "$dir" remote add origin "$2"
  fi
  if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$3" ]; then
    git -C "$dir" fetch -q --depth 1 origin "$3"
    git -C "$dir" checkout -q --force FETCH_HEAD
  fi
}

COMMON_LDFLAGS=(
  -O3
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createCore
  -sENVIRONMENT=web,worker,node
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sMAXIMUM_MEMORY=1073741824
  -sSTACK_SIZE=4194304
  -sFORCE_FILESYSTEM=1
  -sEXIT_RUNTIME=0
  -sINVOKE_RUN=0
  -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,HEAP16,HEAPU32,UTF8ToString,stringToNewUTF8
  -sEXPORTED_FUNCTIONS=_malloc,_free
)

# link_core NAME ARCHIVE [cpp-sources...] [-- LRC_ROOT c-sources...]
# C sources after "--" are libretro-common files that RetroArch normally
# provides to statically linked cores; they are compiled against LRC_ROOT.
link_core() {
  local name=$1 archive=$2; shift 2
  local obj="$OUT_DIR/obj/$name"
  mkdir -p "$obj"
  echo "== linking $name"
  # Libretro makefiles name the emscripten static archive *.bc; give it an
  # archive suffix so the driver does not try to compile it as source.
  cp "$archive" "$obj/libcore.a"
  local objs=() cpp=("$HERE/frontend/fe.cpp")
  while [ $# -gt 0 ] && [ "$1" != "--" ]; do cpp+=("$1"); shift; done
  for src in "${cpp[@]}"; do
    local o="$obj/$(basename "${src%.*}").o"
    em++ -O3 -std=c++17 -I"$HERE/frontend" -c "$src" -o "$o"
    objs+=("$o")
  done
  if [ "${1:-}" = "--" ]; then
    shift
    local lrc=$1; shift
    for src in "$@"; do
      local o="$obj/lrc_$(echo "$src" | tr '/' '_' | sed 's/\.c$//').o"
      emcc -O2 -D__LIBRETRO__ -I"$lrc/include" -c "$lrc/$src" -o "$o"
      objs+=("$o")
    done
  fi
  em++ "${objs[@]}" "$obj/libcore.a" "${COMMON_LDFLAGS[@]}" -o "$OUT_DIR/$name.mjs"
  ( cd "$OUT_DIR" && sha256sum "$name.wasm" > "$name.wasm.sha256" )
}

# libretro-common pieces FBNeo expects from the frontend when statically linked
# (mirrors the "ifneq ($(STATIC_LINKING), 1)" list in FBNeo's Makefile.common).
FBNEO_LRC=(
  file/file_path.c file/file_path_io.c file/retro_dirent.c
  encodings/encoding_utf.c compat/compat_posix_string.c compat/compat_strcasestr.c
  compat/compat_strl.c compat/compat_strldup.c compat/fopen_utf8.c string/stdstring.c
  streams/file_stream.c streams/file_stream_transforms.c features/features_cpu.c
  file/config_file.c file/config_file_userdata.c lists/string_list.c memmap/memalign.c
  time/rtime.c vfs/vfs_implementation.c
)

# Same for FCEUmm (its Makefile.common STATIC_LINKING guard).
FCEUMM_LRC=(
  compat/compat_posix_string.c compat/compat_snprintf.c compat/compat_strcasestr.c
  compat/compat_strl.c compat/fopen_utf8.c encodings/encoding_utf.c file/file_path.c
  file/file_path_io.c streams/file_stream.c streams/file_stream_transforms.c
  string/stdstring.c time/rtime.c vfs/vfs_implementation.c
)

build_fbneo_subset() { # subset
  local subset=$1
  local work="$SRC_DIR/fbneo-$subset"
  if [ ! -d "$work" ]; then cp -r "$SRC_DIR/fbneo" "$work"; fi
  # Our patches (see native/patches/README.md): savestates complete enough to
  # move a running game into a fresh instance (late join, resync, recovery).
  for patch in "$HERE"/patches/fbneo-*.patch; do
    local mark="$work/.applied-$(basename "$patch" .patch)-$(sha256sum "$patch" | cut -c1-12)"
    if [ ! -f "$mark" ]; then
      git -C "$work" checkout -q -- src
      git -C "$work" apply "$patch"
      rm -f "$work"/.applied-* "$work/.subset-headers"   # sources reset: regenerate headers too
      touch "$mark"
    fi
  done
  echo "== building FBNeo subset $subset"
  # REGEN_HEADERS regenerates driverlist.h for just this subset's drivers (it
  # runs small generator tools with the host gcc/g++ and perl).
  if [ ! -f "$work/.subset-headers" ]; then
    ( cd "$work/src/burner/libretro" && make platform=emscripten SUBSET="$subset" REGEN_HEADERS=1 generate-files >/dev/null )
    rm -f "$work"/src/burn/burn.o "$work"/src/burn/burn.d
    touch "$work/.subset-headers"
  fi
  ( cd "$work/src/burner/libretro" && emmake make -j"$JOBS" platform=emscripten SUBSET="$subset" EXTERNAL_ZLIB=0 >/dev/null 2>&1 )
  link_core "fbneo_$subset" "$work/src/burner/libretro/fbneo_${subset}_libretro_emscripten.bc" "$HERE/frontend/shim_fbneo.cpp" \
    -- "$work/src/burner/libretro/libretro-common" "${FBNEO_LRC[@]}"
}

build_fceumm() {
  fetch fceumm "$FCEUMM_REPO" "$FCEUMM_REV"
  echo "== building FCEUmm"
  ( cd "$SRC_DIR/fceumm" && emmake make -f Makefile.libretro -j"$JOBS" platform=emscripten >/dev/null )
  link_core fceumm "$SRC_DIR/fceumm/fceumm_libretro_emscripten.bc" \
    -- "$SRC_DIR/fceumm/src/drivers/libretro/libretro-common" "${FCEUMM_LRC[@]}"
}

CORES=("$@")
[ ${#CORES[@]} -eq 0 ] && CORES=(fbneo_cps12 fbneo_neogeo fceumm)

for c in "${CORES[@]}"; do
  case "$c" in
    fbneo_*) fetch fbneo "$FBNEO_REPO" "$FBNEO_REV"; build_fbneo_subset "${c#fbneo_}"
             cp "$SRC_DIR/fbneo/src/license.txt" "$OUT_DIR/LICENSE-FBNeo.txt" ;;
    fceumm) build_fceumm
            cp "$SRC_DIR/fceumm/Copying" "$OUT_DIR/LICENSE-FCEUmm.txt" ;;
    *) echo "unknown core $c" >&2; exit 1 ;;
  esac
done

# Record exactly what was built so checkpoints and records can be tied to it.
{
  echo "{"
  echo "  \"emcc\": \"$(emcc --version | head -1 | sed 's/"/\\"/g')\","
  echo "  \"fbneo\": \"$FBNEO_REV\","
  echo "  \"fceumm\": \"$FCEUMM_REV\","
  echo "  \"frontend\": \"$(cat "$HERE/frontend/fe.cpp" "$HERE/frontend/shim_fbneo.cpp" | sha256sum | cut -c1-16)\""
  echo "}"
} > "$OUT_DIR/build-info.json"
cp "$(dirname "$(command -v emcc)")/LICENSE" "$OUT_DIR/LICENSE-Emscripten.txt" 2>/dev/null || true

# Corresponding source for the shipped binaries (both licenses require the
# source of any modification to be available): upstream revisions + our patches.
{
  echo "These WebAssembly cores were built from:"
  echo "  FBNeo   $FBNEO_REPO @ $FBNEO_REV"
  echo "  FCEUmm  $FCEUMM_REPO @ $FCEUMM_REV"
  echo "with the patches in native/patches/ and the frontend in native/frontend/"
  echo "of this repository (native/build-cores.sh reproduces the build)."
  echo "FBNeo is distributed under its own non-commercial license (LICENSE-FBNeo.txt);"
  echo "FCEUmm under the GNU GPL v2 (LICENSE-FCEUmm.txt); the Emscripten runtime"
  echo "under MIT / University of Illinois NCSA (LICENSE-Emscripten.txt)."
} > "$OUT_DIR/SOURCES.txt"
echo "done: $OUT_DIR"
