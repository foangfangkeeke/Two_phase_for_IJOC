# Paper Writing Rules

This file records the collaboration workflow for the manuscript.
It does not automatically execute itself.
At the start of a new manuscript session, the active project instructions or the user must point the agent to this file.

## Startup

- Read this file before manuscript work whenever it is available in context.
- If `management-science-writing` is explicitly invoked, read that skill before acting.
- Before editing, state the task understanding, plan, and whether files will be modified.
- Use Chinese for planning when useful; write formal manuscript text in English unless requested otherwise.

## Section Workflow

For each section, proceed in this order:

1. Identify the section's role in the whole paper.
2. Propose subsection divisions and responsibilities.
3. After approval, plan one subsection at a time.
4. Propose the paragraph structure for that subsection.
5. After approval, summarize each paragraph as a compact narrative chain, such as A -> B -> C, so the paragraph's internal argument is visible before formal drafting.
6. After approval, draft the formal text.
7. Check cross-section consistency and compile when needed.

## Target Style

- Target the manuscript toward Transportation Science or IJOC-style algorithm writing unless the user says otherwise.
- Emphasize the algorithmic contribution: exact reformulation, decomposition logic, valid lower bounds, valid cuts, and convergence.
- Keep baseline methods concise; do not turn standard baselines into major theoretical results.
- Avoid defensive positioning prose. Do not write sentences whose main purpose is to say what a component is not, such as "this is not a contribution," "cut forms are not part of the model definition," or "we use this only as a benchmark," unless the distinction is necessary to prevent a concrete misunderstanding.
- Prefer affirmative functional statements: state what the component does, what variables or constraints it contains, what limitation motivates the next section, or what evidence it provides.
- Use propositions/theorems for core algorithmic claims, not for every standard modeling observation.
- Prefer short proof sketches in the main text and move long technical proofs to the appendix when needed.
- In method sections, each paragraph should serve one of four roles: define the algorithmic object, state why it is valid, explain what computational advantage it creates, or connect to the next algorithmic component.
- For Phase 1, state the two benefits compactly: relaxing shared plug-in fast-charger capacity coupling decomposes the subproblem by route, and the resulting smaller LP subproblems provide dual cuts.
- For Phase 2, emphasize that exact mixed-integer verification restores equivalence with the lifted formulation, the baseline decomposition, and the extensive form when the cut loop converges.
- Avoid long prose transitions; use equations, formal statements, and concise explanatory sentences.
- For framework drafts, keep all needed formulas, propositions, proof sketches, and algorithm boxes, but make non-formula paragraphs concise narrative-chain drafts rather than polished journal prose.
- Framework drafts may use explicit transition sentences or short roadmap paragraphs to make the intended section logic visible during discussion.
- Final manuscript drafts should remove section-by-section roadmap prose unless the target-journal reference papers use it in the same location; replace it with natural experiment-design or argument-progression sentences.

## Planning Files

- Use `manuscript/SECTION_LOGIC.md` as the global paper map.
- Keep `SECTION_LOGIC.md` at the chapter/subsection level: section role, subsection responsibility, key narrative decisions, open experiments, and todo items.
- Do not use `SECTION_LOGIC.md` as a paragraph-by-paragraph writing script.
- Put active paragraph plans in the corresponding `.tex` file as short comments or compact narrative-chain draft paragraphs.
- Remove paragraph-plan comments from `.tex` after the surrounding text becomes stable.
- Keep unresolved data needs close to the relevant `.tex` subsection with `% TODO:` comments when they affect the next writing step.

## Editing

- Model, method, experiment, and discussion files may all be changed when the scope is approved.
- Prefer small, traceable edits over broad rewrites.
- Do not rewrite an entire `.tex` file unless explicitly requested.
- When the user asks to inspect only the next round of edits, stage the current worktree state before making new changes, then leave the new changes unstaged unless asked otherwise.
- If an editing tool or IDE plugin reports an error, stop and explain before trying another editing route.
- Do not put internal planning labels such as `Role`, `Content plan`, or `Expected message` into final manuscript text.
- Record missing experiments and unresolved decisions visibly in the planning notes.

## Evidence and Numerical Results

- Use `experiment_results_2026-07-19.md` as the authoritative numerical source.
- Use `PROJECT_SUMMARY.md` for paper-facing project context.
- Use `handoff.md` when implementation details or past decisions need checking.
- Keep computational-performance claims separate from planning-robustness claims.
- Report time limits, gaps, convergence status, OOM status, and skipped-case logic accurately.

## LaTeX

- Compile from `manuscript/`.
- For routine manuscript revisions, use `pdflatex` twice. Run `bibtex` only when bibliography/citation files change or the user explicitly asks for a bibliography rebuild.
- Do not use `run.bat` for manuscript or code builds unless explicitly requested.
- After compiling, report success or failure, the PDF path, and important warnings or errors.
- In displayed mathematical programs, write constraints symbolically whenever possible. Prefer equation references, indexed equations, domains, or named feasible sets over prose inside `\mathrm{s.t.}`; if clarification is needed, put it in the surrounding sentence rather than inside the constraint block.
