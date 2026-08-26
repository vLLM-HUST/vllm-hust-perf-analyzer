# Clock calibration boundary

TraceLoom contains a provider-neutral affine clock fitter for explicit paired
markers. It recovers the robust Theil--Sen fit, deterministic every-fifth
holdout, residual accounting, uncertainty bound, and half-even timestamp
mapping originally developed by Luqhhh on
`feat/host-device-clock-calibration` in commits `5c2e4e1`, `3b659e9`,
`f1a1dc8`, and `21cefbd`.

The recovery deliberately does not restore the retired idle/gap-analysis
product around that fitter. The public inputs are instead bounded pairs of
source- and target-domain timestamps with stable marker IDs and target-domain
uncertainty. This keeps the reusable clock mechanism independent of any one
gap explanation.

## Evidence gate

An affine-looking fit is not enough to establish a shared clock. The caller
must name the marker contract and state whether marker identity was validated
independently of timestamp proximity:

- a validated explicit marker contract yields `calibrated`;
- a synthetic fixture yields `synthetic_only`;
- structural or ordinal pairs whose identity has not yet been accepted yield
  `candidate_only`;
- missing, duplicate, too few, or unbounded observations fail closed as
  `invalid`.

Candidate-only models report drift and holdout residuals for investigation but
cannot move production timestamps. This is important for distributed traces:
matching collective events by nearby timestamps and then using those matches
to prove clock alignment would be circular. Structural correspondence may
produce candidate marker pairs, but a cross-rank display must keep its original
timestamps until an independent marker contract is accepted.

The current distributed Perfetto export therefore still uses its explicitly
labelled display-only first-event translation. A later calibrated export may
consume this fitter after capture and persistence of an auditable marker
receipt are implemented.
