#!/usr/bin/env bash
# Gives every co-op test build (draft release coop-stage1-run<N>) the owner's naming:
#   title  "AREA 51 SPLITSCREEN CO-OP v<version> (test build <N>)"
#   notes  start with "AREA 51 SPLITSCREEN CO-OP", then what changed since the previous
#          successful build (commit subjects), then the downloads and the automated checks
#   files  AREA51_SplitscreenCoop_v<version>.zip            (NexusMods drop-in)
#          AREA51_SplitscreenCoop_v<version>_Standalone.zip (cloud play build with launcher)
#          AREA51_SplitscreenCoop_v<version>.pdb            (debug symbols)
# Run by the release job of coop-build.yml (needs GH_TOKEN, GITHUB_REPOSITORY, full git history
# of coop/stage1 and the branch qa-results fetched as origin/qa-results).
set -euo pipefail
R="${GITHUB_REPOSITORY:?}"

# Which co-op line this is.  Defaults: the 2-player line (coop/stage1).  The 4-player line
# (coop/4player) sets its own values, so each line only ever touches its own releases.
# This line's test builds, by title (GitHub gives drafts no tag name): the title set when the
# build creates it, or the one this script gives it.  Must not match the other line's titles.
TITLE_REGEX="${TITLE_REGEX:-^(AREA 51 SPLITSCREEN CO-OP v[0-9.]+ \(test build [0-9]+\)|Co-op Stage 1, build [0-9]+)}"
LINE_TITLE="${LINE_TITLE:-AREA 51 SPLITSCREEN CO-OP}"   # release title / notes heading
LINE_SHOWN="${LINE_SHOWN:-AREA 51 Splitscreen Co-op}"   # asset names shown on the page
FILE_BASE="${FILE_BASE:-AREA51_SplitscreenCoop}"        # file names: <base>_v<version>...
OFFICIAL_PREFIX="${OFFICIAL_PREFIX:-AREA51_SplitscreenCoop_v}"  # official release tags
RESULTS_ROOT="${RESULTS_ROOT:-runs}"                    # folder on qa-results
WORKFLOW_FILE="${WORKFLOW_FILE:-coop-build.yml}"
LINE_BRANCH="${LINE_BRANCH:-coop/stage1}"

version_of() {  # build number -> version shown in the menu
  if [ "$1" -le 95 ]; then printf '0.%02d' "$1"; else printf '0.95.%d' "$(( $1 - 95 ))"; fi
}

result_line() {  # $1 = qa-results path, $2 = line prefix
  git show "origin/qa-results:$1" 2>/dev/null | tr -d '\r' | grep -m1 "^$2" | sed "s/^$2//" || true
}

# Successful builds of coop/stage1, newest first: "<run_number> <head_sha>"
gh api "repos/$R/actions/workflows/${WORKFLOW_FILE}/runs?branch=${LINE_BRANCH}&status=success&per_page=100" \
  --jq '.workflow_runs[] | "\(.run_number) \(.head_sha)"' > /tmp/builds.txt || true

# This line's test builds: "<id>\t<commit>\t<title>".
list_drafts() {
  gh api "repos/$R/releases?per_page=100" \
    --jq '.[] | select(.draft) | "\(.id)\t\(.target_commitish)\t\(.name)"' | while IFS=$'\t' read -r ID SHA TITLE; do
      printf '%s' "$TITLE" | grep -qE "$TITLE_REGEX" || continue
      N="$(printf '%s' "$TITLE" | grep -oE 'build [0-9]+' | head -1 | grep -oE '[0-9]+')"
      [ -n "$N" ] && printf '%s\t%s\t%s\n' "$ID" "$SHA" "$N"
    done
}
list_drafts > /tmp/rels.txt

while IFS=$'\t' read -r ID SHA N; do
  VER="$(version_of "$N")"
  BASE="${FILE_BASE}_v${VER}"

  # What changed: commits since the previous successful build.
  PREV="$(awk -v n="$N" '$1 < n { print; exit }' /tmp/builds.txt)"
  PN="${PREV%% *}"; PSHA="${PREV#* }"
  CHANGES=""
  if [ -n "$PREV" ] && git cat-file -e "${PSHA}^{commit}" 2>/dev/null && git cat-file -e "${SHA}^{commit}" 2>/dev/null; then
    CHANGES="$(git log --no-merges --format='- %s' "${PSHA}..${SHA}" | head -40)"
  fi
  [ -n "$CHANGES" ] || CHANGES="- (no list: the previous build was not found)"
  SINCE="since the previous successful build${PN:+ ($PN, v$(version_of "$PN"))}"

  COOP="$(result_line "${RESULTS_ROOT}/$N/linux-test-coop/score.txt" 'RESULT: ')"
  SWAP="$(result_line "${RESULTS_ROOT}/$N/linux-test-swap/score.txt" 'RESULT: ')"
  NEXUS="$(result_line "${RESULTS_ROOT}/$N/nexus-smoke/smoke.txt" 'MOD RESULT: ')"

  cat > /tmp/notes.md <<EOF
# ${LINE_TITLE}

**v${VER}: test build ${N}** (draft: only visible to the owner, not an official release).

## What changed ${SINCE}

${CHANGES}

## Downloads

- \`${BASE}.zip\`: the NexusMods drop-in files. Drag everything into your Dreamland-51 game folder and start A51.exe.
- \`${BASE}_Standalone.zip\`: the cloud play build with its own launcher (extract into a new folder, double-click PLAY-COOP-2-PLAYERS.cmd; it downloads the game data).
- \`${BASE}.pdb\`: debug symbols, only needed to read a crash report.

## Automated checks

- Two-controller co-op test: ${COOP:-no result}
- Controller swap test: ${SWAP:-no result}
- NexusMods package on a fresh install (A51.exe alone): ${NEXUS:-no result}

Built from commit ${SHA} (build ${N}). The zips contain no game data.
EOF

  gh api -X PATCH "repos/$R/releases/$ID" -f name="${LINE_TITLE} v${VER} (test build ${N})" \
    -F body=@/tmp/notes.md > /dev/null

  # File names and the names shown on the page (labels).
  SHOWN="${LINE_SHOWN} v${VER}"
  gh api "repos/$R/releases/$ID/assets" --jq '.[] | "\(.id)\t\(.name)\t\(.label // "")"' | while IFS=$'\t' read -r AID ANAME ALABEL; do
    case "$ANAME" in
      *_Standalone.zip|Area51-Coop-Stage1-*.zip) NEW="${BASE}_Standalone.zip"; LABEL="${SHOWN} Developer Build" ;;
      *.pdb)                                     NEW="${BASE}.pdb";            LABEL="${SHOWN} Crash Debug" ;;
      *.zip)                                     NEW="${BASE}.zip";            LABEL="${SHOWN} Test Build" ;;
      *)                                         continue ;;
    esac
    [ "$ANAME" = "$NEW" ] && [ "$ALABEL" = "$LABEL" ] && continue
    gh api -X PATCH "repos/$R/releases/assets/$AID" -f name="$NEW" -f label="$LABEL" > /dev/null \
      && echo "release $N: $ANAME -> $NEW ($LABEL)" || echo "release $N: could not rename $ANAME"
  done
  echo "release $N: title and notes set (v$VER)"
done < /tmp/rels.txt

# Keep the newest 5 test builds of this line (by release id: drafts have no tag).
list_drafts | awk -F'\t' '{print $3" "$1}' | sort -rn | tail -n +6 | while read -r N ID; do
  gh api -X DELETE "repos/$R/releases/$ID" && echo "removed old test build $N"
done

# Official (public) releases: the names shown on the page.
gh api "repos/$R/releases?per_page=100" \
  --jq ".[] | select((.draft | not) and (.tag_name | startswith(\"${OFFICIAL_PREFIX}\"))) | \"\\(.id) \\(.tag_name)\"" \
  | while read -r ID TAG; do
      VER="$(printf '%s' "${TAG#"$OFFICIAL_PREFIX"}" | sed -E 's/^([0-9.]+).*/\1/')"
      SHOWN="${LINE_SHOWN} v${VER}"
      gh api "repos/$R/releases/$ID/assets" --jq '.[] | "\(.id)\t\(.name)\t\(.label // "")"' | while IFS=$'\t' read -r AID ANAME ALABEL; do
        case "$ANAME" in
          *_Standalone.zip) LABEL="${SHOWN} Developer Build" ;;
          *.pdb)            LABEL="${SHOWN} Crash Debug" ;;
          *.zip)            LABEL="${SHOWN} Official Release" ;;
          *)                continue ;;
        esac
        [ "$ALABEL" = "$LABEL" ] && continue
        gh api -X PATCH "repos/$R/releases/assets/$AID" -f name="$ANAME" -f label="$LABEL" > /dev/null \
          && echo "official $TAG: $ANAME shown as '$LABEL'" || echo "official $TAG: could not label $ANAME"
      done
    done
