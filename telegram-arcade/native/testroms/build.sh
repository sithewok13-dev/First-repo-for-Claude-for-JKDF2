#!/usr/bin/env bash
# Builds the ATC test ROMs (needs cc65's ca65/ld65 and python3).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
python3 font.py > build/font.chr
for m in versus:0 coop:1 turns:2 solo:3; do
  name=${m%%:*}; val=${m##*:}
  ca65 -D MODE_VAL=$val atc.s -o build/atc-$name.o
  ld65 -C nrom.cfg build/atc-$name.o -o build/atc-$name.nes
  rm build/atc-$name.o
done
sha256sum build/*.nes
