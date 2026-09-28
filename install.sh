#!/usr/bin/env bash
# Put your ROM zips in roms/, then run this. That is the whole installation.
#   ./install.sh --help   for the options
cd "$(dirname "$0")"
exec ./pelletino install "$@"
