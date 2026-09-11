---
id: gaps/ai-director
title: The AI Director
category: Not Yet Documented
summary: The AI control plane is being built now; there is no tool list to document and none is invented here.
order: 202
status: not-yet-documented
tags: ai, director, prompt, tools, agent, model
keywords: how do i use the ai; ai director; can the ai change my scene; prompting; what can the ai do; undo an ai change
related: gaps/world-editor, start/how-it-works
---

# The AI Director

**This area is not documented yet, and this page will not guess.**

The AI control plane — the prompt panel, the provider abstraction, the tool registry and the
transaction model — is being built right now, in parallel with this Help system. There is no tool
list to document, so none is invented here.

## What is missing

- What the AI can and cannot change
- How to prompt it, from single commands to full production direction
- Which tools exist, what each does, and when it is useful
- What "working" means, how plans work, and how to cancel one
- How to undo an AI-generated modification
- Choosing a model and where credentials live
- Which operations are destructive
- How the AI discovers what AV Gen supports

## What is safe to say today

Help itself is already built to be the AI's knowledge source. The same five retrieval calls the
Help panel uses — `help.search`, `help.get`, `help.related`, `help.getShortcut`, `help.getFeature` —
are exposed as read-only tool descriptors for a tool registry to register. **There is one
documentation database and two consumers**, so an answer the AI gives about AV Gen and an answer
you can read in this panel come from the same place and cannot drift apart.

When the AI pass lands, its tools should carry a Help topic each, so that a capability, its
documentation and its machine-readable description stay one record rather than three.

## When this will be written

When the AI control-plane pass lands.
