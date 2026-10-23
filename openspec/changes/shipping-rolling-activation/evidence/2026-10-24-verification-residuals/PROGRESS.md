# Verification residuals — 1.2 progress (2026-10-24)

## 2.2 — derived-deadline fixture: LANDED

New registered case `test_f2_the_derived_handoff_lead_is_the_oracle_on_short_and_long_loops`
(`test_runtime_working_set_ownership`). It evaluates the contract's own derived lead (source grains, denied-turn
allowance, phase advance) on the **64-bar** and the **2-bar** loop in the same shape: the long loop fits the lead in
one occurrence; the short loop's widest single pass is shorter than the lead, so its production entry positions the
demand later (`test_3_15f_c_short_loop_is_admitted_at_a_later_handoff`). It also pins that tempo scales the wall time
(120 vs 240 BPM) while the tick lead is unchanged — a fixed-millisecond oracle would disagree. The production-entry
boundary remains `test_window_owner_declares_its_caps_and_the_handoff_margin_is_the_oracle`.

**Red-proof:** replacing `handoffLeadTicks` with a fixed `loopLengthTicks / 8` turns **28** lead-dependent tests red
(including the 3.7h positioning, the 3.10 declared-envelope boundary and the demoted-slot length sweep), restored
byte-identically; focused suite **240/240**.

## 2.4 — commit-edge capacity/inline-build mutation: LANDED

The S4b.7 helper now asserts, **after** the clock lane, that the demoted target's runtime is still unallocated
(`the clock lane may not take the target's occupancy (F-4: no capacity on the edge)`). With
`prewarmPlaybackForSlot(targetSlot, true)` inserted on the commit edge, **28** tests fail, including both
`test_s4b_7_*` cases; the mutation is restored byte-identically and the focused suite is **240/240**. The earlier
`ensurePlaybackMergedEventsForSlot` inline-build variant was a no-op (no runtime = no build), which is why the
capacity form is the discriminating mutation.

## Remaining in 1.2 (not claimed)

- **2.9** — retained-source hold/release matrix; `retainPlaybackSourceForSlot` still has no production caller, so the
  owner must decide whether the matrix is test-driven or a retainer consumer is added.
- **4.1** — content-level undo parity through the existing pipeline + the parity-consumer decision
  (`Loop::checkpointParity()` still has no caller).
- **4.2** — content-divergence oracle and the repair choice (persisted provenance vs bounded load reconciliation).
- **2.8** — consolidated red-proof accounting; 2.2/2.4 are recorded above, 2.9 is pending.

No 1.2 checkoff; only 2.2 and 2.4 are claimed.
