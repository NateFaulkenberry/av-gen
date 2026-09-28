#!/usr/bin/env bash
# Run one of CI's test suites exactly as CI runs it. The workflows call this script; so can you.
#
#   tools/ci/run-suite.sh <suite> [--build] [--bin <dir>] [--out <dir>] [--filter <spec>] [--seed <n>]
#
#   suite        preset   binary               what CI does with it
#   cpu          release  avgen_tests          every push; GATES (3 shards)
#   cpu-assets   release  avgen_tests          nightly with the private test assets (Glowmere tier); GATES
#   gpu-hosted   release  avgen_render_tests   main/nightly on a hosted VM; INFORMATIONAL (its Metal can't
#                                              compile the renderer's pipelines)
#   gpu          release  avgen_render_tests   self-hosted Apple-silicon runner with the private assets;
#                                              GATES. Runs under tools/gpu-lock.sh
#   asan:<job>   asan     avgen_tests          nightly; one job of the sanitizer plan (jobs below)
#   ubsan        ubsan    avgen_tests          nightly; UBSan alone over the whole default set
#   tsan         tsan     avgen_tests          nightly; the concurrency set (TSAN_FILTER below)
#
#   --build        configure and build the suite's preset first, with tools/ci/build.sh (as CI does)
#   --bin <dir>    where the binaries are (default: build/<preset>/bin if CI packaged them there,
#                  else build/<preset>/tests and build/<preset>/src)
#   --out <dir>    results (default: ci-results/<suite>); result.json, summary.md, shard logs and XML
#   --filter       override the suite's Catch2 filter (the summary labels the run FILTERED)
#   --seed         Catch2 --rng-seed (default: $GITHUB_RUN_ID in CI, so an ASan matrix shares one order)
#
# Examples:
#   tools/ci/run-suite.sh cpu --build              # the push gate, locally, from scratch
#   tools/ci/run-suite.sh ubsan --build --filter '[ai]'
#   tools/ci/run-suite.sh asan:rest-a              # one nightly ASan job, from build/asan
#
# Locally the CPU suite counts as GPU work (tools/gpu-lock.sh explains why): run the full cpu suite
# when nothing else is using the GPU, or under the lock.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

suite="${1:?usage: run-suite.sh <suite> [--build] [--bin dir] [--out dir] [--filter spec] [--seed n]}"
shift
build=false; bin=""; out=""; filter=""; seed="${GITHUB_RUN_ID:-}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build) build=true ;;
        --bin) bin="$2"; shift ;;
        --out) out="$2"; shift ;;
        --filter) filter="$2"; shift ;;
        --seed) seed="$2"; shift ;;
        *) echo "run-suite: unknown argument '$1'" >&2; exit 2 ;;
    esac
    shift
done
[[ -z "$seed" ]] && seed=$(( $(date +%s) % 2147483647 ))
seed=$(( seed % 2147483647 ))

EXC=tools/ci/hosted-runner-exceptions.txt
PLAN=tools/ci/sanitizer-plan.txt
# Every tag whose cases start threads (census in docs/development/ci.md, "TSan"). Tags only, OR-ed.
TSAN_FILTER="[jobs],[job],[motionthreads],[threading],[worldbuilder],[ring],[analysis],[motionlib],[ai],[transport]"

# The ASan/UBSan matrix: job -> plan part, rest shards run here, total rest shards, first shard.
# Three processes per job: the hosted runner has 3 vCPUs. See tools/ci/sanitizer-plan.txt.
asan_job() {
    case "$1" in
        heavy-1) echo "heavy-1 0 0 0" ;;
        heavy-2) echo "heavy-2 0 0 0" ;;
        heavy-3) echo "heavy-3 1 7 0" ;;
        rest-a)  echo "- 3 7 1" ;;
        rest-b)  echo "- 3 7 4" ;;
        *) echo "run-suite: unknown ASan job '$1' (heavy-1 heavy-2 heavy-3 rest-a rest-b)" >&2; exit 2 ;;
    esac
}

case "$suite" in
    cpu|cpu-assets|gpu-hosted|gpu) preset=release ;;
    asan:*) preset=asan ;;
    ubsan) preset=ubsan ;;
    tsan) preset=tsan ;;
    *) echo "run-suite: unknown suite '$suite'" >&2; exit 2 ;;
esac

if [[ "$build" == true ]]; then
    tools/ci/build.sh "$preset" "${out:-ci-results/$suite}/build"
fi
if [[ -z "$bin" ]]; then
    bin="build/$preset/bin"
    [[ -d "$bin" ]] || bin="build/$preset/tests"
fi
binary() { [[ -x "$bin/$1" ]] || { echo "run-suite: no $bin/$1 (build it, or pass --build / --bin)" >&2; exit 2; }; echo "$bin/$1"; }
out="${out:-ci-results/${suite/:/-}}"

args=(--out "$out" --seed "$seed")
filtered=()
[[ -n "$filter" ]] && filtered=(--filter "$filter" --expected-label "FILTERED $filter: not the full suite")

case "$suite" in
    cpu)
        exec python3 tools/ci/catch2_run.py run --binary "$(binary avgen_tests)" --name cpu \
            --label "CPU suite (avgen_tests)" "${args[@]}" --shards 3 --timeout-min 100 \
            --exceptions "$EXC" ${filtered[@]+"${filtered[@]}"}
        ;;
    cpu-assets)
        # No hosted-runner exceptions: with the private assets linked, a case that needs them must pass.
        exec python3 tools/ci/catch2_run.py run --binary "$(binary avgen_tests)" --name cpu-assets \
            --label "CPU suite with the private test assets (avgen_tests)" "${args[@]}" --shards 3 \
            --timeout-min 150 ${filtered[@]+"${filtered[@]}"}
        ;;
    gpu-hosted)
        exec python3 tools/ci/catch2_run.py run --binary "$(binary avgen_render_tests)" --name gpu \
            --label "GPU suite (avgen_render_tests)" "${args[@]}" --shards 1 --timeout-min 75 --report-only \
            --exceptions "$EXC" \
            --expected-label "INFORMATIONAL: hosted VM GPU (Apple Paravirtual device) is not authoritative" \
            ${filtered[@]+"${filtered[@]}"}
        ;;
    gpu)
        # One process, serialised with every other GPU user on the machine (the runner may be a
        # developer's Mac). The lock wraps the whole runner so the binary's exit code is read by
        # catch2_run.py, never through the wrapper (tools/gpu-lock.sh, "THE ONE SENTENCE").
        exec tools/gpu-lock.sh python3 tools/ci/catch2_run.py run --binary "$(binary avgen_render_tests)" \
            --name gpu --label "GPU suite (avgen_render_tests, real Apple GPU)" "${args[@]}" --shards 1 \
            --timeout-min 120 ${filtered[@]+"${filtered[@]}"}
        ;;
    asan:*)
        job="${suite#asan:}"
        read -r part shards total first < <(asan_job "$job")
        [[ "$part" == "-" ]] && part=""
        common=(--binary "$(binary avgen_tests)" --name "asan-$job" --label "ASan: $job" "${args[@]}"
                --timeout-min 330 --exceptions "$EXC" --gate sanitizer)
        if [[ -n "$filter" ]]; then
            # A filtered dispatch runs in one job only; the others have nothing to do.
            [[ "$job" == rest-a ]] || { echo "A filtered ASan run uses the rest-a job only."; exit 0; }
            exec python3 tools/ci/catch2_run.py run "${common[@]}" --shards 3 "${filtered[@]}"
        fi
        label="$PLAN: ${part:-no named processes}"
        [[ "$shards" -gt 0 ]] && label="$label; rest shards $((first + 1))-$((first + shards)) of $total"
        exec python3 tools/ci/catch2_run.py run "${common[@]}" --plan "$PLAN" --part "$part" \
            --shards "$shards" --shard-total "$total" --shard-first "$first" --expected-label "$label"
        ;;
    ubsan)
        exec python3 tools/ci/catch2_run.py run --binary "$(binary avgen_tests)" --name ubsan \
            --label "UBSan (avgen_tests, whole default set)" "${args[@]}" --shards 3 --timeout-min 330 \
            --exceptions "$EXC" --gate sanitizer ${filtered[@]+"${filtered[@]}"}
        ;;
    tsan)
        f="${filter:-$TSAN_FILTER}"
        label="SUBSET $f: not the full suite"
        [[ -n "$filter" ]] && label="FILTERED $f: not the full suite"
        exec python3 tools/ci/catch2_run.py run --binary "$(binary avgen_tests)" --name tsan \
            --label "TSan" "${args[@]}" --shards 3 --timeout-min 300 \
            --exceptions "$EXC" --gate sanitizer --filter "$f" --expected-label "$label"
        ;;
esac
