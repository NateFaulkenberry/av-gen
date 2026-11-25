#!/bin/bash
# Regenerates the engine's published contracts from the engine itself (ADR-2150, ADR-2152):
#   schemas/tool-catalogue.json, docs/api/tools.md, docs/api/errors.md, docs/api/protocol.md,
#   schemas/avgen-project.schema.json, schemas/avgen-scene.schema.json
# Run it after changing any tool's name, description, schema or annotations, an error code, a parser
# key list or a typing table (src/api/schema_types_*.cpp); tests/unit/test_api_catalogue.cpp fails
# until you do.
#
#   tools/gen_api.sh [path/to/avgen]          (default: build/release/src/avgen)
#   tools/gen_api.sh --baseline [path/to/avgen]
#
# --baseline also rewrites schemas/tool-catalogue-baseline.json, the contract the breaking-change
# check compares against. Do that ONLY in the commit that raises avgen::api::kApiLevel.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASELINE=0
if [ "${1:-}" = "--baseline" ]; then BASELINE=1; shift; fi
AVGEN="${1:-$ROOT/build/release/src/avgen}"
[ -x "$AVGEN" ] || { echo "no avgen at $AVGEN; build it first or pass its path" >&2; exit 1; }
"$AVGEN" --describe all --out "$ROOT"
if [ "$BASELINE" = 1 ]; then
    "$AVGEN" --describe baseline --out "$ROOT"
fi
