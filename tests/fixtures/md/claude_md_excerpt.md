   entry in `JOURNAL.md` for open threads.
2. **While working:** stay within the current phase unless the plan says otherwise.
2b. **The plan is a hypothesis, not a contract — rewrite it when the work disproves it.**
   `PLAN.md` was written *before* the work; the work is the evidence. When they conflict,
   assume the plan is what's wrong. You are explicitly permitted — expected — to re-scope
   a phase, move milestones between phases, reorder, split or merge phases, rewrite a
   `Done-when` that turned out unachievable as stated, or add a phase that should have
   existed. Do that instead of working around a mis-scoped plan: a workaround leaves the
   next session inheriting the same bad map plus a detour.
   - **Symptoms that mean the plan is wrong, not the work:** a `Done-when` that cannot be
     satisfied without the *next* phase having started; a milestone that is really a
     multi-session *process* (a review window, a track record) sitting inside a *build*
     phase; a circular dependency between phases; a phase that quietly grew to cover
     unrelated things.
   - **Moving work is free; dropping it is not.** A milestone may be relocated freely, but
     it may never just disappear — if it's genuinely obsolete, leave it struck through
     with the reason, the way resolved Open questions are handled.
   - **Say what you changed and why in `JOURNAL.md`:** what the plan assumed, what the work
     revealed, what moved. A restructure with no journal entry is indistinguishable from
     scope drift, and this convention only works if the diff explains itself.
   - Any phase you newly scope or re-scope **must** carry a model/effort recommendation,
     for the same reason every existing phase does (see Model-choice verification below).
   - **Hard boundary — this license covers the `plan/` file set only** (the top-level
     `PLAN.md` index and every file under `plan/`). CLAUDE.md's "Hard constraints" list
     and the self-edit safety gate (`SelfWork.*`, `PROBATION.md`, `.roll-self`,
     `.githooks/`) are **not** re-plannable under it: they are `invariants`-tier and
     permanently human-supervised (see `PROBATION.md`). An agent free to rewrite its own
     plan must never be able to plan away its own guardrails — that is the whole failure
