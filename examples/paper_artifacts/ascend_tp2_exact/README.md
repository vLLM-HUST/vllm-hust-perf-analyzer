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
  rows supporting launch control and visible graph bodies.

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

The checkout contract proves exact multi-stream reconstruction and direct
source provenance on the normal monolithic path. The previously verified
monolithic/split storage-layout parity remains an external secondary result;
the split databases are intentionally not duplicated into this repository.
