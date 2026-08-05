# TraceLoom paper artifacts

This directory is the narrow exception to the repository's general rule
against committed profiler databases. Only deterministic, manifest-governed,
claim-preserving reductions may live here. A reduced database is accepted only
when its source hash, construction command, privacy audit, expected claim
fields, and full-versus-reduced equivalence check are recorded.

Generated TraceLoom reports and sidecars do not belong in Git. The target
reviewer workflow writes them beneath an explicit temporary output directory.

After building TraceLoom, run the complete CPU-only checkout ledger with:

```bash
examples/paper_artifacts/verify.py \
  --traceloom build/native-tests/native/traceloom
```

The repository deliberately uses two complementary scales rather than padding
one fixture:

| Artifact | Main DB size | Contract |
| --- | ---: | --- |
| `ascend_interleaved` | 1.69-1.86 MiB each | exact graph/interleaved-unit fidelity |
| `../kickstart_smoke` | 36.42-38.52 MiB each | realistic ingestion and nested folding |

The medium pair is close to the requested 50 MB-per-profile review scale while
remaining natural data. Keeping the contracts separate prevents irrelevant
rows from making the exactness fixture look more representative without adding
evidence.

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

## Active scale contract: Ascend nested folding

The sanitized medium pair under `../kickstart_smoke` retains 78,585,856 bytes
of real profiler input and 145,927 normalized events. Current TraceLoom reduces
those to 44,733 semantic anchors and 990 rendered tree nodes while recovering
both `Repeat x29 -> Repeat x74` and `Repeat x29 -> Repeat x24` on each device.
This is a structural-compression claim only; elapsed analyzer time is not part
of the contract.

`tools/verify_kickstart_folding.py` checks input hashes and privacy, regenerates
both reports, verifies exact event/anchor/node counts and nested repeats, and
requires at least 40 anchors and 100 normalized events per rendered node.

## Fixed workflow-comparison receipt

`workflow_comparison/` adds no new capture. It evaluates top-k aggregation,
direct raw SQL, repeat-only structural compression, and complete TraceLoom over
the same exact stock/fused pair. Its verifier freezes the directly observed
answers, including four exact units, the neutral interleaved order, zero
orphaned source links, and the opposite cost directions inside and between
graph units. The interpretation and explicit non-claims are recorded in
`../../notes/workflow-comparison-study.md`.
