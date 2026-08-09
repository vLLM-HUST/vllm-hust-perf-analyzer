# Ascend TP2 exact-composition pair

This checkout-bundled pair freezes TraceLoom's strongest current Ascend exact
reconstruction result. It contains two ranks of one retained Qwen2.5-3B TP2
graph-mode capture. The profiles were produced by the project maintainers and
are redistributed as repository test data under the repository license. They
contain profiler records, not model weights or prompt payloads.

Each rank independently recovers:

- 1,110 completion-backed, capture-instance-linked graph launches;
- 30 exact `H + L×35 + T` replay units with 37 ordered launch members each;
- one 37-slot graph template with the shared body-sequence identity;
- zero incomplete or legacy replay units; and
- raw-row resolution for all 1,110 host launch rows and 13,500 distinct task
  rows supporting launch control and visible graph bodies; and
- eight repeated pre-graph `AllReduce` positions (280 occurrences) with every
  selected anchor resolved to a distinct raw `COMMUNICATION_OP` row.

The communication slice also freezes a cross-rank localization contrast that
is invisible in replay counts alone: replay-envelope time is 817.243/848.158 ms
(1.038x), while the same eight pre-graph `AllReduce` positions total
763.455/7,530.700 ms (9.864x) on device 2/device 3. TraceLoom reports structural
position and source evidence; workload-semantic attribution remains a separate
human or agent judgment.

The replay-internal cost map receipt is asserted per rank: 30 fully supported
multi-launch units, 1,110 ordered launches, zero unsupported launches, 10,170
member rows, and 339 role-collapsed aligned aggregates, with the exact
H + L×35 + T slot positions preserved in every unit (member_order 0..36 maps
to head/layer×35/tail with slot_order 0..36) and every replay member resolved
to the verified TASK source rows.

The current unknown-first projection emits 5,669/5,645 semantic anchors and
543/519 Loop Tree nodes. Of those anchors, 2,550/2,530 are preserved
unclassified task events. Older paper snapshots reported 3,119/3,115 anchors
and 157/155 nodes because that policy filtered rather than preserved those
events. The exact 30-unit composition and all 1,110 memberships are unchanged;
the checked artifact deliberately freezes the more conservative current view.

## Size and reduction

The main databases are 15.60 MiB and 23.55 MiB, 39.15 MiB combined. Each has
an 8 KiB numeric `CaptureStreamInfo` companion. This is a deterministic
full-time-range reduction of the retained 24.12/32.07 MiB monolithic inputs:

- every `TASK`, `CANN_API`, and `COMMUNICATION_OP` row is retained;
- dependent compute/communication rows and referenced strings are retained;
- original source table rowids are preserved;
- all original table schemas remain present;
- `HOST_INFO` and `TASK_PMU_INFO` row content is omitted; and
- path-, email-, and identity-like strings are rejected by the verifier.

The per-rank manifests record full/reduced hashes, exact time bounds, copied
row counts, rowid policy, privacy omissions, and companion hashes. Running the
verifier with the optional full-profile paths proves equality of the complete
frozen observation, including the Loop Tree body and exact-evidence hashes.

## Reproduce

```bash
examples/paper_artifacts/tools/verify_ascend_tp2_exact.py \
  --traceloom build/native-tests/native/traceloom
```

Maintainers with the retained full profiles may additionally run:

```bash
examples/paper_artifacts/tools/verify_ascend_tp2_exact.py \
  --traceloom build/native-tests/native/traceloom \
  --reference-device2 /path/to/device2/PROF_... \
  --reference-device3 /path/to/device3/PROF_...
```

The checkout contract proves exact multi-stream reconstruction, direct source
provenance, and communication-cost localization on the normal monolithic path.
The previously verified
monolithic/split storage-layout parity remains an external secondary result;
the split databases are intentionally not duplicated into this repository.
