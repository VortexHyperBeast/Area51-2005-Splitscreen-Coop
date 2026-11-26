#!/usr/bin/env bash
# Copies the text files in <source-dir> to <dest-subdir>/ on the branch qa-results of this
# repository and pushes it.  Used by the CI jobs so that logs and test results can be read
# without opening the Actions web pages.  Text only: never pass game data or screenshots here.
#
# usage: publish_results.sh <source-dir> <dest-subdir>
# env:   GITHUB_TOKEN, GITHUB_REPOSITORY (both set by GitHub Actions)
#        PUBLISH_REPO_URL overrides the target repository (used to test this script)
set -euo pipefail

SRC="${1:?source dir}"
DEST="${2:?destination subdir}"
if [ -z "${PUBLISH_REPO_URL:-}" ]; then : "${GITHUB_TOKEN:?}" "${GITHUB_REPOSITORY:?}"; fi

if [ ! -d "$SRC" ] || [ -z "$(ls -A "$SRC" 2>/dev/null)" ]; then
  echo "publish_results: nothing to publish from $SRC"
  exit 0
fi

REPO_URL="${PUBLISH_REPO_URL:-https://x-access-token:${GITHUB_TOKEN:-}@github.com/${GITHUB_REPOSITORY:-}.git}"
WORK="$(mktemp -d)"
IDENT=(-c user.name=coop-qa -c user.email=coop-qa@users.noreply.github.com)

if git clone -q --depth 1 --branch qa-results "$REPO_URL" "$WORK" 2>/dev/null; then
  :
else
  git init -q "$WORK"
  git -C "$WORK" checkout -q --orphan qa-results
  git -C "$WORK" remote add origin "$REPO_URL"
fi

mkdir -p "$WORK/$DEST"
cp -R "$SRC"/. "$WORK/$DEST"/
git -C "$WORK" add -A
if git -C "$WORK" diff --cached --quiet; then
  echo "publish_results: no change"
  exit 0
fi
git -C "$WORK" "${IDENT[@]}" commit -q -m "results: $DEST"

# Many jobs publish at once.  On a rejected push, start again from the newest branch tip and
# copy the files again (a rebase in a shallow clone can stop half-way and leave HEAD on the
# remote tip, and pushing that "succeeds" without the results).  Then check they arrived.
for attempt in 1 2 3 4 5 6 7 8; do
  if git -C "$WORK" push -q origin HEAD:qa-results; then
    git -C "$WORK" fetch -q --depth 1 origin qa-results || true
    if git -C "$WORK" cat-file -e "FETCH_HEAD:$DEST" 2>/dev/null; then
      echo "publish_results: pushed $DEST"
      exit 0
    fi
    echo "publish_results: push reported ok but $DEST is not on the branch, retrying"
  else
    echo "publish_results: push failed (attempt $attempt), retrying"
  fi
  sleep $(( attempt * 3 + RANDOM % 5 ))
  git -C "$WORK" rebase --abort >/dev/null 2>&1 || true
  git -C "$WORK" fetch -q --depth 1 origin qa-results
  git -C "$WORK" reset -q --hard FETCH_HEAD
  mkdir -p "$WORK/$DEST"
  cp -R "$SRC"/. "$WORK/$DEST"/
  git -C "$WORK" add -A
  if git -C "$WORK" diff --cached --quiet; then
    echo "publish_results: $DEST already on the branch"
    exit 0
  fi
  git -C "$WORK" "${IDENT[@]}" commit -q -m "results: $DEST"
done
echo "publish_results: giving up"
exit 1
