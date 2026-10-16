#!/bin/sh
# Jedi Knight: starts the game from this folder (where it looks for the game files)
cd "$(dirname "$0")" && exec ./openjkdf2 "$@"
