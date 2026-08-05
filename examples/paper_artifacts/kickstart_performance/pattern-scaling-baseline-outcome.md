# Pattern strong-scaling baseline outcome

Outcome: **PARTIAL; preregistered strong-scaling threshold NOT_REPRODUCED**

The committed preregistration required at least 4.0x eight-thread scan speedup
on four million tokens. The clean Release campaign produced 2.348x, with all
candidate, diagnostic, and summary hashes identical across 90 fresh processes.
It therefore establishes deterministic parallel execution, but not the expected
strong-scaling efficiency.

| Tokens | Partitions | Candidates | 8-thread scan | 8-thread scan speedup | 8-thread scan+reduce speedup |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 100,000 | 25 | 199,949 | 10.093 ms | 1.973x | 1.101x |
| 1,000,000 | 245 | 1,999,949 | 93.313 ms | 2.378x | 1.009x |
| 4,000,000 | 977 | 7,999,949 | 384.960 ms | 2.348x | 1.152x |

The run exposes the actual scalability boundary. At four million tokens, the
one-thread scan takes 903.763 ms, while the global `reduce_candidates` sort
takes 5,867.180 ms and peak RSS reaches about 1.15 GiB. Candidate discovery
materializes almost eight million heap-owning occurrence rows and then globally
sorts them even though they collapse to only 200 distinct candidates. More
threads cannot repair that serial amplification.

The result changes the engineering decision: preserve the owned-range/halo
correctness contract, but move reduction into partition-local map work and
merge compact deterministic summaries. The baseline receipt remains immutable;
an optimized path requires a separately preregistered follow-up rather than
rewriting this result.
