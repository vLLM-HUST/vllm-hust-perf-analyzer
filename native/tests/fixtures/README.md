# Native test fixtures

These fixtures are part of TraceLoom's test contract and live with the code
that consumes them. Tests must not depend on a sibling research checkout or a
worktree-relative `drafts/` directory.

The protected-sequence fixtures exercise candidate ownership and semantic
boundaries. Their partition halo must be at least
`candidate_max_len - 1`; an undersized halo would make a parallel scan
deterministic but incomplete and is rejected by the production scanner.

The ACLGraph fixtures exercise fixture parsing, native-IR adaptation, and the
legacy compatibility materializer. They are test inputs, not a second product
model or a published report format.
