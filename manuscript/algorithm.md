# Algorithm Entry Points and Paper Names

This file records the mapping between command-line algorithm names and manuscript-facing method names.
CLI names may stay short for scripts; manuscript tables should use the paper-facing names.

| CLI `--pb` | CLI `--alg` | Paper-facing name | Role |
| --- | --- | --- | --- |
| `wireless` | `solver` | Direct extensive-form MILP | Monolithic deterministic-equivalent benchmark |
| `wireless` | `natural-ils` | Baseline integer L-shaped decomposition | Classical scenario decomposition induced by the original two-stage model |
| `wireless_reform` | `lifted-ils` | Lifted integer L-shaped decomposition | Lifted master without the two-phase relaxation |
| `wireless_reform` | `lifted-monotone` | Lifted integer L-shaped decomposition with monotonicity cuts | Cross-ablation for monotonicity strengthening |
| `wireless_reform` | `lifted-two-phase-no-rsp` | Lifted two-phase method without RSP | Appendix diagnostic for the RSP enhancement |
| `wireless_reform` | `lifted-two-phase` | Lifted two-phase method | Lifted master plus Phase 1 lower bounding and Phase 2 verification |
| `wireless_reform` | `full-no-p1-custom` | Full method without Phase 1 custom cuts | Ablation for Phase 1 custom cuts |
| `wireless_reform` | `full` | Full method | Complete proposed method |

Terminology notes:

- Avoid `natural ILS` in manuscript prose because `ILS` is commonly used for iterated local search.
- Use `integer L-shaped method` or `integer L-shaped decomposition` in formal text.
- Use `direct extensive-form MILP` instead of `solver` in manuscript tables.
- Use `full method` after the proposed method has been defined; use `proposed full method` when contrast is needed.
