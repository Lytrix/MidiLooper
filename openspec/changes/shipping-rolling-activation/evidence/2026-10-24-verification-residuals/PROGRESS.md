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

## 2.9 — retained-source hold/release matrix: LANDED (test-driven)

New registered `test_retained_source_holds_a_paused_build_across_a_replacement_pass` (`test_runtime_working_set_ownership`):
retain a revision, admit a window build and pause it after one grain, replace the committed pass with different content
(the revision moves), attempt release (refused: the paused build still needs the source), resume to completion, and
compare the served window's interval rows to the **old** revision's effective content element-for-element. Combined with
the existing W-14…W-17 fixture (roles/pins/owned bodies, root replacement, pass reclamation, the reset path, the
served-window hold, release after a newer window), this covers the matrix's sequence. **Owner decision recorded:** the
matrix is test-driven; a production retainer consumer is not added (`retainPlaybackSourceForSlot` remains without a
production caller), and that consumer is the activation change's follow-up if the shipping route needs it.

## 4.1 — content-level undo parity: LANDED (exact root identity)

The existing `test_checkpoint_parity_names_the_lagging_case_and_ignores_the_windows_lag` now also asserts that the undo
installs the **exact root object the entry named** (`publishedSnapshot().get()` identity), not a re-derived one, in
addition to the coverage it carried and the named `OwesReconciliation`/never-current lagging case. **Owner decision:**
`checkpointParity` remains an oracle (no production consumer in the undo path); the 4.2 load path will be its first
production consumer per the design. The stronger event-set comparison through the 4.3 content pipeline belongs to the
4.2 oracle, where it is specified.

## 4.2 — full design written

[`2026-10-24-4.2-design/DESIGN.md`](../2026-10-24-4.2-design/DESIGN.md): the content-divergence oracle (production
writer + restore edge; mutate committed MIDI content, not the `nextNoteId` counter), the two repair options (persisted
provenance vs bounded load reconciliation), and the recommendation: **bounded load reconciliation** — compare the
persisted root's content to the loaded passes at the restore edge, name `OwesReconciliation` and rebuild/stale rather
than presenting a diverged root as current; no LSV1 format change. Implementation is the next slice.

**Still open in 1.2:** 4.2 implementation (oracle + repair), and the 2.8 consolidated red-proof accounting for 2.9/4.1
(2.2/2.4 are recorded).
