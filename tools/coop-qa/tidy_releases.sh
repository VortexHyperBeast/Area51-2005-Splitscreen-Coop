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

version_of() {  # build number -> version shown in the menu
  if [ "$1" -le 95 ]; then printf '0.%02d' "$1"; else printf '0.95.%d' "$(( $1 - 95 ))"; fi
}

result_line() {  # $1 = qa-results path, $2 = line prefix
  git show "origin/qa-results:$1" 2>/dev/null | tr -d '\r' | grep -m1 "^$2" | sed "s/^$2//" || true
}

# Successful builds of coop/stage1, newest first: "<run_number> <head_sha>"
gh api "repos/$R/actions/workflows/coop-build.yml/runs?branch=coop/stage1&status=success&per_page=100" \
  --jq '.workflow_runs[] | "\(.run_number) \(.head_sha)"' > /tmp/builds.txt || true

# Drafts have no tag yet: list them by release id through the API instead.
gh api "repos/$R/releases?per_page=100" \
  --jq '.[] | select(.draft and (.name | test("build [0-9]+"))) | "\(.id) \(.target_commitish) \(.name)"' > /tmp/rels.txt

while read -r ID SHA TITLE; do
  N="$(printf '%s' "$TITLE" | grep -o 'build [0-9]*' | head -1 | grep -o '[0-9]*')"
  [ -n "$N" ] || continue
  VER="$(version_of "$N")"
  BASE="AREA51_SplitscreenCoop_v${VER}"

  # What changed: commits since the previous successful build.
  PREV="$(awk -v n="$N" '$1 < n { print; exit }' /tmp/builds.txt)"
  PN="${PREV%% *}"; PSHA="${PREV#* }"
  CHANGES=""
  if [ -n "$PREV" ] && git cat-file -e "${PSHA}^{commit}" 2>/dev/null && git cat-file -e "${SHA}^{commit}" 2>/dev/null; then
    CHANGES="$(git log --no-merges --format='- %s' "${PSHA}..${SHA}" | head -40)"
  fi
  [ -n "$CHANGES" ] || CHANGES="- (no list: the previous build was not found)"
  SINCE="since the previous successful build${PN:+ ($PN, v$(version_of "$PN"))}"

  COOP="$(result_line "runs/$N/linux-test-coop/score.txt" 'RESULT: ')"
  SWAP="$(result_line "runs/$N/linux-test-swap/score.txt" 'RESULT: ')"
  NEXUS="$(result_line "runs/$N/nexus-smoke/smoke.txt" 'MOD RESULT: ')"

  cat > /tmp/notes.md <<EOF
# AREA 51 SPLITSCREEN CO-OP

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

  gh api -X PATCH "repos/$R/releases/$ID" -f name="AREA 51 SPLITSCREEN CO-OP v${VER} (test build ${N})" \
    -F body=@/tmp/notes.md > /dev/null

  # Rename the files.
  gh api "repos/$R/releases/$ID/assets" --jq '.[] | "\(.id) \(.name)"' | while read -r AID ANAME; do
    case "$ANAME" in
      AREA51_SplitscreenCoop_*)          continue ;;
      Area51-SplitscreenCoop-*.zip)      NEW="${BASE}.zip";            LABEL="NexusMods drop-in files" ;;
      Area51-Coop-Stage1-*.zip)          NEW="${BASE}_Standalone.zip"; LABEL="Standalone play build with launcher" ;;
      *.pdb)                             NEW="${BASE}.pdb";            LABEL="Debug symbols (only for crash reports)" ;;
      *)                                 continue ;;
    esac
    gh api -X PATCH "repos/$R/releases/assets/$AID" -f name="$NEW" -f label="$LABEL" > /dev/null \
      && echo "release $N: $ANAME -> $NEW" || echo "release $N: could not rename $ANAME"
  done
  echo "release $N: title and notes set (v$VER)"
done < /tmp/rels.txt
