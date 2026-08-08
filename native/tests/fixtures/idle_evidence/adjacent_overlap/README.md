# Adjacent/overlapping evidence fixture

This controlled fixture freezes E4 boundary splitting and the priority
`visible wait > capture/control > record/runtime > absence`.  Its expected
partition and exact TASK lineage are executable through the golden fixture
matrix test.  It is simulation/model evidence, not runtime evidence.

Rebuild `adjacent_overlap.db` with:

```sh
sqlite3 adjacent_overlap.db < fixture.sql
```
