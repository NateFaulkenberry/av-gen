# ADR-1105: A degradation priority is data

**Status:** Accepted (live optimizer, Stage 4.4). **Date:** 2026-10-03

`live.priority` names lever groups in the order to give them up: particles, shadows, volumes, post, lod, resolution.
`ladderFromPriority` builds the five levels from it -- High gives up the first group a little; Medium the first fully and
the second a little; Low the first two fully and the next two a little; Emergency everything named fully -- and a group
the list leaves out is touched only at Emergency, a little, so a project that does not name "resolution" keeps its
output sharp until the last level. The built ladder replaces the strategy's table in the controller
(`InteractiveResolution::ladder()`); without a priority the three ADR-1084 tables are unchanged.
