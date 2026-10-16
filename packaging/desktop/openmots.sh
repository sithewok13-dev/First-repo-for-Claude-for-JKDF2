#!/bin/sh
# Mysteries of the Sith: starts the game from this folder, as MoTS
cd "$(dirname "$0")" && exec ./openjkdf2 -motsCompat "$@"
