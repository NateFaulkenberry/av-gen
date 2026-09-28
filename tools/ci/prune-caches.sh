#!/usr/bin/env bash
# Keep only the newest Actions cache entry per key prefix on main, so the repository stays under its
# 10 GB cache quota and the caches main needs are not the ones evicted.
#
# Every build saves `ccache-<os>-<arch>-xcode<v>-<preset>-<sha>`, and a restore takes the newest by
# prefix, so older entries of the same prefix are never read again. On 2026-09-28 eleven of them sat on
# main (116-526 MB each, 10.7 GB in all, over the quota) and the TSan cache had been evicted.
#
# Run by each workflow's report job on main, with GH_TOKEN carrying `actions: write`. Locally:
#   GH_TOKEN=$(gh auth token) GITHUB_REPOSITORY=NateFaulkenberry/av-gen tools/ci/prune-caches.sh
set -euo pipefail
repo="${GITHUB_REPOSITORY:?set GITHUB_REPOSITORY=owner/name}"

# Each distinct key with its trailing commit sha removed is one prefix; keep the newest of each.
keys=$(gh cache list --repo "$repo" --ref refs/heads/main --limit 100 --sort created_at --order desc \
           --json id,key,sizeInBytes --jq '.[] | "\(.id)\t\(.key)\t\(.sizeInBytes)"')
declare_seen=""
deleted=0; freed=0
while IFS=$'\t' read -r id key size; do
    [[ -z "$id" ]] && continue
    case "$key" in
        ccache-*) prefix="${key%-*}" ;;   # drop the sha
        *) prefix="$key" ;;               # cpm-*: the key is the content hash; each is distinct
    esac
    case "$key" in cpm-*) prefix="cpm" ;; esac
    if [[ "$declare_seen" == *"|$prefix|"* ]]; then
        gh cache delete "$id" --repo "$repo" >/dev/null && { deleted=$((deleted + 1)); freed=$((freed + size)); }
        echo "deleted $key"
    else
        declare_seen="$declare_seen|$prefix|"
        echo "kept    $key"
    fi
done <<< "$keys"
echo "pruned $deleted cache entr$( [[ $deleted -eq 1 ]] && echo y || echo ies ), $((freed / 1048576)) MB"
