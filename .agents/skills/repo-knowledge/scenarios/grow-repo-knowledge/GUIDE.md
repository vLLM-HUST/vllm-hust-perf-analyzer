# Grow repository knowledge

Use this scenario when current work has earned reusable repository-specific
understanding, an expensive bounded observation, or capability, or when the
local `repo-knowledge` tree itself needs to change. This is a living
maintenance scenario and the canonical shape reference for new scenarios—not
a template whose headings must be copied.

## Decide whether growth is earned

Read the catalog and relevant existing scenarios first. Grow the tree only when
all five judgments hold:

1. **Paid:** the work exposed non-trivial friction, reasoning, rediscovery,
   failure, useful task-local glue, or an expensive experiment or observation.
2. **Repeatable:** a credible future repository task or inquiry will consume
   the result.
3. **Local:** the leverage belongs to this repository rather than nearby source
   truth or a cross-repository personal Skill.
4. **Actionable:** it will change future action or judgment by reducing
   reading, guessing, reconstruction, experimental cost, or execution risk.
5. **Cheaper:** finding, understanding, validating, and maintaining the asset
   costs less than rediscovering it.

If a judgment does not hold, leave the tree unchanged.

## Grow like a pine

1. Prefer the matching existing scenario. Make a useful branch thicker before
   creating another branch.
2. Add `scenarios/<working-situation>/` only when the future entry condition and
   outcome are genuinely distinct from every existing scenario.
3. Give each scenario a `GUIDE.md` that makes its task or knowledge-entry
   condition recognizable and carries the accepted understanding, authority
   or invariants, bounded observations, reliable workflow, and verification
   that the scenario actually needs. Omit empty ceremonial headings.
4. Colocate scenario-owned observation notes, executable helpers, fixtures,
   and bounded evidence beside that guide. Add them only when they reduce
   repeated or fragile inquiry or work.
5. Add one concise catalog entry with the outcome, use condition, and local
   guide link. Do not duplicate the guide in the catalog.
6. Run the sibling `validate` executable after changing this tree, then run any
   scenario-specific validation affected by the change.

## Keep the metabolism positive

- Do not restate source truth that is cheaper to read where it is owned.
- Do not create empty scenarios, speculative taxonomies, or a new branch for
  every task.
- Do not split the tree into root-level content-type homes such as `docs/`,
  `scripts/`, `fixtures/`, or `evidence/`; keep working material with its
  scenario.
- Do not add ownership fields, reuse counts, lifecycle states, promotion
  ceremonies, or periodic freshness rituals to the catalog.
- Do not preserve bulky logs or evidence when a compact observation, invariant,
  fixture, or executable check carries the lesson better.

## Preserve what was learned without hardening guesses

- Keep an accepted contract or invariant distinct from an inference and from
  an experimental observation when future work could mistake one for another.
- Preserve an expensive observation even before it supports a general
  conclusion when a credible future inquiry can use it. Record the smallest
  sufficient envelope: relevant setup and input, tested artifact identity,
  observed result, repeat or variability information when it matters, and the
  remaining interpretation boundary.
- Prefer a compact result, source or artifact anchor, fixture, or bounded
  reproducer over raw transcripts. Retain fuller evidence only when the compact
  form cannot support future judgment, and keep it scenario-local and bounded.
- Name scenario-local files by their future use rather than creating a
  content-type sub-tree. A knowledge-heavy scenario is valid without an
  executable when its understanding already changes future inquiry or action.

## Verify that the tree gained blood

The addition is useful only if a future Lumi can enter through the catalog,
recognize when the scenario applies, and answer the question or perform the
work with less attention, tokens, experimental cost, error, or maintenance.
Exercise new helpers on a bounded real case or fixture and check observation
anchors or reproducers when present. If the asset cannot change a future
judgment or action, remove or simplify it before considering the growth
complete.

This scenario demonstrates the intended shape: a concrete use condition,
decision boundary, reliable procedure, maintenance limits, verification, and a
colocated executable. Adapt that shape to reality rather than copying its prose.
