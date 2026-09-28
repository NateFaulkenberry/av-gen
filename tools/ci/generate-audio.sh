#!/usr/bin/env bash
# Generate the repository's own scores (assets/audio/*.wav) wherever they are missing, and prove each
# one is the file the manifest describes.
#
#   tools/ci/generate-audio.sh
#
# They are not committed (16-20 MB each) because they are deterministic: tools/make_glowmere_score.py
# and tools/make_city_score.py compute every sample from notes in the script, with no third-party
# rights (assets/audio/manifest.json). So CI regenerates them instead of skipping the ~20 cases that
# play them. Each takes about 50 s of stdlib Python; the two run in parallel.
#
# A generated file whose sha256 differs from the manifest is DELETED, with a warning: a score that is
# not the recorded one would make the cases that play it assert against the wrong music, and a SKIP
# ("... is generated, not committed") is the honest result. An existing file is left alone.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
manifest=assets/audio/manifest.json

# name <TAB> file <TAB> command <TAB> sha256, one line per track.
tracks=$(python3 -c '
import json, sys
for t in json.load(open(sys.argv[1]))["tracks"]:
    print("\t".join([t["name"], "assets/" + t["file"], t["command"], t["sha256"]]))
' "$manifest") || { echo "generate-audio: cannot read $manifest" >&2; exit 1; }

pids=(); names=()
while IFS=$'\t' read -r name file command sha; do
    [[ -e "$file" ]] && { echo "generate-audio: $file present, left alone"; continue; }
    echo "generate-audio: $name <- $command"
    # The manifest's command is this repository's own; it is run as written, from the root.
    (eval "$command" > "${TMPDIR:-/tmp}/generate-audio-$name.log" 2>&1) &
    pids+=($!); names+=("$name|$file|$sha")
done <<< "$tracks"

[[ ${#pids[@]} -eq 0 ]] && exit 0
for i in "${!pids[@]}"; do
    IFS='|' read -r name file sha <<< "${names[$i]}"
    if ! wait "${pids[$i]}"; then
        echo "::warning title=Score not generated::$name: the generator failed (log below); its cases will SKIP"
        cat "${TMPDIR:-/tmp}/generate-audio-$name.log" >&2
        rm -f "$file"; continue
    fi
    got=$(shasum -a 256 "$file" | cut -d' ' -f1)
    if [[ "$got" != "$sha" ]]; then
        echo "::warning title=Score differs from its manifest::$file is $got, the manifest records $sha; deleted, so its cases SKIP instead of playing different music"
        rm -f "$file"
    else
        echo "generate-audio: $file matches the manifest ($sha)"
    fi
done
# A failure here is a warning, not a failed job: without the file the cases skip, as they always did.
exit 0
