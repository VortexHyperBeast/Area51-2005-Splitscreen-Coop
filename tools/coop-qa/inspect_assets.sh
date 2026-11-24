#!/usr/bin/env bash
# Finds the newest "assets" release of the upstream port, downloads it and writes text reports
# about what is in it (file names, sizes, and the text of any guide or readme).  Nothing from the
# pack itself is published, only these reports.
#
# usage: inspect_assets.sh <report-dir> <work-dir>
# env:   GH_TOKEN (GitHub Actions token; public data only)
set -uo pipefail

OUT="${1:?report dir}"
WORK="${2:?work dir}"
UP="${UPSTREAM_REPO:-Source2Spy/area51-pc}"
mkdir -p "$OUT" "$WORK"

say() { echo "$@" | tee -a "$OUT/inspect.txt"; }

say "upstream: $UP"
gh release list --repo "$UP" --limit 100 > "$OUT/releases.txt" 2>&1
say "releases found: $(wc -l < "$OUT/releases.txt")"
head -40 "$OUT/releases.txt"

TAG="$(gh release list --repo "$UP" --limit 100 --json tagName,publishedAt \
        --jq '[.[] | select(.tagName | test("asset"; "i"))] | sort_by(.publishedAt) | reverse | .[0].tagName' 2>/dev/null)"
if [ -z "$TAG" ] || [ "$TAG" = "null" ]; then
  say "no release with 'asset' in its tag; nothing more to inspect"
  exit 0
fi
say "chosen release: $TAG"

gh release view "$TAG" --repo "$UP" > "$OUT/release-body.txt" 2>&1
gh release view "$TAG" --repo "$UP" --json assets --jq '.assets[] | "\(.size)\t\(.name)"' > "$OUT/assets.tsv" 2>&1
say "assets (bytes, name):"
cat "$OUT/assets.tsv"

TOTAL="$(awk -F'\t' '{s+=$1} END {printf "%.0f", s}' "$OUT/assets.tsv")"
say "total bytes: $TOTAL"
LIMIT=$((24 * 1024 * 1024 * 1024))
if [ "${TOTAL:-0}" -gt "$LIMIT" ]; then
  say "pack is larger than 24 GB; not downloading"
  exit 0
fi

df -h "$WORK" | tee -a "$OUT/inspect.txt"
gh release download "$TAG" --repo "$UP" --dir "$WORK/dl" --clobber > "$OUT/download.txt" 2>&1
say "download exit: $?"
ls -la "$WORK/dl" 2>&1 | tee -a "$OUT/inspect.txt"

command -v 7z >/dev/null 2>&1 || sudo apt-get install -y p7zip-full >/dev/null 2>&1

: > "$OUT/archive-listing.txt"
for f in "$WORK"/dl/*; do
  [ -f "$f" ] || continue
  {
    echo "=== $(basename "$f")  $(stat -c %s "$f") bytes"
    case "$f" in
      *.zip) unzip -l "$f" 2>&1 | head -300 ;;
      *)     7z l "$f" 2>&1 | head -300 ;;
    esac
  } >> "$OUT/archive-listing.txt"
done
say "archive listing lines: $(wc -l < "$OUT/archive-listing.txt")"

mkdir -p "$WORK/data"
for f in "$WORK"/dl/*; do
  [ -f "$f" ] || continue
  case "$f" in
    *.zip|*.7z|*.rar|*.001|*.tar|*.tar.*|*.tgz) 7z x -y -o"$WORK/data" "$f" > "$OUT/extract-$(basename "$f").txt" 2>&1; say "extract $(basename "$f"): exit $?" ;;
    *) say "not an archive, left as is: $(basename "$f")" ;;
  esac
done

df -h "$WORK" | tee -a "$OUT/inspect.txt"
( cd "$WORK/data" && find . -maxdepth 4 | sort | head -3000 ) > "$OUT/tree.txt" 2>&1
( cd "$WORK/data" && du -h --max-depth=3 . | sort -h | tail -150 ) > "$OUT/sizes.txt" 2>&1
( cd "$WORK/data" && find . -type f | sed -E 's/.*\.([^./]+)$/\1/' | sort | uniq -c | sort -rn | head -60 ) > "$OUT/extensions.txt" 2>&1

: > "$OUT/guides.txt"
( cd "$WORK/data" && find . -type f \( -iname '*guide*' -o -iname 'readme*' -o -iname '*.md' -o -iname 'install*' -o -iname '*instruction*' -o -iname 'config.ini' -o -iname '*.txt' \) -size -64k | sort | head -40 ) | while read -r g; do
  {
    echo "=== $g"
    head -c 6000 "$WORK/data/$g" | tr -d '\r'
    echo
  } >> "$OUT/guides.txt"
done
say "guide-like files: $(grep -c '^=== ' "$OUT/guides.txt")"

# Which campaign level holds which kind of enemy (text counts only): the type names the levels'
# files mention, e.g. "NPC - Mutant_Tank" for the Theta boss.  Helps to send the owner to the
# right level for a test.
: > "$OUT/level-enemies.txt"
( cd "$WORK/data" && find . -maxdepth 3 -type d -ipath '*levels*' | sort ) | while read -r d; do
  n=$(find "$WORK/data/$d" -maxdepth 1 -type f | wc -l)
  [ "$n" -gt 0 ] || continue
  line="$d:"
  for t in "Mutant_Tank" "NPC - Gray" "NPC - BlackOpps" "NPC - Grunt" "NPC - Hazmat" "NPC - GENERIC" "Alien Glob" "Turret"; do
    c=$(cat "$WORK/data/$d"/* 2>/dev/null | grep -a -o -i -- "$t" | wc -l)
    line="$line  $t=$c"
  done
  echo "$line" >> "$OUT/level-enemies.txt"
done
say "level enemy scan: $(wc -l < "$OUT/level-enemies.txt") level folders"
