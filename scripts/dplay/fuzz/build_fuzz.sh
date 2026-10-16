#!/bin/bash
# Builds the DirectPlay readers' fuzz harness with AddressSanitizer and UBSan:
#   scripts/dplay/fuzz/build_fuzz.sh [out]    then  out 1 .. out 7   (6: two million random messages)
# (CC=clang works too where its sanitizer runtimes are installed.)
cd "$(dirname "$0")/../../.." || exit 1
OUT=${1:-dplay_fuzz}
${CC:-gcc} -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fshort-wchar -std=gnu11 -w \
    -Isrc -DPLATFORM_DPLAY -DPLATFORM_ENET -DLINUX -DARCH_64BIT -DQOL_IMPROVEMENTS -DSDL2_RENDER -DSTDSOUND_NULL \
    scripts/dplay/fuzz/dplay_fuzz.c scripts/dplay/fuzz/dplay_fuzz_main.c -o "$OUT"
