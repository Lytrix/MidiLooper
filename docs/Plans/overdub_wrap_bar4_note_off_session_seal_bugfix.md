# Overdub wrap — bar 4 note-offs become loop-end / wrap-held (session-S seal)

**Status:** Stage 1 host **shipped** (DEC-051). Device seal-at-0 **PASS** [`201956`](../../captures/session_20260901_201956.log), [`204043`](../../captures/session_20260901_204043.log). Stage 2 gated closed (no `0→L`). Stage 3 pairing not opened. Stop hide re-admit **PASS** on `204043` — see [`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](overdub_occupy_same_start_stop_hide_readmit_bugfix.md).  
**Date:** 2026-09-01  
**Kind:** bugfix  
**Parent:** [`overdub_wrap_loop_end_commit_refinement.md`](overdub_wrap_loop_end_commit_refinement.md) (primary fix — seal at loop end, not S)  
**Evidence:** [`193232`](../../captures/session_20260901_193232.log) (bar 4 note-offs, S=640); sibling [`192435`](../../captures/session_20260901_192435.log), [`190847`](../../captures/session_20260901_190847.log)  
**Does not start:** RC2b `applyCapturePlayheadTails` patch; `findOpenPassNote` / gather union surgery; occupy present-at-S changes; OpenSpec `3.5`/`3.6` checkoff

---

## Invariants

### Architectural (Stage 1 repairs this)

**A wrap commit must never cause an event captured in the tail fragment `[S, L)` to be paired with an unrelated event from the head fragment `[0, S)` merely because the pass was sealed at S.**

Session identity remains at **S** (`playheadPhaseTick`). The persistence/commit boundary moves to **loop phase 0**.

### User-visible regression (bar 4 manifestation)

**Bar-4 overdub notes that end inside the loop keep their captured note-off ticks through wrap commit, live paint, and overdub stop — they are not rewritten to wrap-held head ticks (`96`, `432`) nor extended to full-loop span (`0→3072`) unless the performer actually held an open note from tick 0.**

---

## Interval model — `E` and sealed pass shape

This plan does **not** redefine wrap-shaped **consume** `E`. That symbol inherits the parent refinement and [`overdub_wrap_crossing_hold_head_consume_bugfix.md`](overdub_wrap_crossing_hold_head_consume_bugfix.md): **`E`** is the incoming wrap-held note's head end tick; consume window after Stage 2 retarget is **`[start, L) ∪ [0, E)`**.

For **wrap seal persistence** (Stage 1), define **`E_seal`** separately so the child plan does not smear consume semantics onto the commit interval:

```text
S     = playheadPhaseTick (session start; identity only)
L     = loopLengthTicks
E_seal = loop-phase playhead tick when the phase-0 crossing is observed
         (tickInLoop at didPlayheadCrossPhase(prev, curr, 0, L)),
         subject to existing capture/journal boundaries in parent Stage 1

Today (seal at S): one pass spans [S, L) ∪ [0, S) — session-S artifact

After (seal at 0): one sealed wrap pass spans [S, L) ∪ [0, E_seal)

Full-loop wraps (S == 0 or after the first 0 seal): E_seal advances with
the playhead; later seals are at every phase-0 crossing per parent plan.
```

Stage 1 host proof targets **persistence geometry** (no cross-fragment tail-on/head-off pairs invented by session-S sealing). It does **not** assert that flat merge/reconstruct will correctly pair every same-pitch stack.

---

## Debugging boundary

```text
Session identity @ S (playheadPhaseTick)
    ← stay: openOverdubSession, source view, occupy present-at-hold
Wrap seal clock
    ← fix: seal at phase 0 (parent refinement), not at S
Stage 1
    ← proves persistence geometry only
Stage 2
    ← stop finalization / orphan N@0 (RC-D), if gated
Stage 3
    ← same-pitch flat-merge pairing (RC-C), if gated
RC2b playhead tails / draw_src compose rewrite
    ← out of scope until device shows wrap-held pairs gone from store
```

---

## Symptom ([`193232`](../../captures/session_20260901_193232.log))

4-bar loop (`L = 3072`). Overdub started at **S = 640** (`Overdubbing started @ tick 640`). HITL-style grid overdub on track 4.

**Bar 4** = bar index 3 = ticks **2304–3071**.

| Phase | Expected | Observed |
|-------|----------|----------|
| MIDI input (bar 4) | Note-offs at grid ticks (e.g. 2400, 2880, 3024) | `SEVT` shows correct `F` events at 2400, 2784, 2880 |
| After overdub wrap | Linear bar-4 spans in store | `WRAP,2304,96,4,63` — on@2304 paired with off@96 (head fragment) |
| Live paint (`draw_src`) | Same ends as committed | `old,63,2304,2880` → `new,63,2304,432`; `old,69,2352,2400` → `new,69,2352,96` |
| Overdub stop reconstruct | Bar-4 segments preserved | `DNTE,63,0,0,3072` — full loop from unclosed `N@0`; bar-4 rows also wrap-shortened in frame paint |
| Pre-wrap verify | Canonical store | `capture hot stop verify: non-canonical storage (check=2)` — **`LinearNoteOff`** fail before each wrap |

Wrap commits at transport ticks **3712, 6784, 9856, 12928** (= **640 + n×3072**), confirming seal when playhead crosses **S**, not loop end.

---

## Root cause (layered)

### RC-A — Session-S wrap pass spans two loop fragments (primary)

`Track::commitOverdubWrapAtSessionStart` seals when the playhead crosses **`playheadPhaseTick` (S)**, not phase 0.

Each sealed overdub pass contains **`[S, L) ∪ [0, S)`**. With **S = 640**:

- Bar-4 note-ons (`N@2304`) sit in the tail fragment `[640, 3072)`.
- Earlier fragment offs (`F@96`, `F@432`, …) sit in `[0, 640)`.
- After tick-sort, reconstruct pairs tail ons with head offs as **wrap-held** (`off < on`), e.g. `2304→96`, not `2304→2880`.

This is the same mechanism described in [`overdub_wrap_loop_end_commit_refinement.md`](overdub_wrap_loop_end_commit_refinement.md). [`193232`](../../captures/session_20260901_193232.log) is a clean **S = 640** instance with bar-4 grid notes as the visible symptom.

### RC-B — Live paint follows corrupt capture preview (secondary, display)

`draw_src` compares committed resolve (`old`) vs painted frame (`new`). Committed still shows `2304→2880`; paint shows `2304→432`. Overdub disables wrap continuation in `applyCapturePlayheadTails` (`allowWrapContinuation = false`); bad ends come from **capture preview / store reconstruct**, not playhead-tail extension.

Do **not** patch tails until device shows store no longer contains invented wrap pairs.

### RC-C — Same-pitch stack pressure in flat merge (amplifier — Stage 3)

Note 63 in flat `SEVT` dump:

```text
N@0 (×3)  →  F@96 only     → 2 opens at tick 0
N@2304 (×3) → F@2400 (×1) → 2 opens at 2304
N@2640    → F@2784 (×2), F@2880
```

Grid overdub + layered record content yields multiple same-pitch ons per tick. LIFO pairing assigns later offs to the wrong stack entries. **Amplifier**, not primary RCA — visible in input/merge shape before seal geometry is fixed. Stage 3 only if persistence geometry is clean but pairing remains wrong.

### RC-D — `finishOpenNotes` at stop extends orphan opens (Stage 2)

`finishCanonicalSpansOpenNotes` extends still-open notes to `loopLength` when `finishOpenNotes = true`. Two unclosed `N@0` for pitch 63 → `DNTE,63,0,0,3072`. That is **not** “bar-4 off moved to loop end”; it is orphan opens at tick 0 finished to loop boundary. Stage 2 only when formally gated below.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO for Stage 1 — same owners: `Loop::shouldCommitOverdubWrap`, `Track::maybeCommitOverdubWrap`, `Track::commitOverdubWrapAtSessionStart`. |
| **State transition change?** | YES for Stage 1 — wrap seals at phase **0**, not **S**. First seal earlier when S ≠ 0; **O:** increments at loop-end seal. This is the substantive Stage-1 change. |
| **Unbounded interval owner?** | NO — same wrap commit, journal caps, extract of open ons. |
| **Formal trigger?** | YES — timing model (when wrap seals) + DEC-038 wrap-persist amendment. |
| **Reuse** | YES — change `didPlayheadCrossPhase` target from S to 0. No new domain noun. |

Stage 2 needs its own checkpoint if opened. Rename is **cleanup after** semantic change + tests — not a correctness gate.

---

## Fix strategy

| Stage | Proves | Owner |
|-------|--------|-------|
| **1 — persistence geometry** | Seal at 0; no session-S cross-fragment pairs in stored passes | Parent refinement Stages 0–2 |
| **2 — stop finalization / orphans (RC-D)** | `finishOpenNotes` / excess `N@0` only if gated | Conditional |
| **3 — same-pitch pairing (RC-C)** | Flat merge LIFO under `N@2304×3` / `F@2400×1` only if gated | Conditional |

**Do not** combine RC-B/C/D with Stage 1. **Do not** patch tails, `findOpenPassNote`, or gather union in Stage 1.

---

## Stages

### Stage 1 — Loop-end wrap seal (parent refinement)

Implement parent plan Stages 0–2. Implementation order within Stage 1:

```text
1. Semantic change (shouldCommitOverdubWrap / sealedAtTick / suppress at 0)
2. Native tests (boundary cases below)
3. Rename commitOverdubWrapAtSessionStart → commitOverdubWrap
4. Compile / search stale references
```

#### Stage 1 — host assertions (persistence geometry)

Prove the **seal trigger and sealed pass provenance**, not full reconstruct pairing. Do **not** invent new instrumentation solely to prove cross-fragment pairing mechanics if existing fixtures cannot inspect per-pass membership — frame assertions around what the test infrastructure can observe:

```text
The first sealed pass is the pass produced by the phase-0 crossing,
not the earlier S crossing, and contains the expected tail/head
capture fragments for that phase-0 boundary.

sealed interval provenance:
    [S, L) ∪ [0, E_seal)

If per-pass event inspection is available in the fixture:
    no cross-fragment tail-on/head-off pair created merely by
    session-S sealing (on in [S,L) with off in [0,S) from old clock)
```

`WRAP` / `2304→96` remains primarily a **device regression** check, not host Stage-1 sole proof.

`shouldCommitOverdubWrap` boundary cases (after arm):

```text
prev=3071, curr=0     → true   (primary trigger)
prev=0,     curr=1     → false  (off-by-one past phase 0)
prev=S-1,   curr=S     → false  (negative regression: cross S no longer seals)
prev=639,   curr=640   → false  (193232 S=640 instance)
```

Do **not** use `N@2304 + F@2880 linear; no 2304→96` as the Stage-1 **unit of proof** — that is expected **device regression** evidence, not proof that same-pitch pairing is solved (see RC-C).

#### Stage 1 — device regression ([`193232`](../../captures/session_20260901_193232.log), S=640, L=3072)

1. First `Overdub wrap committed` aligns with loop end (phase 0), not tick 640.
2. No `WRAP,2304,96,4,63` (or bar-4 on + head-fragment off) after wrap 1.
3. Pre-wrap `capture hot stop verify` does not log `check=2` (`LinearNoteOff`) for the pass about to seal.
4. `draw_src` bar 3: no `new,63,2304,432` when committed shows linear end ≥ 2400.
5. **Regression expectation:** bar-4 notes such as `2304→2880` remain linear through wrap — observed on device, not required to pass host Stage-1 seal fixtures.

### Stage 2 — Stop orphan opens / RC-D (conditional)

**Opens only when all are true:**

1. Stage 1 seal-at-0 passes (host + device gates above).
2. Stop still emits full-loop span for pitch 63 (`DNTE,63,0,0,3072` or equivalent `0→L`).
3. **`SEVT` proves excess `N@0` remains unmatched** (e.g. `N@0 (×n)` with fewer than `n` matching `F` in `[0, S)`).
4. The unmatched `N@0` is **not** attributable to an intentionally held note from phase 0 (performer held across loop boundary).

If 2 is true but 3/4 fail, treat as pairing or other RC — do not open Stage 2.

Work: pin record pass vs occupy re-append vs wrap extract; minimal stop/finish policy — not full `validateAndCleanupMidiEvents` on stop ([`Loop-MIDI-Storage-And-Validation`](../../.cursor/rules/Loop-MIDI-Storage-And-Validation.mdc)).

### Stage 3 — Same-pitch pairing / RC-C (conditional)

**Opens only when:**

1. Stage 1 persistence geometry is clean (`WRAP` / `draw_src` show no session-S artifact pairs).
2. Bar-4 ends are still wrong on device **or** `SEVT` still shows stack pressure (`N@2304×3`, `F@2400×1`, etc.).
3. Stage 2 gate is **not** satisfied (otherwise orphan stop path owns the symptom).

Investigate pass-boundary pairing vs flat `mergeActiveCapturePasses` order. Out of Stage 1 scope.

---

## Tests (host)

### Stage 1 — seal clock (required)

| Fixture | Assert |
|---------|--------|
| `shouldCommitOverdubWrap(3071, 0)` after arm | true |
| `shouldCommitOverdubWrap(0, 1)` after arm | false |
| `shouldCommitOverdubWrap(S-1, S)` when S≠0 | false (negative: cross S) |
| S=640: `shouldCommitOverdubWrap(639, 640)` | false |
| S=0: arm after leave, cross 0 | true (parity) |
| Undo suppress | next **0** crossing consumed |

### Stage 1 — sealed pass geometry (required)

| Fixture | Assert |
|---------|--------|
| S=640, L=3072, first wrap | Pass produced by **phase-0** crossing (not S crossing); sealed interval `[640,L)∪[0,E_seal)` with expected tail/head fragments at that boundary |
| Per-pass inspection (if fixture supports) | No `isWrappedLoopNotePair(on, off, L)` where `on ∈ [640,L)` and `off ∈ [0,S)` from session-S sealing artifact |
| Empty S→0 remainder | parent empty-wrap rule (no pass, no **O:**) |

### Device regression (not Stage-1 host proof)

| Fixture | Assert |
|---------|--------|
| 193232 bar 4 | `2304→2880` linear through wrap 1; no `2304→96` in `WRAP` lines |
| `draw_src` | committed `2304→2880` equals painted end (no `432`) |
| Stop `DNTE` | no `0→3072` unless Stage 2 gate satisfied |

Suites: `test_overdub_source_view`, `test_pending_note_change`, `test_loop_take_survival`, optional `test_overdub_wrap_bar4_session_seal`.

Gate: `pio run -e teensy41-capture-serial` (RAM1 locals ≥ 4000), then `pio test -e native`.

---

## Log anchors ([`193232`](../../captures/session_20260901_193232.log))

| Marker | Line (approx.) | Meaning |
|--------|----------------|---------|
| `Overdubbing started @ tick 640` | 7323 | S = 640 |
| `Overdub wrap committed @ tick 3712` | 9206 | First seal at S cross (= 640+3072) |
| `draw_src,diff,bar,3,id,77,old,63,2304,2880,new,63,2304,432` | 9987 | Paint shortens bar-4 end |
| `WRAP,2304,96,4,63` | 17343 | Stored wrap-held pair |
| `SEVT,N,2304,4,63` (×3) / `SEVT,F,2880,4,63` | 17211–17302 | Input vs store pairing |
| `DNTE,63,0,0,3072` | 17760 | Stop reconstruct full-loop orphan |
| `capture hot stop verify: non-canonical storage (check=2)` | 9205, 11187, … | `LinearNoteOff` before wrap |

---

## Files

Stage 1 = parent refinement file list. Stage 2 additions (if opened):

| File | Change |
|------|--------|
| `src/Utils/NoteUtils.cpp` | stop/finish pairing policy only if pinned |
| `src/Track/TrackDeferredMaintenance.cpp` | stored verification / orphan slice |
| `test/test_overdub_wrap_bar4_session_seal/` | 193232 fixtures |

Docs: [`NOTE_WRAPPING_LOGIC.md`](../../docs/Guides/NOTE_WRAPPING_LOGIC.md) overdub wrap paragraph; parent refinement evidence cross-link.

---

## Explicit non-goals

- Patch `applyCapturePlayheadTails` for overdub wrap continuation.
- Change occupy present-at-S or source-view open at S.
- Rebuild source view at 0 on wrap `beginCapture`.
- Invent Off@0 for held notes at wrap extract.
- Full-loop `validateAndCleanupMidiEvents` on overdub stop.
- Same-pitch pairing fixes in Stage 1 commit.
- OpenSpec `3.5` / `3.6` checkoff.

---

## Approval

**APPROVED / READY TO IMPLEMENT — Stage 1 only** (2026-09-01 final review).

1. Parent [`overdub_wrap_loop_end_commit_refinement.md`](overdub_wrap_loop_end_commit_refinement.md) — DEC-038 seal at 0, **O:** at loop-end seal.
2. This plan as **193232** evidence anchor; Stage 2/3 formal gates above.
3. Rename `commitOverdubWrapAtSessionStart` → `commitOverdubWrap` as **cleanup after** semantic change + tests (not a correctness prerequisite).

Remaining uncertainty is empirical: does seal-at-0 remove bar-4 wrap-held corruption? If yes, RC-A confirmed and RC-B is verify-only. If corruption persists with clean `WRAP` geometry, RC-C opens separately.

Firmware Stage 1 may start under this contract.

---

## Pre-implementation review (codebase-backed)

### Ready

- Owners confirmed: `TrackOverdubWrap.cpp`, `LoopOverdubSession.cpp`.
- `draw_src`: `DisplayManager::logDrawSourceComposeDifferential`.
- `WRAP` / `SEVT` / `DNTE`: `TrackDeferredMaintenance`, `DebugSessionCapture`.
- `E` / `E_seal`: consume `E` inherited from parent; seal `E_seal` defined above.

### Resolved (review 2026-09-01)

| Topic | Decision |
|-------|----------|
| Stage-1 host proof | Persistence geometry only; `2304→2880` is device regression |
| Boundary tests | `3071→0` true, `0→1` false, `S-1→S` false |
| Stage 2 gate | `DNTE 0→L` **plus** SEVT excess `N@0`, not intentional hold |
| Stage 3 | Same-pitch pairing only after clean `WRAP`/`draw_src` |
| Rename | After semantic change + tests |

### Open before coding

1. DEC-038 amendment vs DEC-051 — parent Stage 0.
2. Stage 2 opens only on formal four-part gate (not automatic after HITL).

### Proceed?

**YES** — Stage 1 via parent refinement under this contract.
