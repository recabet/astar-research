# Deviations from the frozen protocol

None for the experimental design, inputs, seeds, treatments, counters or
statistical plan.

The only post-run changes were in the analysis/reporting layer:

- a pandas column-selection bug caused the first analysis invocation to stop
  before writing summaries; the raw benchmark CSV files were already complete
  and were not modified;
- figure labels and a summary column (`ns per expanded vertex`) were adjusted
  for readability.

No benchmark was rerun or filtered, and the raw data checksums in
`results/RUN_LOG.md` identify the frozen files used in `RESULTS.md`.

## Post-freeze exploratory extension

After the frozen main run, we added the analyses in `NOVELTY.md`:

- heuristic informativeness diagnostics (reverse BFS, rank correlation and
  normalised error);
- weighted A* on perfect mazes (`f = g + epsilon*h`, epsilon sweep);
- a tail summary of the existing tie-break comparison.

These are exploratory and do not replace or modify the frozen main results
(`results/grid.csv`, `results/real.csv`).  They are reported separately in
`results/novelty.md`, `NOVELTY.md`, and the corresponding report/talk sections.

The same post-freeze extension includes an OpenMP batch-throughput study
(`scripts/run_omp.sh`).  It parallelises independent queries only; the single
search remains sequential.  Its results (`results/omp_scaling.csv`,
`results/omp_tasks.csv`, `results/omp.md`) are reported separately and do not
replace the serial main results.

A final robustness check reruns a matched subset in the host's higher
performance/power mode (`scripts/check_performance_mode.sh high`,
`scripts/analyze_perf_mode.py high`).  There is no separate low-mode subset run:
the frozen main CSVs already contain the matching low-mode rows, and the subset
script keeps the frozen per-band and repeat settings precisely so that the two
can be paired.  This changes no design choice, seed, input or treatment, and the
main raw files are untouched.  Results are reported in `results/perf_mode.md`
and summarised in `README.md` and `RESULTS.md`.
