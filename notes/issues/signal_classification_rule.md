### Target repository

vllm-hust-perf-analyzer

### Problem statement

## Background

TraceLoom currently classifies raw profiler timeline events using manually encoded matching logic. This includes decisions such as:

- which raw events should become compute or collective anchors
- which events should be treated as prelude or auxiliary events
- which events should be ignored
- how event names, stream scopes, runtime records, and device records map to TraceLoom semantic roles

This works for the current Ascend/vLLM traces, but it makes the analyzer harder to maintain as CANN, vLLM, model architectures, and profiler schemas evolve.

## Problem

The signal classification policy is currently embedded in code. As a result:

- adding or adjusting matching rules requires modifying parser/analyzer logic
- different profiling environments cannot easily provide their own classification policy
- it is hard to review, diff, or document why a given event is classified as an anchor, prelude event, auxiliary event, or ignored event
- rule precedence and fallback behavior are implicit

## Proposal

Extract the current built-in classification behavior into a data-driven default ruleset.

A first version could use a simple structured format such as YAML, TOML, JSON, or CSV. Each rule could include fields like:

- source table or source domain
- event name pattern
- stream scope or host/device scope
- category constraints
- semantic role, such as `anchor`, `prelude_aux`, `comm`, or `ignore`
- priority or precedence
- optional notes explaining why the rule exists

TraceLoom should load this default ruleset internally, while also allowing users to pass an override or extension ruleset from the CLI.

## Expected Outcome

- Existing behavior remains the default.
- Signal classification becomes inspectable and easier to review.
- New CANN/vLLM/profiler variants can be supported by editing rules rather than changing core analyzer code.
- Future reports can optionally include which rule classified each event, improving debuggability.





### Proposed solution

## Possible Implementation Steps

1. Identify the current hard-coded event classification points.
2. Define a minimal rule schema that can express the existing behavior.
3. Move the current default policy into a bundled ruleset file.
4. Add CLI/config support for user-provided rulesets.
5. Add tests that verify the extracted rules preserve current outputs on representative traces.

### Expected impact and tradeoffs

_No response_