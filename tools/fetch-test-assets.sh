#!/usr/bin/env bash
# Fetch the private test-asset repository and link its files into this checkout's assets/.
#
#   tools/fetch-test-assets.sh [--check] [--dir <clone-dir>] [--ref <commit>]
#
# The assets the GPU suite and the Glowmere validation cases need include purchased packs (the aliens,
# the farm animals) that must never be committed to this PUBLIC repository, cached by Actions or
# uploaded as an artifact. They live in a PRIVATE repository instead (docs/development/
# gpu-ci-private-assets.md). This script is the one way both a developer and CI get them:
#
#   1. clone or update that repository OUTSIDE the checkout (default ~/.cache/avgen-test-assets, or
#      $AVGEN_TEST_ASSETS_DIR; in CI, $RUNNER_TEMP), so nothing under the work tree can ever `git add` it;
#   2. check out the exact commit pinned in tools/ci/test-assets.lock, so a test run is reproducible;
#   3. verify every file against the repository's own SHA256SUMS;
#   4. symlink each file into assets/<same path>, and only where git ignores that path. A file git
#      would track is refused, loudly: a link there is one `git add -A` from publishing the asset.
#
# It only fills gaps, like tools/link-worktree-assets.sh: a real file already at a path is left alone.
# A path this repository tracks is skipped (the checkout has it from git), never linked or refused.
#
# Authentication, first match wins:
#   AVGEN_TEST_ASSETS_TOKEN   a fine-grained PAT with Contents: read on the asset repository (CI secret)
#   AVGEN_TEST_ASSETS_SSH_KEY a read-only deploy key's private half (CI secret)
#   neither                   your own git credentials (ssh-agent, or `gh auth setup-git`), locally
#
# --check  resolve and verify only; link nothing, print what would be linked.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lock="$root/tools/ci/test-assets.lock"
in_ci=${GITHUB_ACTIONS:-false}

err() {
    # One line a person can act on, as an Actions annotation when in CI.
    if [[ "$in_ci" == true ]]; then echo "::error title=Test assets unavailable::$*"; fi
    echo "fetch-test-assets: $*" >&2
    exit 1
}

check_only=false
dir="${AVGEN_TEST_ASSETS_DIR:-}"
ref=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --check) check_only=true ;;
        --dir) dir="$2"; shift ;;
        --ref) ref="$2"; shift ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) err "unknown argument '$1' (see --help)" ;;
    esac
    shift
done

[[ -f "$lock" ]] || err "missing $lock"
# The lock file: `repository <owner/name>` and `commit <sha>` lines; '#' starts a comment.
repo=$(awk '$1=="repository"{print $2}' "$lock")
pinned=$(awk '$1=="commit"{print $2}' "$lock")
[[ -n "$repo" ]] || err "$lock names no repository"
ref="${ref:-$pinned}"
if [[ -z "$ref" || "$ref" == "none" ]]; then
    err "$lock pins no commit yet: the private asset repository has not been published (docs/development/gpu-ci-private-assets.md, step 3)"
fi
if [[ -z "$dir" ]]; then
    if [[ "$in_ci" == true && -n "${RUNNER_TEMP:-}" ]]; then dir="$RUNNER_TEMP/avgen-test-assets"
    else dir="$HOME/.cache/avgen-test-assets"; fi
fi
case "$(cd "$(dirname "$dir")" 2>/dev/null && pwd)/" in
    "$root"/*) err "--dir $dir is inside the checkout; it must be outside it, so git can never see the clone" ;;
esac

# ---- authentication: one-shot, never written to the clone's config --------------------------------
git_auth=()
no_secret=""
url="https://github.com/$repo.git"
keyfile=""
cleanup() { [[ -n "$keyfile" ]] && rm -f "$keyfile"; return 0; }
trap cleanup EXIT
if [[ -n "${AVGEN_TEST_ASSETS_TOKEN:-}" ]]; then
    # An extra header for this invocation only: the token is never in a URL, a remote or a log line.
    basic=$(printf 'x-access-token:%s' "$AVGEN_TEST_ASSETS_TOKEN" | base64 | tr -d '\n')
    [[ "$in_ci" == true ]] && echo "::add-mask::$basic"
    git_auth=(-c "http.https://github.com/.extraheader=AUTHORIZATION: basic $basic")
elif [[ -n "${AVGEN_TEST_ASSETS_SSH_KEY:-}" ]]; then
    keyfile=$(mktemp "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/avgen-assets-key.XXXXXX")
    chmod 600 "$keyfile"
    printf '%s\n' "$AVGEN_TEST_ASSETS_SSH_KEY" > "$keyfile"
    url="git@github.com:$repo.git"
    export GIT_SSH_COMMAND="ssh -i $keyfile -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"
elif [[ "$in_ci" == true ]]; then
    # Fatal only if this run has to reach the network (an existing clone that has the commit does not).
    no_secret="neither the AVGEN_TEST_ASSETS_TOKEN nor the AVGEN_TEST_ASSETS_SSH_KEY secret is set for this repository, so $repo cannot be read (docs/development/gpu-ci-private-assets.md, step 4)"
else
    # Locally: whatever the developer's git already uses for github.com. ssh if an agent has a key.
    if ssh-add -l >/dev/null 2>&1; then url="git@github.com:$repo.git"; fi
fi

# ---- clone or update, then pin -------------------------------------------------------------------
if [[ -d "$dir/.git" ]]; then
    if ! git -C "$dir" cat-file -e "$ref^{commit}" 2>/dev/null; then
        [[ -n "$no_secret" ]] && err "$no_secret"
        git -C "$dir" remote set-url origin "$url"
        git ${git_auth[@]+"${git_auth[@]}"} -C "$dir" fetch --quiet origin || err "cannot fetch $repo into $dir (no access, or the repository does not exist)"
    fi
else
    [[ -n "$no_secret" ]] && err "$no_secret"
    mkdir -p "$(dirname "$dir")"
    git ${git_auth[@]+"${git_auth[@]}"} clone --quiet --no-checkout "$url" "$dir" ||
        err "cannot clone $repo (no access, or the repository does not exist). Locally: check 'gh auth status' or your ssh key; in CI: check the secret's scope"
fi
git -C "$dir" cat-file -e "$ref^{commit}" 2>/dev/null || err "$repo has no commit $ref (tools/ci/test-assets.lock is ahead of the asset repository?)"
git -C "$dir" -c advice.detachedHead=false checkout --quiet --force "$ref"
git -C "$dir" clean -fdq

# ---- verify ----------------------------------------------------------------------------------------
[[ -f "$dir/SHA256SUMS" ]] || err "$repo@$ref has no SHA256SUMS"
(cd "$dir" && shasum -a 256 --quiet -c SHA256SUMS) || err "$repo@$ref does not match its own SHA256SUMS"

# ---- link ------------------------------------------------------------------------------------------
# Every asset in the repository sits under assets/ with the same relative path it has here.
linked=0; present=0; tracked=0; refused=()
while IFS= read -r rel; do
    dest="$root/assets/$rel"
    # A path this repository TRACKS is never linked: the checkout already has the file from git, so
    # the private copy is redundant (e.g. the three CC0 Quaternius fixtures, tracked since 2026-09-28,
    # which an asset repository built before then still carries). Skipping it cannot stage anything.
    if git -C "$root" ls-files --error-unmatch -- "assets/$rel" >/dev/null 2>&1; then
        tracked=$((tracked + 1))
        continue
    fi
    if ! git -C "$root" check-ignore -q "assets/$rel"; then
        refused+=("assets/$rel")
        continue
    fi
    if [[ -e "$dest" || -L "$dest" ]]; then
        present=$((present + 1))
        continue
    fi
    if [[ "$check_only" == true ]]; then
        echo "would link assets/$rel"
    else
        mkdir -p "$(dirname "$dest")"
        ln -s "$dir/assets/$rel" "$dest"
    fi
    linked=$((linked + 1))
done < <(cd "$dir/assets" && find . -type f | sed 's|^\./||' | sort)

if [[ ${#refused[@]} -gt 0 ]]; then
    printf '  %s\n' "${refused[@]}" >&2
    err "${#refused[@]} file(s) in $repo land on paths this repository does NOT ignore; refusing to link any of them. Add them to .gitignore first (a public repository must never be able to stage them)."
fi
verb=linked; [[ "$check_only" == true ]] && verb="would link"
ref=$(git -C "$dir" rev-parse HEAD)
echo "fetch-test-assets: $repo@${ref:0:12} -> $dir; $verb $linked file(s), $present already present, $tracked tracked here (skipped)"
if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "AVGEN_TEST_ASSETS_REV=$ref" >> "$GITHUB_ENV"
fi
