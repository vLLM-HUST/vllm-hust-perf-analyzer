# Serving Design Study Draft

This directory holds the TraceLoom-oriented paper draft and supporting notes
that were originally developed in `ascend-llm-realworkload-prof`.

The draft treats TraceLoom as the main structured trace attribution contribution:

- `traceloom_serving_design_study.tex`: LaTeX source.
- `traceloom_serving_design_study.pdf`: compiled snapshot.
- `references.bib`: bibliography used by the draft.
- `figures/fig_traceloom_structure.png`: TraceLoom structure figure used by the paper.
- `discussions/`: design notes and contribution-shaping discussions.

The workload-analysis repository should keep only the experimental analysis
paper. TraceLoom-specific methodology, semantic anchor attribution, repeated
pattern compression, and trace-level optimization gates should evolve here.
