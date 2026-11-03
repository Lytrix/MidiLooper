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

## 9. Owner follow-up (2026-11-03)

64 ticks is **not** an enforceable outer-loop guarantee and is not promoted to DEC-147.

### 9.1 Can the three view roles prepare N+1 while N-1 serves?

**Lifecycle (code, `RuntimePlaybackViewLifecycle`).** Three views rotate over `current`, `ready`, `build`, `idle`
and an optional `retained`.
- `handoff` moves `ready` to `current` and parks the retired view in `idle` with `published = false`, so the tick
  never reads it again.
- `publishCompletedBuildAsReadySuccessor` refuses while `ready` holds a complete view ("one real successor at a
  time").
- A complete `build` survives a handoff and is published next. So with all three views free, N-1 / N / N+1 fit in
  `current` / `ready` / `build` **with no new storage**.
- The predecessor is safe to consume: it is only reset when it becomes `build`.

**Occupancy as measured** (host, routed steady state with the probe's one-span requester, every 128 ticks):
- **`retained` holds view 1 for the whole run.** Views 0 and 2 alternate between `current` and `ready`, `build` is
  empty, and each successor is complete within 128 ticks of its handoff.
- **The retained view is empty:** `complete=0`, coverage `0+0`, no source. Roles while STOPPED after the Record stop:
  `cur=1 ready=- build=0 idle=2 ret=1`.
- It is the Record stop's return window. Its metadata was reset (`resetMetadataPreserveView` keeps the role marker
  on the preserved index), and nothing drops `retained` once its view holds no coverage.

**Conclusion.**
- Two-span lookahead fits the existing three views **only if that stale `retained` marker is released**.
- Release condition: `retained` names a view with no complete coverage, or one whose owner has retired.
- It also needs the frame to track a **second outstanding demand** (`successorState` / `rollingSuccessorKey` hold
  one). That is frame state, not a view role or lifecycle, but it is new state, so it is the owner's call.

### 9.2 One-span vs two-span envelope (`g = 64`; `L - g` = 1,472 vs 3,008)

Bound: `(G - k) * a + k * X <= L - g` for `k` stalls of length `X` inside one lead.

| case | `a` | `X` | max `k`, one-span | max `k`, two-span |
|---|---|---|---|---|
| sparse `G = 7` | 64 | 211 (wrap Clock) | 6 | 16 |
| sparse `G = 7` | 64 | 536 (post-stop MIDI, pre-247/248) | 2 | 5 |
| dense `G = 24` | 46 | 211 | 2 | 11 |
| dense `G = 24` | 46 | 536 | **0** | 3 |
| dense `G = 24` | 64 | 211 | **infeasible even at `k = 0`** (1,536 > 1,472) | 10 |
| dense `G = 24` | 64 | 536 | infeasible | 3 |

- Two-span lookahead makes the dense case feasible at the declared 64-tick cadence, with up to 3 of the worst
  measured stalls per lead.
- One-span cannot hold a dense window at 64 at all. It needs `a <= 61` with almost no stall.
- Neither form becomes a guarantee until `a_outer` is **declared and latched**. Two-span widens the margin; it does
  not replace that declaration.

### 9.3 The real minimum adoption margin

- Placement of an already-complete window needs no grant. It is decided in the lane turn that observes the
  completion, and the clock advances at most `kPlaybackViewPermittedAdvancePerTurn` (64) within one turn.
- **The real minimum is one permitted advance, 64 ticks.** It is not one bar, and not `a + g`. Its equality with `g`
  is coincidence: `g` absorbs a *grant's* lateness, while the margin covers the *placement turn's* advance.
- Carrying one bar forward would refuse eligible boundaries. A stop 600 ticks before the boundary leaves about 544
  ticks after completion, which is eligible.

### 9.4 Guards strengthened

- Both earlier guards were **vacuous**. Under production pricing the cold frame's first window always covered bars
  0-1 for those stop phases, so the window they guarded was never built.
- `test_c5_3_completion_placement_never_claims_a_consumed_boundary` now stops at phase 2,000 and **asserts** that a
  complete current-revision bars 2-3 window exists while traversal 3's bars 2-3 are still playing (`CompletedWindowWitness`).
  It passes: not adopted mid-interval.
- The margin near-miss cannot be produced behaviourally with the existing host knobs: it would need a completion
  delayed by hundreds of ticks per grant, and `denyPerGrain` is at most 255. The vacuous version is removed. The
  margin guard becomes a direct test of the placement predicate, written with the implementation.

### 9.5 Gates (unchanged, independent, emitted MIDI, no named silent gap counts)

| gate | RED | flag |
|---|---|---|
| steady-state continuity | `test_c5_3_steady_state_is_continuous_under_production_pricing` | `STEADY_STATE_RED` |
| stopped-state launch | `test_c5_3_cold_start_transport_start_plays_the_first_traversal` | `ROUTED_AUDIBILITY_RED` |
| post-overdub-stop coverage | `test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop` | `ROUTED_AUDIBILITY_RED` |

Plus the placement RED (`COMPLETION_PLACEMENT_RED`) and the consumed-boundary guard, which is in the default gate.

## 10. Slice 1: orphaned `retained` role (owner ruling 2026-11-03), fixed

**Invariant.** A physical view keeps a role only while it has a legitimate owner or a qualified serving or return
purpose.

**Mechanism of the defect.**
- Every legitimate `retained` view is complete: `installPreparedCurrent` and `replaceRetainedWithReady` require it,
  and `handoffToRetained` refuses otherwise.
- Several paths empty a view in place without touching roles; for example the STOPPED oracle path's
  `current.resetMetadata()` runs while `current == retained`.
- The runtime reset then preserved whatever `retained` named, whatever its content, and `resetMetadataPreserveView`
  re-stamped `lifecycle.retained = keep`. So after a Record stop, view 1 stayed `retained` and empty for the rest of
  the run.

**Fix.**
- `RuntimePlaybackViewLifecycle::releaseOrphanedRetained` drops a `retained` marker whose view holds no complete
  coverage. The view joins `build` or `idle`, or stays `current` unpublished; roles stay distinct.
- `LoopPlaybackRuntime::releaseOrphanedRetainedView` calls it only when no Record-side owner remains: no bound return
  window for a pending job, no frozen request, no coverage-owned build.
- It is called from the rolling lane turn (`serviceRollingPlaybackWindow`, cold) unless the track is recording.
- The runtime reset no longer preserves an emptied retained view.

**Host.**
- RED: `test_c5_3_orphaned_retained_role_is_released` (`retained` = view 1, complete=0, no owner) now GREEN.
- Guard: `test_c5_3_a_valid_retained_record_bar_stays_protected` asserts it observed a complete retained Record bar
  while recording, so it is not vacuous, and that roles stay distinct. It passes.
- The four gated REDs are unchanged (steady state, placement, launch, post-stop), so the fix masks none of them.
- Native full: 3,984 / 0 failed. Both images are within the RAM1 budget.

**Next (slice 2).** The two-span frame RED: N-1 / current, N / ready, N+1 / build coexist on the three views now
free, N+1 survives the N-1 -> N rotation, a valid retained window refuses the second lookahead safely, and every
expected MIDI event is emitted under production pricing.

## 11. Slice 2: the three-view / two-span lifecycle, structurally (owner ruling 2026-11-03)

Host, `test/test_playback_prewarm/two_span_lifecycle.inc`, real builder (`RuntimePlaybackViewBuild`) and the
existing `RuntimePlaybackViewLifecycle` transitions only:

- **`test_two_span_three_views_carry_n_minus_1_n_and_n_plus_1_through_a_handoff`.** It reaches the state N-1 in
  `current` (published), N in `ready` (complete, unpublished) and the third view free in `build`, and asserts:
  - N+1 begun and left mid-fill; `publishCompletedBuildAsReadySuccessor` refuses ("one real successor"), and N stays
    ready;
  - the N-1 -> N handoff keeps N+1 in `build` with its identity, coverage, fill progress and `storageGeneration`
    intact, and N-1 is unpublished;
  - resumed, N+1 equals a reference build made without a handoff, event for event;
  - published, N+1 is `ready` and unpublished, and the retired N-1 view is the next `build`, reset and never
    published;
  - roles are distinct at every step.
- **`test_two_span_never_steals_a_legitimately_retained_view`.** A complete retained view with events occupies its
  own role, so there is no second-lookahead view (`build` is none) and `releaseOrphanedRetained` refuses it. The
  handoff never recycles or resets it, and the marker survives.
- **Result: both GREEN on the current code.** After slice 1 the existing lifecycle already carries two-span lookahead
  on three views. These are structural proofs, not REDs: no lifecycle change is needed.

### The smallest frame-state change for the second outstanding obligation

**What the frame holds today** (`RollingFrame`, probe requester):
- the serving window: `deadlineTick` (N's start), `startPhase`, span;
- `successorState`: 0 none, 1 demanded, 2 missed, 3 duplicate;
- the successor's key, *derived* by `rollingSuccessorKey` (deadline `deadlineTick + span`, start
  `startPhase + span`);
- the build obligation, carried by the one `pendingViewRequest_`;
- one-span comes from a single requester gate: it refuses while `ready` holds a complete view (gate bit 16).

**Proposal: no new field.** Three local changes:
1. **The key at depth 1 or 2:** `rollingSuccessorKey(frame, depth)`, giving deadline `deadlineTick + depth * span`
   and start `startPhase + depth * span`. `rollingSuccessorDemandIs` accepts either key.
2. **The requester gate:** when `ready` holds a complete view whose coverage equals the depth-1 key, open the depth-2
   demand instead of refusing. It is still the single `pendingViewRequest_`, the single builder and the single
   `build` view; the depth-1 window is already complete, so only one obligation is ever being built.
3. **Promotion keeps the obligation:** when N (depth 1) is promoted to serving, the frame's `deadlineTick` advances by
   one span, so the outstanding depth-2 demand **becomes** the new frame's depth-1 key without being re-stamped.
   `successorState = 1` must survive that promotion instead of resetting to 0.
   - The reset is the successor handoff in `src/Track/TrackPlaybackWindowBuild.cpp`
     (`frame.successorState = 0; // N+1 is now the served window; its own successor is demanded afresh`).
   - Two-span keeps it at 1 when `pendingViewRequest_` already carries the new frame's depth-1 key.

All three are inside the existing successor requester (`PLAYBACK_ROLLING_SUCCESSOR_PROBE`), keyed by the same
absolute deadlines the cancel and missed paths already use. No new lifecycle, view or worker, and no admission change.
