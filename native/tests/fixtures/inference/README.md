# Observable inference fixture

`observable.ndjson` is entirely synthetic and contains no captured model input
or output. It exercises a root run, queue wait, parallel execution/retrieval,
model counters and an observation, a failed tool attempt and retry, delivery,
producer drop receipt, and a cancelled root with an unfinished child.
Monotonic timestamps exceed 2^53 to detect accidental float conversion.

Wire contract: `docs/contracts/inference-trace-v1.md`.
