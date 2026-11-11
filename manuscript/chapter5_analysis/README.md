# Chapter 5 analysis — 2026-11-08

## Source and scope

- Published results: `origin/codex/final-experiments-2026-11-05@21969663eb908bb8390fdca29a4bad26af675737`, directory `experiments/final_2026-11-07/`. The branch was updated through Git SSH; the current writing branch was preserved.
- The package contains 45 main-table records, four VI records, and 16 R28 R-TPILS/TPILS positions: 65 positions reuse 57 distinct logs and cover 14 cases.
- **All selected outcomes are final, including N10/N20/N40 and both VI pairs. No solver work or result replacement is pending.** The package's `final_acceptance=false` retains a broader historical-archive qualification, rather than an unfinished experiment. Four EF memory-limit runs have no recorded original-problem bounds; these remain missing values.
- Source builds are `7d932b9` (33 logs) and `c29cdc5` (24 logs). Their source difference is confined to search logging in `src/algorithm/lifted_two_phase.cpp`; no model, cut or parameter change was found. They are two executable builds. All eight R28 R-TPILS/TPILS pairs use a matched executable, command settings and input. Each of the two VI pairs is likewise matched except for its switch. Source and solver settings are unchanged between the previous publication `65c331a` and this publication.
- All 57 overlapping result positions have unchanged bounds, statuses, gaps, controller times, log hashes and build hashes. Eight positions were added; one reuses an existing main-table log.
- Main-table time uses controller monotonic wall time. Progress plots and common search windows use the solver's elapsed timer. All are published target-machine measurements. No local performance results are substituted.
- Paper-facing instance labels use `R-S-F-C` (for example, `R28-S5-F50-C4`). Archived case IDs, log filenames, and data directories retain `N` for the scenario count; the table and figure scripts change only their display labels.

## Reproduce

Run from the writing repository root. The 2026-11-08 cleanup moved the complete experiment export to the adjacent support archive:

```powershell
$paperSupport = Join-Path (Split-Path (Get-Location).Path -Parent) 'Lifted-Two-Phase-support/2026-11-08-cleanup'
$paperSnapshot = Join-Path $paperSupport 'experiment_snapshot'
$paperPackage = Join-Path $paperSnapshot 'experiments/final_2026-11-07'
python manuscript/chapter5_analysis/analyze.py --package "$paperPackage" --source-commit 21969663eb908bb8390fdca29a4bad26af675737
python manuscript/figures/chapter5_progress.py --l2p-c4 "$paperPackage/logs/R28-N5-F50-C4_lifted-two-phase.log" --l2p-f10 "$paperPackage/logs/R28-N5-F10-C4_lifted-two-phase.log" --full-f10 "$paperPackage/logs/R28-N5-F10-C4_full.log" --l2p-c3 "$paperPackage/logs/R28-N5-F50-C3_lifted-two-phase.log" --full-c3 "$paperPackage/logs/R28-N5-F50-C3_full.log" --output-dir manuscript/figures
python manuscript/figures/chapter5_vi_progress.py --log-dir "$paperPackage/logs" --output-dir manuscript/figures
python manuscript/chapter5_analysis/plot_charger_visits_r28_n40.py --data-root "$paperSnapshot/data/R28_N40_data"
```

The two R28-N40 plotting scripts are retained; their temporary PNG, HTML, and CSV diagnostics are not retained. The charger-visit script also writes the Section 5.1 figure to `manuscript/figures/chapter5_station_visits.png`.

`manuscript/figures/chapter5_network_map.png` is the author's 2026-11-09 screenshot of the [28 selected services on BusRouter SG](https://busrouter.sg/#/services/945~282~96~984~872~452~183~79~334~990~941~453~41~285~284~871~189~98~335~98M~992~66~870~947~78~333~173~49). It supplies geographic context only; the 493-station, 624-directed-link network counts come from the processed experimental inputs. The website's "226 INTERSECTING STOP" display is a different measure. The manuscript caption credits BusRouter SG, LTA route data, and the OpenStreetMap/Protomaps basemap.

The complete repository export is needed for the input checks. The archived partial export under `history/chapter5_latest_2026-11-08_partial/` is historical material. To reproduce the previous-publication comparison, define `$paperPrevious = Join-Path $paperSupport 'history/chapter5_latest_2026-11-07/experiments/final_2026-11-07/results.json'` and add `--previous-results "$paperPrevious"` to the analysis command.

If a fresh copy is needed, fetch the source and export it to a new directory beside the writing repository. The following example creates a separate export and fails if its destination already exists:

```powershell
git -c core.sshCommand="ssh -o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes" fetch git@github.com:foangfangkeeke/Two_phase_for_IJOC.git '+refs/heads/*:refs/remotes/origin/*'
$paperExport = Join-Path (Split-Path (Get-Location).Path -Parent) 'Lifted-Two-Phase-support/fresh-2196966'
New-Item -ItemType Directory -Path $paperExport -ErrorAction Stop
$paperSnapshot = Join-Path $paperExport 'experiment_snapshot'
New-Item -ItemType Directory -Path $paperSnapshot -ErrorAction Stop
git archive --format=tar "--output=$paperExport/source-2196966.tar" 21969663eb908bb8390fdca29a4bad26af675737
tar -xf "$paperExport/source-2196966.tar" -C "$paperSnapshot"
$paperPackage = Join-Path $paperSnapshot 'experiments/final_2026-11-07'
```

The analysis checks all 137 manifest-listed package files, 365 repository input/configuration hashes, all record/log hashes, explicit terminal bounds, statuses and gaps, eight R-TPILS/TPILS pairs and two final VI pairs. It also checks nested scenario content/probabilities, reconstructs candidate ranking from the archived inputs and independently reproduces the eight published R28 VI checkpoints. It never starts the solver or compiles the manuscript. The resulting numerical evidence and definitions are recorded in this README and `summary.json`.

## Outputs and manuscript placement

All Chapter 5 and computational-appendix tables are written directly in `manuscript/sections/05_experiments.tex` and `manuscript/appendices/appendix_extra_results.tex`.
The analysis script writes numerical evidence only; it does not generate LaTeX table fragments.
Full-precision source records remain in `summary.json`.

| Output | Use |
|---|---|
| `../figures/chapter5_station_visits.png` | Section 5.1 illustration of scheduled visits to five existing plug-in stations across R28-N40 service plans |
| `../figures/chapter5_vi_progress.pdf` | Paired R12/R28 VI trajectories; crosses mark master candidates that violate VI |
| `vi_checkpoint_bounds.csv` | Independently reconstructed VI checkpoints |
| `summary.json` | Full-precision results, certificates, paired search diagnostics and internal input metadata |

`summary.json:instance_description` retains route sets, physical network dimensions, numerical parameters and candidate rankings for verification. Following the user's latest request on 2026-11-09, the chapter opening follows the setup paragraph in Hu et al.'s trilevel bus-line-contracting paper: experimental purpose, benchmark network, appendix pointer, implementation/hardware, solver and GitHub availability. Appendix `app:experimental-setup` now documents the network sizes, nested route subsets, service-plan scenarios, candidate ranking, and operational/cost parameters. The main text retains a brief network description and defines the R-S-F-C code at its first use.

The actual repository is `https://github.com/foangfangkeeke/Two_phase_for_IJOC`; its public visibility was verified through GitHub repository metadata. The paper calls it the accompanying GitHub repository. The selected experiments are preserved on `codex/final-experiments-2026-11-05` at the source commit recorded above. Network sizes and route membership were checked against the published input files; bus/trip ranges were counted over each N5 set, and costs and efficiencies were checked against `globalCfg.txt` and `DwcFacilityCost` in `model_helpers.cpp`. No solver runs or manuscript compilation were performed for this setup revision.

## Definitions

- Phase 1 distance is `100 * (later verified reference UB - Phase 1 LB) / reference UB`. It is retrospective and does not make the relaxation incumbent feasible for the original problem. C2 never meets the 5% threshold in Phase 1.
- The certified true relative relaxation gap lies in `[max(0, 1 - U_R/L), 1 - L_R/U]`, using valid original-problem bounds `[L,U]` and Phase 1 relaxation bounds `[L_R,U_R]`. These certificates are recorded separately from the retrospective distance and each algorithm's own final gap/status.
- Search comparisons use completed Phase 2 master-candidate evaluations by the earlier solver endpoint: 3,046.242 seconds for C3 and 7,200.130 seconds for C2. A final master return used for stopping, or without completed scenario evaluation, is excluded. C3 R-TPILS has 19 master returns but only 18 completed evaluations; TPILS has 258 returns and 257 completed evaluations over its whole run, of which 101 are in the common window.
- A resource configuration is the installed-facility set plus the selected capacity for each scenario and route. Objective proxies and Phase 1 points are excluded.
- Facility change is binary Hamming distance. Battery change counts each route–scenario capacity change once, irrespective of its size. The underlying one-hot Hamming distance is `facility changes + 2 * battery changes`. Both conventions and medians are retained in `summary.json`; the paper table reports the two means separately.
- Repair probes are additional evaluations outside the master-candidate sequence. Fewer master configurations do not imply less total evaluation work.
- Cut-anchor diagnostics compare the anchor with the originating master configuration, counting all added facilities and raised batteries in the repaired scenario. Initial-UB completion repairs are excluded because their starting point can be an already combined repaired incumbent.
- Resource-box coverage counts the configurations below each anchor before intersecting other master constraints: `2^(installed facilities) * product(number of battery options no greater than each route's anchor capacity)`. The expansion ratio compares two monotone cuts at the repaired and original anchors, holding the cut family fixed. Ratios are anchor-level quantities; overlapping or previously excluded boxes are not summed. The exact box counts and ratios are stored in `summary.json:phase_two:<case>:full:anchor_statistics:resource_box_coverage`.
- Checkpoint LB comparisons use the latest timestamped completed iteration, or a final bound available by that time. Search-master JSON has no elapsed timestamp, so its later bound is never assigned to an earlier checkpoint. Final C2 bounds come from slightly different endpoints; the text identifies them as terminal values.
- Component times sum logged timers for completed iterations. Other is the residual, which can include an unfinished final solve. Timing-bar shares use each method's own Phase 2 duration.
- First-UB times refer to the first completed-iteration record containing a finite original-problem UB. The logs do not timestamp its earlier discovery within that iteration.
- For N10/N20/N40, common overall endpoints are 863.432/2950.595/7202.534 solver seconds. N40 R-TPILS's third evaluation completes at 7203.777 seconds, so the common-window search counts two R-TPILS candidates and 254 probes; the complete run counts three and 387. Normalized facility and battery changes divide by F and 28N.
- VI checkpoints use the latest complete-iteration or terminal bound actually logged by the requested solver time. R28 no-VI's terminal LB 5089.153366 is recorded at 7412.264 seconds; its latest recorded LB by 7200 seconds is 4416.490878. Candidate counts can include an incomplete final evaluation, and VI logs do not contain complete master decision vectors.
- In the VI figure, crosses mark only completed no-VI evaluations with `vi_excluded=1`: 19 for R12 and three for R28. Their positions use evaluation-end times and recorded LBs. Each no-VI run records one additional unfinished evaluation without a candidate-visit timestamp; it is counted in 20/20 or 4/4 but receives no inferred cross. The curves retain their final recorded LBs without separate terminal markers.

## Main revisions supported by the new evidence

- EF now ends by memory limit on all three R28 cases; Lifted ILS R28-F50 has a complete time-limit record. Natural ILS R28 LB is explicitly zero.
- Main-table target counts are EF 5/9, Natural ILS 3/9, Lifted ILS 6/9, TPILS 8/9, and R-TPILS 9/9. R12 TPILS times are 28–89 times shorter than Lifted ILS times.
- The same Phase 1 bound at R28-F50 C4/C3/C2 is 0.302%/2.575%/10.133% below the later reference incumbent. The last number also reflects the C2 incumbent's 7.156% gap.
- C3: R-TPILS evaluates 18 distinct configurations versus 101 for TPILS in the common window. Mean facility/battery changes are 4.94/20.12 versus 4.16/10.00. Total-choice-change medians are 26 versus 12. R-TPILS's final LB is 0.154% above the latest completed TPILS LB at the common endpoint.
- C3 R-TPILS: 247 probes, 45 strengthened cuts; repair consumes 36.5% of Phase 2 time. The 45 anchors average 12.18 additional facilities and 5.84 raised battery choices relative to their originating candidates.
- C2: mean total decision change is smaller for R-TPILS (31.44 versus 36.42). Its completed LB is 0.084% higher at 3,600 seconds and 0.047% lower at strictly 7,200 seconds (17987.393018 versus 17995.926269). Its terminal LB is 0.023% lower, using distinct endpoints. TPILS's terminal 18007.233391 is recorded at 7200.130 seconds and is not assigned to 7200 seconds. The evidence does not show a uniform LB-speed or step-size advantage.
- All 20 R12 and all four R28 master candidates visited without VI violate an omitted inequality. R12 VI on reaches the target; R28 on and off both have no finite UB. Filtering and recorded lower-bound progress improve, while exact feasibility remains difficult on R28.
- N10/N20/N40: R-TPILS obtains finite UBs in all three cases; TPILS obtains none. R-TPILS reaches the 1% target on N10/N20 in 866.6/2965.8 controller seconds, and ends N40 by time limit with a 1.080071% gap. TPILS's terminal LBs are slightly higher in all three cases; this is not a same-time comparison.
- Phase 1 first enters the later UB's 5% range in 21.4/46.3/148.6 TPILS seconds, but ends in 31.8/1052.6/1532.2 seconds. N20/N40 do not enter 1% in Phase 1. N10 completes faster than N5. Changing N changes scenario size and equal probability weights; N40 also replaces one of the N5 top-50 candidate links, dropping `(169,30)` and adding `(454,107)`. No pure size effect or monotone runtime-scaling law is inferred.
- In the new common windows, R-TPILS completes two unique master evaluations per N versus 8/3/4 for TPILS. Mean decision changes are smaller for R-TPILS, with only one R-TPILS adjacent pair in each window. The extra evidence reinforces feasibility recovery, without a general search-step or lower-bound-speed claim.

## Planning solution reconciliation

Section 5.4 retains the local exported C4 solution pair and no-DWC C3 EF solution; these supply solution values, never local performance times. `python manuscript/chapter5_local_solution/verify_r28.py` reproduces the C4 cost and route-energy comparison.

On 2026-11-07, all 24 R28 dataset files in `../Lifted-Two-Phase-support/2026-11-08-cleanup/repair-worktree/` matched the final package's case hashes byte for byte. Ignoring line endings, `git diff d030988 c29cdc5 -- src/model` changes only the EF stopping-gap configuration; the original objective and constraints are unchanged. The final C3 R-TPILS bounds and cost decomposition reproduce those already used in the planning comparison. The C3 DWC savings therefore remain 549.44–841.93 USD/day, with an incumbent difference of 671.92 USD/day. They do not establish a monotone capacity-substitution trend because the C3 and C4 intervals overlap.

No manuscript compilation was performed in this revision, following `manuscript/WRITING_RULES.md`.

## Repair exposition revision — 2026-11-09

Section 5.2.3 presents R-TPILS convergence across C/N, a paired F10 overhead example, and paired C3 bound trajectories. In both repair figures, circles (R-TPILS) and diamonds (TPILS) mark reconstructed master-return times on LB curves and recorded incumbent-update times on UB curves. F10 has four TPILS master returns and three R-TPILS master returns; the third R-TPILS return is terminal and does not enter the subproblems. The C3 upper panel plots both methods against elapsed time from their own phase switches. The nineteenth R-TPILS master return supplies the final bound without another evaluation, and the component-time panels show this terminal master-only iteration explicitly. C3 TPILS completes 257 evaluations and has no finite UB. Main-text claims continue to focus on R-TPILS convergence and cut strengthening, with detailed LB/Hamming comparisons retained in the replication materials.

The F10 Phase 2 durations are 318.204 TPILS seconds and 385.330 R-TPILS seconds, including 77.192 R-TPILS repair seconds (20.0%). C3's 45 anchor ratios have minimum 21.6, median 3379.2 and maximum about 3.402e10. All 50 candidate links are used in each C3 scenario, as checked against the source's `UsedLinks` construction. Battery ranks count options up to the selected capacity, so route-specific upper-menu truncation does not alter the count. The full-precision validation records remain in `summary.json`.
