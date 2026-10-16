#!/bin/bash
# Fails if a desktop build's folder (or its .zip / .tar.gz) holds anything that
# could be game data: the game's archives and levels (.gob .goo .jkl .cd), its
# videos and music packs (.smk .san), any zip inside, or any .exe other than
# the program itself (openjkdf2*.exe). Game files are never shipped.
#
# usage: check_no_game_data.sh <folder | file.zip | file.tar.gz>...
set -u
bad=0
check_list() { # $1: where; stdin: one path per line
    local hits
    hits=$(grep -iE '\.(gob|goo|jkl|cd|smk|san|zip)$|(^|/)(episode|music)/' \
           | grep -viE '\.exe$' || true)
    exes=$(grep -iE '\.exe$' <<<"$LIST" | grep -viE '(^|/)openjkdf2[^/]*\.exe$' || true)
    if [ -n "$hits$exes" ]; then
        echo "::error::game data (or a stray file) in $1:"
        printf '%s\n' $hits $exes | head -20
        bad=1
    fi
}
for a in "$@"; do
    if [ -d "$a" ]; then LIST=$(cd "$a" && find . -mindepth 1)
    elif [[ "$a" == *.zip ]]; then LIST=$(unzip -Z1 "$a")
    elif [[ "$a" == *.tar.gz || "$a" == *.tgz ]]; then LIST=$(tar -tzf "$a")
    else echo "::error::not a folder, .zip or .tar.gz: $a"; bad=1; continue; fi
    check_list "$a" <<<"$LIST"
    echo "checked $a: $(wc -l <<<"$LIST") entries"
done
[ $bad = 0 ] && echo "no game data found"
exit $bad
