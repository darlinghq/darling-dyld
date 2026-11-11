#!/bin/sh
# Builds and runs dyld's host-side unit tests with the host C compiler; no Darling build needed.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root="$here/../.."
cc=${CC:-cc}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$cc" -O1 -Wall -Wextra -pthread -I "$root/src" "$here/dylib-arena.c" -o "$tmp/dylib-arena"
"$tmp/dylib-arena"
