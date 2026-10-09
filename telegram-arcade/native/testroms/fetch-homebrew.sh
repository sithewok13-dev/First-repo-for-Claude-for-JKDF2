#!/usr/bin/env bash
# Builds lawful, openly licensed NES homebrew used for compatibility tests
# from pinned upstream sources (nothing is redistributed by this repository).
# Needs: git, cc65 (ca65/ld65), make, python3 + Pillow, dpkg-deb, apt-get.
# Output: native/testroms/homebrew/*.nes  (consumed by test/unit/homebrew-compat.test.ts)
#
#   RHDE: Furniture Fight  GNU All-Permissive   pinobatch/rhde-nes
#   Thwaite                GPL-3.0-or-later     pinobatch/thwaite-nes
#   Concentration Room     GPL-3.0-or-later     pinobatch/croom-nes
#   Squirrel Domino        zlib (code)          NovaSquirrel/SquirrelDomino
#   allpads                zlib                 pinobatch/allpads-nes
#   Escape from Pong       BSD-3-Clause         Ubuntu package "efp"
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$HERE/.homebrew-src}"
OUT="$HERE/homebrew"
mkdir -p "$WORK" "$OUT"

get() { # dir url rev
  if [ ! -d "$WORK/$1/.git" ]; then git clone -q "$2" "$WORK/$1"; fi
  git -C "$WORK/$1" checkout -q "$3"
}

get rhde-nes https://github.com/pinobatch/rhde-nes f3330e30b9e468a9d44ef2c3c18e5647ae57c765
# donut.py asks for the terminal size and fails without a TTY
( cd "$WORK/rhde-nes" && script -qec "make all" /dev/null >/dev/null ) && cp "$WORK"/rhde-nes/*.nes "$OUT/rhde.nes"

get thwaite-nes https://github.com/pinobatch/thwaite-nes 00e36745188bc165990f60eed6b093c3ce6ad0e3
( cd "$WORK/thwaite-nes" && make -s all >/dev/null ) && cp "$WORK"/thwaite-nes/thwaite.nes "$OUT/thwaite.nes"

get croom-nes https://github.com/pinobatch/croom-nes ed19c3c07ca389b70cf2e0dd2ce0320df28d511d
( cd "$WORK/croom-nes" && make -s all >/dev/null ) && cp "$WORK"/croom-nes/croom.nes "$OUT/croom.nes"   # embeds build time (not byte-reproducible)

get SquirrelDomino https://github.com/NovaSquirrel/SquirrelDomino 96557700ffe3009f7800da3e76abd95af375bb74
( cd "$WORK/SquirrelDomino" && ca65 squirrel_domino.s -o squirrel_domino.o && ld65 -C nrom128.x squirrel_domino.o -o squirrel_domino.nes ) && cp "$WORK/SquirrelDomino/squirrel_domino.nes" "$OUT/"

get allpads-nes https://github.com/pinobatch/allpads-nes e70cc84369b326300e4dc11077597565dd715e6a
( cd "$WORK/allpads-nes" && make -s allpads.nes >/dev/null ) && cp "$WORK/allpads-nes/allpads.nes" "$OUT/"

if [ ! -f "$WORK/efp/efp.nes" ]; then
  mkdir -p "$WORK/efp" && ( cd "$WORK/efp" && apt-get download efp >/dev/null 2>&1 && dpkg-deb -x efp_*.deb x )
  find "$WORK/efp/x" -name '*.nes' -exec cp {} "$WORK/efp/efp.nes" \;
fi
cp "$WORK/efp/efp.nes" "$OUT/efp.nes"

( cd "$OUT" && sha256sum *.nes )
