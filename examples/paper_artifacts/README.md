# TraceLoom paper artifacts

This directory is the narrow exception to the repository's general rule
against committed profiler databases. Only deterministic, manifest-governed,
claim-preserving reductions may live here. A reduced database is accepted only
when its source hash, construction command, privacy audit, expected claim
fields, and full-versus-reduced equivalence check are recorded.

Generated TraceLoom reports and sidecars do not belong in Git. The target
reviewer workflow writes them beneath an explicit temporary output directory.

## Active reduction contract: Ascend interleaved structure

**Claim.** A small ordinary Ascend profiler SQLite can preserve exact graph
reconstruction together with a large and small productive sequence interleaved
between graph units. A stock/fused pair can preserve the representative rank-0
structural delta without retaining the full production capture.

**Reproduction layer.** Author-artifact packaging of an already reproduced
external-validity observation. This reduction does not create new performance
evidence.

**Expected observation.** Each reduced input retains four exact graph units and
the three complete intervening structural units from full-profile units
`G1..G4`/`U1..U3`. Corresponding complete units retain anchor membership,
fingerprint, and cost fields exactly. The stock/fused large unit remains
1186/994 anchors respectively.

**Evidence against the claim.** Any exact graph becomes typed unrecognized;
unit membership, fingerprint, or cost changes; the structural audit fails; a
required source-row link becomes orphaned; sensitive or unrelated strings
remain; or either database exceeds 10 MiB.

**Budget and stop rule.** Try at most three deterministic time-window/dependency
policies. Do not hand-edit a database. Stop and keep the inputs external if a
faithful pair cannot fit below 20 MiB combined without weakening capability
semantics or provenance.

`tools/reduce_ascend_sqlite.py` is the deterministic reducer. It copies the
original table schemas, preserves source `rowid` where SQLite exposes one,
keeps only time-overlapping primary evidence and its task/string dependencies,
retains relevant empty tables, and deliberately removes host identity and PMU
bulk that are not used by the scoped claim. When supplied, it also reconstructs
the small numeric `host/sqlite/stream_info.db` companion required for exact
capture-instance association.

The accepted pair and its reviewer workflow are in `ascend_interleaved/`.
`tools/verify_ascend_interleaved.py` analyzes both inputs from scratch, runs the
checked-in structural audit, checks exact graph capability, validates every
materialized source-row link, and compares stable unit fields plus ordered
logical membership hashes against the full-profile reference observations.
Generated reports and sidecars remain temporary.
