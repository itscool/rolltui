#!/bin/sh
# fetch_ucd.sh — download the pinned Unicode Character Database files that
# tools/gen_unicode_tables.cpp reads and rolltui/tests/ replay, into rolltui/ucd/.
#
# The files are checked in (they are the generator's inputs AND the conformance
# suites, so a clone must be able to regenerate and re-verify offline); this script
# is how they got there and how a Unicode version bump replaces them. Every file is
# verified against the sha256 recorded below before it is accepted — a partially
# downloaded LineBreak.txt would generate a header that is silently wrong for every
# code point past the truncation, which is exactly the class of failure this project
# never tolerates.
#
# Bumping Unicode: change UNICODE_VERSION, run with --refresh-hashes to print the new
# digests, paste them below, re-run, then `gen_unicode_tables rolltui/ucd` and re-run
# ctest — the two conformance suites are the verification that the new tables and the
# unchanged algorithm still agree with Unicode.
set -eu

UNICODE_VERSION=17.0.0
BASE="https://www.unicode.org/Public/${UNICODE_VERSION}/ucd"
HERE=$(cd "$(dirname "$0")/.." && pwd)
DEST="${HERE}/ucd"

# path-under-ucd  sha256
#
# Line_Break and East_Asian_Width are taken from the extracted/Derived* files, not
# LineBreak.txt / EastAsianWidth.txt: the derived files state the defaults for
# unassigned code points as machine-readable `@missing` lines, where the plain files
# only describe them in header prose — and the prose is imprecise (LineBreak.txt says
# "U+1F000..U+1FAFF default to ID"; the derived file says 1F000..1F7FF and
# 1F900..1FAFF, and the conformance suite agrees with the derived file; found
# 2026-09-01 by 71 failing LineBreakTest cases on U+1F8FF).
FILES='
extracted/DerivedLineBreak.txt      dad3ef492d198d6f1dde4922b175f7371a27dfe62fce489f3e04807015a4c682
extracted/DerivedEastAsianWidth.txt 0b5523a2217cb318d20b329a05d31eec5af5686ba09d263b85bb75a28989a3a8
DerivedCoreProperties.txt           24c7fed1195c482faaefd5c1e7eb821c5ee1fb6de07ecdbaa64b56a99da22c08
extracted/DerivedGeneralCategory.txt d62e5bab70ca74f099343f71224fa051cb1fdd61a1ab45c0488c44cfc0b6102e
auxiliary/GraphemeBreakProperty.txt d6b51d1d2ae5c33b451b7ed994b48f1f4dc62b2272a5831e7fd418514a6bae89
auxiliary/GraphemeBreakTest.txt     e2d134d2c52919bace503ebb6a551c1855fe1a1faec18478c78fff254a1793ec
auxiliary/LineBreakTest.txt         e69884e0dde6a8724873f885d68c52dc14518abf9ae4ca9e2283b8773db3b752
emoji/emoji-data.txt                2cb2bb9455cda83e8481541ecf5b6dfda66a3bb89efa3fa7c5297eccf607b72b
auxiliary/WordBreakProperty.txt     72274cac1e6b919507db35655c3e175aa27274668a1ece95c28d2069f2ad9852
auxiliary/WordBreakTest.txt         1de23a75f37904abc7d206239ee8d34f8fdf0fb4ab32a7174dfbabbde25419b2
'

refresh=0
[ "${1:-}" = "--refresh-hashes" ] && refresh=1

mkdir -p "$DEST"
status=0
echo "$FILES" | while read -r path sha; do
  [ -n "$path" ] || continue
  name=$(basename "$path")
  tmp="${DEST}/${name}.download"
  curl -sSf -m 120 -o "$tmp" "${BASE}/${path}"
  got=$(shasum -a 256 "$tmp" | cut -d' ' -f1)
  if [ "$refresh" = 1 ]; then
    printf '%-36s %s\n' "$path" "$got"
    mv "$tmp" "${DEST}/${name}"
    continue
  fi
  if [ "$got" != "$sha" ]; then
    echo "sha256 MISMATCH for ${path}: expected ${sha}, got ${got}" >&2
    rm -f "$tmp"
    exit 1
  fi
  mv "$tmp" "${DEST}/${name}"
  echo "ok  ${name}"
done || status=$?
exit $status
