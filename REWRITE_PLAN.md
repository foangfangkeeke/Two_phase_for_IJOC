# COPT Rewrite Plan

The rewrite uses `codex/copt-solver` only as a behavioral reference. The
`codex/copt-rewrite` branch has no parent commit and receives the implementation
in independently buildable stages.

1. Data, configuration, and executable foundation.
2. Native COPT extensive formulation.
3. Natural integer L-shaped method.
4. Lifted integer L-shaped method.
5. Lifted two-phase framework.
6. Full strengthening modules and hierarchical monotone-cut repair.
7. Experiment scripts, documentation, and final cross-method regression.

Every algorithm commit must reproduce the same instance definition and report
solver status, incumbent, bound, relative gap, and wall-clock time. Decomposition
methods must additionally report phase times, iteration counts, and cut counts.
