#!/bin/sh
# The codecs' sources (README's list) built with IM_PREFIX: every external
# symbol they define must carry it (src/prefix.h, include/im/prefix.h
# missing one: it's named here). Usage: test/check-prefix.sh [cc]
set -eu
cd "$(dirname "$0")/.."
CC=${1:-cc}
out=$(mktemp -d "${TMPDIR:-/tmp}/im-prefix.XXXXXX")
trap 'rm -rf "$out"' EXIT
thread=src/posix/thread.c
for f in src/load.c src/io/jpg/jpg.c src/io/jpg/arith.c src/io/jpg/huff.c src/io/jpg/idct.c \
         src/io/jpg/sample.c src/io/jpg/jls.c src/io/jp2/jp2.c src/io/ccitt/ccitt.c src/io/jbig2/jbig2.c src/thread/pool.c $thread; do
  $CC -std=gnu11 -O1 -DIM_CODECS_ONLY=1 -DIM_STATIC -DIM_PREFIX=chk__ -Iinclude -Isrc -c "$f" -o "$out/$(basename "$f" .c).o"
done
bare=$(nm -g "$out"/*.o | awk 'NF == 3 && $2 != "U" { print $3 }' | sed 's/^_//' | grep -v '^chk__' || true)
if [ -n "$bare" ]; then
  echo "not prefixed:"
  echo "$bare"
  exit 1
fi
echo "every symbol prefixed"
