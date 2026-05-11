# Reproduce

This directory is reserved for paper reproduction entry points.

The intended public contract is:

1. select a documented reference workload;
2. run the workload with the user's native CUDA/Nsight or Ascend/CANN profiler;
3. write profiler output under an ignored run directory;
4. run TraceLoom on that profile;
5. compare generated tables, reports, and augmented timelines with the paper.

Large profile files should stay outside git. Add scripts here only when they
can run in a user's existing accelerator environment without imposing a Docker
or driver stack.
