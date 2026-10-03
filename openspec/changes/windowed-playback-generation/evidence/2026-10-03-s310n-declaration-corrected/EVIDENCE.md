# Slice 3.10 (increment 17) — the service-gap declaration is corrected against its own measurement, and pinned so a larger constant cannot hide the gap

**Status:** 2026-10-03. This increment closes the decision the device runs left open. It is a **correction to the
source's claims plus a fixture that pins them**; no admission value changed, because the measurement showed the
proposed repair was the wrong one.

## 1. The decision the evidence forced: neither (a) nor (b)

Increment 15 left a choice: (a) split the constant into a typical service interval + a batched worst step, or
(b) exclude batching from the tick the admission judges. **Run 6 shows both are premised on the wrong term.**
`DIAG,clk_batch` is bounded at **42 pulses = 336 ticks = 44 % of the declared 768**, so batching is *inside* the
declaration and can never have produced the violation. The quantity that exceeds the declaration is **starvation**
(2 138 / 4 355 / 8 520 ticks), which is a run of *lost opportunities* — not a per-turn advance at all, and therefore
not a term any per-turn constant can hold. So:

- `maximumPhaseAdvancePerTurn` bounds the **scheduling** advance (measured 2.9–5.5 ticks per opportunity; the
  declared one bar is an upper bound with ~140–265× to spare) — and it stays as declared;
- the **starvation** component is the lag rule's (`one_pass_late` / `lag_breach`, task 3.6b), which already exists and
  already reports it;
- and **enlarging the constant towards the starvation figure is the wrong repair**, because it would hide a real miss
  instead of closing it. That sentence is now a *fixture*, not a comment.

## 2. Two false claims corrected at the source

**(A) The safety argument.** `playbackViewCoverageContract()` said an unmeasured value *"can only refuse work, never
admit too much"*. Measured, that is false: the starvation advance is 2.8×–11.1× the declaration, so an admitted
window **can** be starved past its handoff. The comment now states which quantity the constant bounds, which quantity
it does not, who handles the miss, and that enlarging it is the wrong repair.

**(B) The conflation, restored.** The same comment claimed `maximumConsecutiveDeniedTurns` is *"the rotation,
`kDeferredIdleOwnerCount - 1`"* — the very conflation `evidence/2026-10-02-s310h-lifetime-fairness-derivation/`
records as an error, because the rotation is **already inside** `maximumPhaseAdvancePerTurn` by that field's unit.
Measured against it: `kDeferredIdleOwnerCount = 7`, so the claim reads **6** while the code passes **3**. The field is
the **within-lane** contention allowance (`H` eligible tracks × `K` sibling slots — `main.cpp`'s offer round-robin),
which is exactly where the earlier record says it belongs, so the comment is corrected rather than the value. The
declared `3` is an allowance, not a proved bound; `H × K` remains task 3.10's work.

The header field docs (`RuntimePlaybackViewBuild.h`) now carry the measured figures and the same two-role reading, so
the contract's own documentation cannot be quoted for the refuted claim.

## 3. The fixture, and its four pins

`test_window_owner_service_gap_declaration_is_bounded_by_measurement_not_by_its_own_claim` (test-only):

1. the declared per-turn advance is an **upper bound on the measured steady state** (≥ 8 ticks);
2. the declared advance **stays below the measured starvation episodes** (< 2 138) — the pin that refuses "just make
   the constant bigger";
3. the tick-denominated allowance's **wall-clock meaning is tempo-dependent**, computed through the clock's own
   `microsPerTick` formula: 768 ticks = **2 000 ms at 120 BPM**, **1 000 ms at 240 BPM**;
4. the turn-count factor is the **within-lane contention allowance, not the owner rotation**.

**The bound of this fixture, stated:** it pins *recorded device measurements* against the declaration, so it can
refuse a declaration that contradicts them; it cannot re-measure the device.

## 4. Red-proofs (every pin made to speak)

Each mutation applied by a guarded `sed` (unique anchor asserted, abort otherwise), reverted from a saved copy, and
verified **byte-identical** (`src/Track/TrackPlaybackWindowBuild.cpp` sha256 `b93fec3e…5970bd` after every run):

| mutation | change | the assertion that spoke |
|---|---|---|
| **M104** | per-turn advance 768 → **7** | pin 1 — *"declared per-turn advance 7 must cover the measured steady state 8 (ticks per opportunity)"* |
| **M105** | per-turn advance 768 → **8520** (the starvation figure) | pin 2 — *"declared per-turn advance 8520 must stay below the measured starvation episode 2138: a starved window's miss is the lag rule's, not this constant's"* |
| **M107** | per-turn advance 768 → **1536** (satisfies pins 1 and 2) | pin 3 alone — *"Expected 2000 Was 4000. the declared allowance is 2.0 s of wall-clock at 120 BPM"* |
| **M106** | denied turns 3 → **6** (the rotation) | pin 4 — *"the owner rotation belongs to the per-turn unit, not to the turn-count allowance"* |

**M107 exists because M105 aborts on pin 2**: Unity stops the case at the first failure, so pin 3 had to be isolated
by a mutation that satisfies both earlier pins. Without it pin 3 would have been an assertion never shown to fail.
**One assertion was removed for the same reason:** the halving relation (`2 × 1 000 == 2 000`) cannot fail while both
absolutes hold, so it is now a comment, not an assertion.

## 5. Verification

- Focused: `test_runtime_working_set_ownership` **77/77** (+1 fixture).
- Full native: **2716 cases / 2709 succeeded / 7 skipped / 0 failed**.
- `pio run -e teensy41-capture-serial` **SUCCESS** (33.6 s — a partial rebuild, because the header changed).
- RAM1 **byte-identical** to the pre-change line: `variables 96,192 / code 390,088 + padding 3,128`, floor 840,
  **2 288 B headroom** ⇒ **no re-base** (the production edits are comments and a doc block, so code size did not move).

## 6. Not claimed

- **No `H × K` contention envelope is declared.** The measured maximum is a *starvation* figure under
  instrumentation, not a scheduling bound, so it cannot be the basis of one.
- **The derived admission stays disabled** (`applyWindowCostToContract` is still not called at the request site), and
  no fixture the derivation refuses was re-based: re-basing them would encode the derived counts as the declaration
  without the envelope that gives them meaning.
- The starvation figure remains an **upper bound under instrumentation**; the production worst case is unknown.
- Task 3.10 stays open on the `H × K` declaration, the wiring, and the systematic grain-cost sweep.
