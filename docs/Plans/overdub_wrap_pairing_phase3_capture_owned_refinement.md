# Phase 3 — Capture-owned wrap pairing and live display (revision)

**Status:** Review — architecture verified against guides + codebase  
**Prerequisite:** Phase 2 committed ([`capture_coordinate_canonical_decision_refinement.md`](capture_coordinate_canonical_decision_refinement.md))  
**Evidence:** [`session_20260713_140656.log`](../captures/session_20260713_140656.log) — `storage==proj` OK; `DNTE,12,0,0,136` still wrong

---

## Verification vs architecture / codebase

| Revision claim | Guides / code | Verdict | Refinement |
|----------------|---------------|---------|------------|
| Wrap pairing belongs in **capture** | [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) § capture ownership; [`Playback.md`](../Authority/Architecture/Playback.md) playback read-only | **Agree** | Keep |
| **Reconstruct unchanged** | `NoteUtils::buildCanonicalSpansFromMidi` — pairing only from storage | **Agree** | No reconstruct edits in Phase 3 |
| **Playback read-only** | `playMidiEvents` — no capture append, no `pendingNotes` mutation ([`Track.cpp`](../../src/Track.cpp) ~1707) | **Agree** | Wrap closure hook **must not** live inside `playMidiEvents` |
| Note owns lifecycle; closure at **boundary crossing** | Not implemented — only `finalizePendingNotes` at stop + live `wrappedHeadOff` on NoteOff | **Gap** | Add capture hook (below) |
| **PendingNote** is authority | [`PendingNote`](../../include/Track.h) has 4 fields only; one map entry per `(note, channel)` | **Partial** | Extend state; optional `NoteId` link to capture row |
| No **storage pitch scan** in finalize | Current finalize is blind append by pending key — no store scan (good) | **Agree** | finalize uses pending flags only |
| **seal** uses same frame as capture | [`Loop::sealCapture`](../../src/Loop.cpp) ~1319 still `tickPhaseInLoop(sealedAtTick, startLoopTick, …)` | **Gap** | Step 3 — pass `closeTick` from Track |
| Display renders **active notes before NoteOff** | [`DisplayManager::applyCapturePlayheadTails`](../../src/DisplayManager.cpp) ~278 — playhead + `isWrapHeldOpenNote` | **Partial** | Extend to read pending wrap state; storage canonical after capture fix |
| Invariant: no mid-wrap capture mutation | LOOP guide §3: "No mid-wrap capture mutation" | **Conflict** | **Update guide** — allow capture-owned head `NoteOff` at wrap boundary; playback still forbidden |

---

## Confirmed failure (140656)

Storage tick order:

```text
N@0  …  N@2112  F@2208  (+ F@136 from stop finalize)
```

Sorted: `F@136` before `N@2112`. Open `N@0` remains → reconstruct pairs `F@136` with `N@0` → `DNTE 12,0,0,136`.

Expected canonical wrap: `N@2112` + `F@136` → `WRAP 2112→136`.

Not a coordinate bug (Phase 2 verified 51/51 `storage==proj`).

---

## Architectural invariants (Phase 3)

### Capture owns

- `pendingNotes` lifetime
- Wrap boundary detection (transport phase)
- Head-phase `NoteOff` append at boundary
- Stop finalize only for notes **not** already wrap-closed

### Reconstruct owns

- Storage → `DisplayNote` spans only

### Reconstruct must not

- Invent `NoteOff`, repair capture, infer ownership from pitch order

### Playback must not

- Append capture events or mutate `pendingNotes` ([`Playback.md`](../Authority/Architecture/Playback.md) §4)

### Display (addition)

- Active capture notes are displayable before terminal `NoteOff`
- Live overdub already uses playhead tails ([`applyCapturePlayheadTails`](../../src/DisplayManager.cpp)); after capture writes canonical head off, display should **converge** with reconstruct — pending wrap flags avoid display-only inference where possible

---

## Revised design (codebase-aligned)

### Step 1 — Wrap closure at boundary crossing (not tail NoteOn)

**Replace:** tail `NoteOn` inserts head `NoteOff`.

**With:** pending notes that cross the loop boundary receive canonical head-phase `NoteOff` when the boundary is crossed.

**Hook owner:** `Track` — new method e.g. `processPendingNotesAtWrapCrossing(uint32_t currentTick)`.

**Hook caller:** [`TrackManager`](../../src/TrackManager.cpp) transport loop — **before** `playMidiEvents(playTick, …)` when track is overdubbing/recording+playing. Same tick as playback; separate function so playback path stays read-only.

**Wrap detection:** Reuse [`IntervalProjection::didDisplayPlayheadWrapBackward`](../../include/Utils/IntervalProjection.h) + `loop.lastTickInLoop` / `capturePhaseTick(currentTick)` — mirror logic in [`playMidiEvents`](../../src/Track.cpp) ~1738 without capture append inside that function.

**Per pending note:**

1. If `startNoteTick >= wrapTailStartTick(loopLength)` (note started in tail) OR note was active before wrap (held from head — detect via `startNoteTick < tailStart` and still pending at wrap)
2. If not already `wrapClosed`
3. `headOffTick = capturePhaseTick(currentTick)` (head phase after wrap)
4. `appendCaptureNoteOffAtPhase(channel, note, headOffTick)` — wrapped-head semantics (no monotonic bump)
5. Set `pendingNote.wrapClosed = true`, `pendingNote.wrapHeadOffTick = headOffTick`
6. **Do not** clear pending — note still active until performer release or stop finalize

**Guide update:** Revise LOOP §3 from "No mid-wrap capture mutation" to:

> Playback must not mutate capture at wrap. Capture may append canonical head-phase `NoteOff` for pending notes at wrap boundary (capture-owned lifecycle).

### Step 2 — `finalizePendingNotes` ownership-aware

Before append:

```text
if pending.wrapClosed → skip (head off already in store)
else → append normal finalize NoteOff at capturePhaseTick
```

**No** store scan by pitch/channel. Pending instance is authority.

**Risk to address:** stale `N@0` open in store from grid legato — boundary closure for the **pending instance** must not produce orphan `F@head` that reconstruct assigns to `N@0`. Mitigation:

- Wrap closure tied to pending `startNoteTick` / optional `NoteId` on the NoteOn that opened this pending instance
- When appending head off at boundary, only if pending represents a note that **crossed** wrap (tail start or held-from-head per detection above)
- Test 4 explicitly guards `N@0` + `N@2112` + STOP

### Step 3 — `sealCapture` closeTick alignment

**Approved.** Current drift:

```cpp
// Loop::sealCapture — still legacy frame
openTailCloseTick = tickPhaseInLoop(sealedAtTick, startLoopTick, loopLengthTicks);
```

**Fix:** Track computes `closeTick = capturePhaseTick(currentTick)` at stop (already done Phase 2) and passes it into seal:

- Option A: `Loop::commitCapturePass(reason, sealedAtTick, closeTick)`
- Option B: set `loop.captureCloseTick` before commit; `sealCapture` reads it

Seal must **not** recalculate a different coordinate frame.

### Step 4 — `PendingNote` extension

```cpp
struct PendingNote {
  uint8_t note;
  uint8_t channel;
  uint32_t startNoteTick;
  uint8_t velocity;
  NoteId noteId = kInvalidNoteId;  // optional — matches capture NoteOn row
  bool wrapClosed = false;
  uint32_t wrapHeadOffTick = UINT32_MAX;
};
```

Set `noteId` in `noteOn` from allocated capture id. Avoid ambiguous same-pitch matching.

### Step 5 — Display (minimal Phase 3)

**Existing:** [`applyCapturePlayheadTails`](../../src/DisplayManager.cpp) + [`isWrapHeldOpenNote`](../../src/Utils/NoteUtils.cpp).

**Phase 3 capture goal:** storage contains `F@head` before stop so reconstruct + WRAP lines match without display repair.

**Optional Phase 3b:** `DisplayManager` reads `Track::pendingNotes` wrap flags for head segment when `wrapClosed && !performerOff` — only if capture-only fix leaves live display gap in HITL.

---

## Tests (native, before firmware)

| # | Name | Fixture |
|---|------|---------|
| 1 | `test_wrap_boundary_closes_pending_without_second_note_on` | `N@2112`, advance wrap, STOP → `F@head` exists |
| 2 | `test_active_display_before_note_off` | HITL or host: pending + playhead past wrap → display shows wrapped active (may use DisplayManager helper test) |
| 3 | `test_same_pitch_retrigger_wrap_closure` | `N60@2000`, `N60@2112`, wrap → correct instance gets `F@head` |
| 4 | `test_head_note_on_not_closed_by_wrap_finalize` | `N@0`, `N@2112`, STOP → no `DNTE 0→136` |
| — | Keep | `test_overdub_grid_tail_wrap_pairs_tail_on_not_first_on` |

Add regression fixture from 140656: tail `2112`, head `136`, loop `2304`.

---

## Acceptance criteria

- No false `DNTE,note,0,head` spans when tail wrap intended
- `WRAP,tail,head` in stop verification
- `non-canonical storage (check=2)` reduced or explained
- Reconstruct + playback code paths unchanged (except seal closeTick input)
- Pending note state drives finalize skip
- Phase 2 coordinate alignment preserved (`storage==proj`)

---

## Out of scope

- MO lag / ch4 slot flood
- Reconstruct / `buildCanonicalSpansFromMidi` changes
- Reintroducing `closeOpenNotesAtLoopWrap` in playback

---

## Implementation order

1. Extend `PendingNote` + native tests (red)
2. `processPendingNotesAtWrapCrossing` + TrackManager hook
3. `finalizePendingNotes` wrap-aware skip
4. `sealCapture` closeTick pass-through
5. Update LOOP guide §3 + display invariant note
6. HITL + 140656 scenario replay
