# DEC-147 successor feasibility envelope (owner ruling 2026-11-03, before implementation)

Status: derivation for the owner's DEC-147 amendment. No code changes.

Approved direction:
1. Connect the existing rolling-frame successor requester in production.
2. Replace the unconditional 512-ticks-per-grant cost with a bound justified by enforceable candidate-specific
   service.
3. Add completion-aware placement.

Gates, kept separate:
- steady-state successor continuity under production settings;
- stopped-state launch from the first traversal;
- complete post-overdub-stop playback from bar 0.

## 1. Terms

| symbol | meaning | value / source |
|---|---|---|
| `S` | window span | 2 bars = 1,536 ticks (`spanTicks`) |
| `L` | successor lead: `N.end - request tick` | at most `S` when requested at `N.start`: one successor in flight, since `kViewCount = 3` roles are current, ready, build |
| `G` | the window's grants, `G(E) = ceil((2E+1)/64) + 3*ceil(E/48) + 2` (`deriveManifestWindowGrantsFrom`; publication's promotion grant included) | sparse take: 7 (the declared floor); dense device take: about 24 |
| `g` | the scheduler's guard before `N.end` for the last grant (amendment 31) | `kDeadlineSuccessorHandoffGuardTicks = 64` |
| `a_lane` | lateness added by **lane arbitration** once a grant is due | at most one lane turn: amendment 31 forbids withholding a due grant (device: lateness at most 23-46 ticks against due) |
| `a_outer` | longest interval in which the **main loop offers no lane turn at all** (outer-loop availability) | not enforced today. Measured: 40-64 normal, 88 after a restart, 440-536 post-stop before DEC-247/248, about 211 for today's wrap Clock |
| `m` | placement margin: completion to a claimed boundary (completion-aware placement) | owner proposal: one bar, 768 |

Today's admission (`canCompleteWithinLead`) is `G * (H + 1) * 64 + leadTicks + slack`, with `H = 7`, a one-bar
`leadTicks` and slack 128. That is the same shape with every grain charged eight full lane turns (512 ticks) and
896 ticks of fixed margin.

## 2. The bound

A grant needs one lane opportunity. The scheduler spaces the `G` grants over `L - g` and serves each when it is due.
Arbitration adds at most `a_lane`, and the outer loop separates opportunities by at most `a_outer`. Each grant then
costs at most `a = max(a_lane, a_outer)` and the last must land by `N.end - g`:

```
admit successor  iff  G * a  <=  L - g
```

- The promotion grant is inside `G`, so publication needs no separate term.
- The scheduler's lateness is absorbed by `g`; device: margin bottoms at 24 ticks under deny 7.
- A **single** long stall `X` per window (the realistic shape: one wrap Clock, or one MIDI burst) gives a tighter but
  still safe form, `(G - 1) * a + X <= L - g`.

## 3. Evaluation (`L = S = 1,536`, so `L - g = 1,472`)

| `a` (ticks) | where it comes from | sparse `G = 7`: `7a` | dense `G = 24`: `24a` | max admissible `G` |
|---|---|---|---|---|
| 46 | measured lateness bound | 322 ✓ | 1,104 ✓ | 32 |
| 64 | declared per-turn advance / normal gap | 448 ✓ | **1,536 ✗** (64 over) | 23 |
| 88 | worst gap after the restart | 616 ✓ | 2,112 ✗ | 16 |
| 211 | today's wrap Clock (220 ms) | 1,477 ✗ (5 over) | ✗ | 6 |
| 536 | post-stop MIDI input, before DEC-247/248 | ✗ | ✗ | 2 |

Single-stall form, `(G - 1) * a + X <= 1,472`:

| case | `a` | `X` | sum | result |
|---|---|---|---|---|
| sparse | 64 | 211 (wrap) | 595 | ✓ |
| sparse | 64 | 536 | 920 | ✓ |
| dense | 46 | 211 | 1,269 | ✓ |
| dense | 64 | 211 | 1,683 | ✗ |
| dense | 46 | 536 | 1,594 | ✗ |

Reading:
- **Sparse windows are feasible** under any outer behaviour short of repeated multi-hundred-tick stalls inside one
  window.
- **The dense (about 24-grant) window is feasible only if `a <= 61` ticks** (`1,472 / 24`) over the whole lead, or
  with at most one stall of about 211 ticks on an otherwise 46-tick cadence.
  - The measured normal gap (40-64) straddles that limit. Today's wrap Clock alone takes a dense successor over it.
- **Today's formula refuses both** (7 x 512 + 896 = 4,480 > 1,536). That is why production never admits a successor.

## 4. What is enforceable, and what is not yet

- **Lane arbitration: enforceable now.** Amendment 31 makes a due grant non-withholdable, so `a_lane` is at most one
  lane turn by construction, not by measurement.
- **Outer-loop availability: not enforced.** Nothing bounds how long one main-loop iteration may keep the lane from
  being offered:
  - MIDI input bursts, the wrap Clock (now 220 ms) and held-NoteOn extraction (98 ms) are all inside one iteration;
  - the earlier 2.6 s iterations were the overdub NoteOff and wrap costs DEC-247/248 removed.
- The admissible bound may therefore only use an `a_outer` the firmware **declares and latches**. That means an
  outer-iteration deadline in the DEC-108 style: an overrun is reported, and a second consecutive overrun latches a
  named fault. It must never be a measured average.
- The value is the owner's to choose:
  - **48 ticks** admits dense windows: 24 x 48 = 1,152 <= 1,472.
  - **64 ticks** admits only `G <= 23`.
  - Either one requires the remaining realtime work first: the wrap Clock (about 211 ticks) and held-NoteOn
    extraction both exceed 48 and 64 today.
- **This is the coupling the gates make explicit.** Steady-state continuity for dense content depends on the P1
  latency work, not only on the admission formula.

## 5. Guard vs placement margin, explicitly

These are three different quantities. Today's formula folds them into one additive 896.

| quantity | role | proposal |
|---|---|---|
| `g = 64` | inside the successor bound: room after the last grant for its lateness, so promotion happens before `N.end` | keep, as amendment 31 measured it |
| one-bar `leadTicks` | today an additive "room to publish". Publication is the promotion grant already counted in `G`, so adding a bar double-counts it | drop it from the successor bound; keep it only as a request-time floor (`L >= leadTicks`), which is harmless at `L = 1,536` |
| `m` (placement) | a **completed** window claiming an earlier boundary needs only one resolver/lane opportunity before that boundary to reposition and promote: `m >= a + g` | one bar (768) is the conservative start the owner proposed. The derived minimum is `a + g` (128 at `a = 64`). Either satisfies the 56-tick example (1,464 to spare) |

Interaction. A successor admitted under section 2 completes by `N.end - g` at the latest, so it never relies on
placement. Placement only reclaims a boundary that the **request-time** bound had ruled out but the work met early. A
window must satisfy one or the other, and never both counted against the same lead.

## 6. When the available lead cannot satisfy the bound

1. **At request:** the successor is refused by name (`PriceRefused`, the existing probe outcome). It is never
   admitted on hope. `N` serves to its end.
2. **At `N.end`:** the routed slot is in a named lane gap. Amendment 23 keeps it silent, with no fallback, and the
   frame cold-starts the next span, which is priced the same way. Every such case is an audible gap, reported per seam
   (`succ_due`/`succ_seam`). It is never counted as a handoff.
3. **Remedies inside the rules:**
   - (a) **Earlier request, two in flight.** Build N+1 while N-1 serves and N waits in `ready`, using the three
     existing roles; this doubles `L`. It is not yet verified that `retained` is free at that point. This is the next
     host measurement, not an assumption.
   - (b) **Bound the outer loop** (section 4): finish the wrap Clock and held-NoteOn work and declare `a_outer`.
   - A shorter span does not help: `G` keeps its fixed `+2 / +3` per-pass overhead while `L` halves.
4. **Not remedies:** a merged fallback, relaxing source identity, synchronous preparation in Clock, or a new lifecycle
   (all excluded).

## 7. Acceptance, per gate (emitted MIDI, every expected event, several traversals, no duplicates)

| gate | host RED (flag) | device |
|---|---|---|
| steady-state successors under production settings | `test_c5_3_steady_state_is_continuous_under_production_pricing` (`STEADY_STATE_RED`) | routed sweep: `none = 0` on track 8 in steady state; every seam `HandedOff`; sparse and dense takes |
| stopped-state launch | `test_c5_3_cold_start_transport_start_plays_the_first_traversal` (`ROUTED_AUDIBILITY_RED`) | first traversal complete |
| post-overdub-stop from bar 0 | `test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop` (`ROUTED_AUDIBILITY_RED`) | traversal after the stop complete |
| completion-aware placement (part of the steady-state gate's tooling, not a substitute for the stop gate) | `test_c5_3_completion_placement_serves_the_nearest_eligible_boundary` (`COMPLETION_PLACEMENT_RED`), with consumed-boundary and margin guards in the default gate | — |

## 8. Recommendation

- **Implement in the order the envelope allows.**
  - (1) Production successor request plus the section 2 bound with a **declared** `a_outer = 64`. That admits sparse
    windows now and refuses dense ones by name rather than silently.
  - (2) Completion-aware placement with `m = max(one bar, a + g)`.
- **Dense continuity:** make the outer-loop bound (section 4) and remedy 3(a) its own next measurement, before
  claiming the steady-state gate for dense takes.
- **Confidence:** high in the bound's shape (it is amendment 31's own rule). Medium in the dense feasibility until the
  outer-loop bound is declared and the two-in-flight role check is done.
