# ADR-269: Nobody decides, and a behaviour tree would be a second interpreter over the queue we already have

**Status:** Accepted
**Date:** 2026-09-17

The brief names Unreal's Behavior Tree and StateTree documentation as references. Neither is
adopted. The reason is not that trees are bad; it is that this repository already owns the
expensive half of a behaviour tree and none of the cheap half, and what is actually missing is one
layer that neither reference is about.

---

## 1. `ActionQueue` is already a behaviour tree's task-execution half

`entity/action.hpp:281`, shipped, tested, deterministic:

* three authority tiers — `Routine`, `Action`, `Director` — with preemption;
* **resumption that keeps elapsed time.** A routine paused at 10 s and resumed at 400 s fires its
  12 s entry two seconds after the resume, not three hundred and ninety seconds late;
* eight primitives, none of which is a verb — verbs are `InteractionDesc` authored on the prop, so
  one chair drives an alien, a deer and a robot;
* start `Condition`s, an `otherwise` branch label, and `ActionResult`
  {Completed, Skipped, Failed, Cancelled} with reasons;
* exclusive interaction claims; `ActionEvent`s stamped with a timeline second, never a wall clock.

That is what a BT's tasks and decorators give you, already written. A behaviour tree on top would
be a second interpreter over the same primitives, carrying its own running/succeeded/failed enum
beside `ActionResult`. Two result vocabularies over one execution model drift the first time
somebody adds a case to one of them.

## 2. Authored control flow is already better served than a per-entity tree could serve it

`stage::Staging` expresses sequence (a cue's step list), parallel (a beat's cue list) and repeat (a
scenario's cycle). Its **parallel is across different entities** — which a per-entity behaviour
tree structurally cannot express, and ADR-210 §3 names as one of the four gaps that justified
building it at all. The shipped UFO abduction is 24 parameters, 5 beats and zero lines of C++ that
know a saucer exists.

A StateTree's advantage over a BT is that state is explicit and the branch decision is made at a
boundary rather than re-walked per tick. `Explore` already *is* a hierarchical state machine with
six phases and a chain-not-a-list transition rule. Its problem is not its shape.

## 3. What is missing is that nobody decides

`ActionQueue` is a thing that can be *told* what to do. `stage::Staging` tells it, for authored
scenarios. The only autonomous decider in the engine is inside `Explore`: 700 lines with the goal
model, five `InterestKind` affinities, a novelty radius, a stroll chance, a six-phase FSM, a
replanner, a stuck detector, a jump arc and a `recent_` visit memory all in one class.

It works. It is also why **every autonomous character in Glowmere is an explorer**. There is one
autonomous mind in this engine and it has one personality; a character that guards something, or
that runs a routine and abandons it when startled, is a second 700-line class.

## 4. Decision: a utility scorer, and the queue keeps its job

`IConsiderer` and `Option` in `src/entity/character_ai.hpp` §3.

A considerer receives a `DecisionContext` — the character's `EntityState`, its percepts, the
navigator, its seed — and appends scored `Option`s. A selector takes the highest, subject to a
dwell and a margin, and pushes the winner's `ActionDesc` list onto `Authority::Routine`. About 400
lines including the selector.

Four reasons, each tied to something already in this repository:

* **It composes.** Several considerers run in order and append to one list, so "what this species
  does", "what this scene asked for" and "what this shot needs" coexist with no merge rule. A tree
  composes by grafting, which needs a single owner of the tree.
* **It explains itself.** One axis means an overlay can print the losing scores beside the winner.
  "Why is it going that way" is a question this project has repeatedly been unable to answer about
  its own characters — `NavDebug` exists because of it (ADR-197), and was published and read by
  nobody for long enough that the data was true and invisible, which is the same as absent.
* **It is the house style already.** `Explore`'s affinities are a utility model. The auto-director
  and the cinematic brief both choose with an order-free `(seed, index)` hash
  (`app/cinematic.cpp:1555-1587`), deliberately avoiding a PRNG stream so that adding one section
  does not re-cast the whole film. A scorer inherits that discipline; a tree with random selectors
  does not.
* **It is the smallest thing that removes the actual limitation.** `Explore` minus its hardcoded
  goal model *is* a considerer.

### Three rules that are easy to leave out and expensive to add later

* **Hysteresis belongs to the selector, not the considerer.** A selector taking the top score every
  tick would thrash exactly as the gait machine thrashed before `GaitSettings::minDwell` — and for
  the same reason, which is that a speed band alone does not stop flicker in time and a dwell alone
  does not stop it in speed. A chosen option holds for a minimum dwell and must be beaten by a
  margin, not by a tie.
* **A cooldown is a term in the score, not a flag.** The moment there are two axes — priority bands,
  interrupt flags — the result stops being explicable and the overlay stops being worth drawing.
* **Considerers hold no per-character state.** One instance per *kind* of character. That is what
  makes ADR-267's D4 free: there is nothing in a considerer to checkpoint.

## 5. Rejected alongside

**GOAP and HTN.** A search over eight primitives whose preconditions are float comparisons. A
scored option list reaches the same answers without a search, and a planner's output would still
have to become an `ActionDesc` list to be executed.

**Needs, drives, emotion and social standing as layers.** Each is a term in a score or a blend
weight on a pose. None is a subsystem until there is a pose layer that can consume one, and there
is not (ADR-270 and `docs/character-ai-research.md` §C). Deferred, deliberately, under the owner's
preference for a small number of extremely solid capabilities.
