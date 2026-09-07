# Overdub occupy — wrap `norowid` leaves ledger open and paints currentTick as temp Off

**Status:** Host-verified. Untagged-Off owner pinned to wrap cursor walk + occupy omitted-identity merge via the same `applyPlaybackEvent` rule. Native **1704/1705** (1 skipped). RAM1 locals **6848**. HITL recapture open.  
**Date:** 2026-09-07  
**Kind:** bugfix  
**Evidence:** [`session_20260907_174151.log`](../../captures/session_20260907_174151.log)  
**Feeder (do not reopen):** [`overdub_display_snapshot_handoff_bugfix.md`](overdub_display_snapshot_handoff_bugfix.md) (`170537` compose-before-Hide)  
**Catalog:** [`OVERDUB_LEDGER_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md) · [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md)

**Does not authorize:** stamping capture Offs (DEC-042); USB occupy catch-up on the wrap interval; per-wrap snapshot publication; full-loop reconstruct on the MIDI interval path; a new occupy Manager.

---

## Debugging boundary

```
display handoff (170537) — compose complete sealed source before Hide
    ← feeder: visualCache stays record-sized 110 after OD1 stop
occupy wrap Off / source-view row (174151) ← this RC
    ← id=112 missing from source view; Off@5375 id=0 does not close ledger
    ← consume,norowid; ledger n grows; incoming missed; Shorten to occupy-phase end
```

Do not treat `hidden=49` after the first OD2 wrap as a second compose-order bug.
The 49 Hides are first-overdub companions applied against a record-only
`visualCache`. Occupy then fails because those same identities are absent
from the consume row set.

---

## Problem

[`174151`](../../captures/session_20260907_174151.log) after record + first
overdub + a long second overdub: new incoming notes disappear, and NoteOff
does not close the painted hold. The hold keeps growing to the playhead
(`currentTick` / occupy-phase exclusive end).

Those are one failure, not two.

Occupy consume reads open ledger identities and must find matching
source-view / draw-source rows. After the first OD2 wrap it cannot.
`consume,norowid` repeats for the rest of the session. Ledger identities
stay open. Paint then applies pending Shorten with a wrap-shaped exclusive
end at the current occupy phase, which is the temp Off the display keeps
drawing.

---

## Proven chain ([`174151`](../../captures/session_20260907_174151.log))

### Session shape

| Phase | Track `ST` | Tick |
|---|---|---|
| Record | `ARMED → RECORDING` … `STOPPED_RECORDING → PLAYING` | 0 → 192 stop |
| OD1 | `PLAYING → OVERDUBBING` … `OVERDUBBING → PLAYING` | stop @ 3160 |
| OD2 | `PLAYING → OVERDUBBING` … wraps @ 5376, 10752, … → `STOPPED` | one long session |

Loop length is **5376**. Record paints `visual=110`. OD1 stop:

```
oh n=49 add=49 hide=49
VCACHE adopt_partial notes=110 dirty=0
```

First-overdub Adds never enter `visualCache`. OD2 open is
`visual=110 preview=0 sealed=0 snap=0 frame=110 hidden=0`.
Source view at both OD opens is `count=111` and ends at `id=111`.
`id=112` (pitch 69, `5328–5375`) is in merged playback and not in the
source view.

### After the first OD2 wrap (tick 5376)

```
oh add=6 hide=2
draw_src layers visual=110 preview=1 sealed=6 snap=0 frame=68 hidden=49
lcr mismatch pitch=69 lid=112 lst=5328 ltick=48 n=1 a=0
mmevt On@5328 id=112 / Off@5375 id=0
consume,norowid pitch=69 id=112
```

`lid=112 lst=5328` is still open at every later pitch-69 occupy through
the end of the capture (`90585706`).

### Incoming notes after more wraps

Pitch 63 ledger `n` grows `1 → 13`. `a` is often `0`.
`consume,norowid` lists previous-pass incoming ids
(`175, 177, 179, 181, 184, 171, 186, 173`) on every later Off.

Closed incoming preview misses the painted frame:

```
missed_preview 0 → 12 inside one wrap cycle
```

Paint writes occupy-phase exclusive ends onto those previous-pass rows:

```
painted_pending_shorten id=186 old 63 4992 5136 → 4992 191
painted_pending_shorten id=173 old 63 5368 528  → 5368 47
```

`191` and `47` are occupy start − 1 after wrap. That is the temp Off
drawn when consume cannot close the identity.

`hidden=49` stays constant for the rest of OD2. `visualCache` stays 110.

---

## Root cause

Occupy consume and wrap ledger close require a source-view / draw-source
row for every identity the playback ledger can open. After OD1 stop that
row set is the record-sized cache (`visual=110`, source view `count=111`).
It does not contain:

- loop-end record identity `id=112` (`69@5328`, Off `@5375` untagged);
- the 49 first-overdub Adds;
- later wrap-sealed incoming ids (`175` … `186`, `173`).

Playback still walks those NoteOns. After wrap, `id=112` is open in
`ActiveNoteLedger`. The untagged Off `@5375` does not close it.
`reconcilePlaybackLedgerAfterFullLoopRebuild` after
`playback_build,compatibility` keeps `112` because the NoteOn is still
in merged events.

`Loop` consume then logs `norowid` and cannot apply Off transforms to
those identities. `accumulatePendingNoteChangesFromSourceNotes` therefore
keeps a pending Shorten whose exclusive end is the current occupy phase.
`applyCapturePlayheadTails` keeps `endTick = currentTick` for holds whose
Off never landed on a painted row.

New incoming notes are the visible victims: `missed_preview` drops closed
preview rows, and the unresolved occupy Hide/Shorten set hits the same
pitch.

DEC-052 makes occupy use the same draw-source predicate as paint. While
that predicate still authorizes the incomplete 110-note cache, occupy
cannot see the identities playback opened.

---

## Invariant

```
every ledger identity occupy consume reads has a source-view / draw-source row
wrap Off@L-1 closes that identity even when the Off is untagged
incoming NoteOff freezes the painted end; currentTick tails apply only to open holds
closed incoming preview is in the same painted frame (missed_preview=0)
```

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO — `Loop` keeps source view / consume; `Track::catchUpOverdubWrapPlaybackLedger` keeps wrap ledger apply; `DisplayManager` keeps paint tails. |
| State transition change? | NO — wrap still commits through `maybeCommitOverdubWrap`; no per-wrap snapshot publication. |
| Unbounded interval owner? | NO — extend existing bounded wrap / consume / paint owners only. |
| Reuse | YES — `appendLastCommittedOverdubPassToSourceView`, occupy select used by `consume`, `catchUpOverdubWrapPlaybackLedger`, `applyPendingNoteChangesToDisplayNotes`, `applyCapturePlayheadTails`. |

If implementation starts writing ledger Entries from source-view spans, or
runs USB occupy catch-up when `shouldCommitOverdubWrap` is true, **stop**.

---

## Proposed fix

### 1. Occupy select must see every ledger identity

Extend the occupy row select that `consume` already uses
(`selectOverlapHoldNotes` / draw-source gather under DEC-052) and
`Loop::appendLastCommittedOverdubPassToSourceView` so the row set includes:

- the loop-end committed identity (`id=112` in this capture);
- just-sealed wrap Adds from `lastCommittedPassId()`.

A cache miss invalidates cache authority, not committed-content existence.
Do not treat `visualCache.notes.size()==110` as “no `id=112`.”
Reconstruct with `finishOpenNotes=false` can also omit a tail-window On
closed by an untagged Off at `L-1`. Recover that row from the committed
event stream using the same `ActiveNoteLedger::applyPlaybackEvent`
untagged-Off / LIFO owner — do not add a second pairing rule, and do not
change reconstruct's wrap-defer heuristic.

The `170537` compose-before-Hide work stays. It does not by itself put
`112` or later sealed incoming ids into occupy consume rows.

### 2. Wrap ledger Off must close the wrap-end identity

**Owner pin:** untagged-Off / LIFO matching stays in
`ActiveNoteLedger::applyPlaybackEvent`. Do not add a second closure rule
on `reconcilePlaybackLedgerAfterFullLoopRebuild`.

`catchUpOverdubWrapPlaybackLedger` is documented as applying merged
`(prev, S]`. The wrap tick sets `atLoopStart` and
`didPlaybackEventCross` then keeps only `evTick <= S`. After
`nextEventIndex` resets to 0, `Off@5375` is never offered to
`applyPlaybackEvent`, so LIFO never runs.

Extend that existing wrap cursor walk so it offers `(prev, L-1]` and then
`[0, S]` to the same `advancePlaybackCursor` → `applyPlaybackEvent`
path. Do **not** stamp Offs (DEC-042). Do **not** let USB catch-up apply
the wrap interval.

### 3. Paint is downstream, not the root fix

Required dependency:

```text
source contains identity
    → consume resolves identity
    → wrap Off closes identity
    → display row is closed
    → playhead tail no longer applies
```

Do not “fix” the symptom by suppressing tails while `norowid` and ledger
`n` still grow. `applyCapturePlayheadTails` stays paint-only. Keep
`liveCaptureBegin` prefix-Hide protection.

### 4. Observability

Keep `consume,norowid` and `painted_pending_shorten`. After this RC a
recapture must not repeat `lid=112` after the wrap that plays `Off@5375`,
and must not grow pitch-63 `n` across wraps while the player releases.

---

## Native pins (from this capture)

| Fixture | Must hold |
|---|---|
| Source view / occupy select after OD1 stop | includes `id=112` `69@5328–5375` |
| Wrap ledger after On@5328 + untagged Off@5375 | `112` is not open at `ltick=48` |
| Consume of a later same-pitch occupy | no `norowid` for `112` or just-sealed incoming ids |
| Closed incoming preview in the paint frame | `missed_preview=0` for that NoteId |
| Pending Shorten after a closed Off | end is the capture Off, not occupy-phase `191` / `47` |
| Incomplete `visualCache` | occupy does not conclude the committed identity is absent |
| Closed identity | later occupy does not re-Shorten it from a leftover ledger/companion entry |

Suites: `test_overdub_source_view`, `test_pending_note_change`,
`test_loop_take_survival`.

---

## Device regression ([`174151`](../../captures/session_20260907_174151.log))

After record + OD1 + a multi-wrap OD2:

- no standing `lid=112` after the wrap that crosses `Off@5375`;
- no repeating `consume,norowid` for `112` or sealed incoming ids;
- pitch-63 ledger `n` does not accumulate across wraps after NoteOff;
- no `painted_pending_shorten` of a closed row to occupy-phase `191` / `47`;
- `missed_preview=0` for closed incoming notes;
- incoming notes appear at NoteOn and freeze at NoteOff (no currentTick tail
  after Off).

`hidden=49` against `visual=110` is the feeder cache gap. Close it with
the `170537` handoff completeness work plus Stage 1 here. Do not call
that collapse fixed if `norowid` / `lid=112` still persist.

---

## Validation

1. Focused native pins above.
2. `pio run -e teensy41-capture-serial` (RAM1 locals ≥ 4000).
3. `pio test -e native`.
4. HITL recapture of the `174151` multi-wrap second overdub.

Update the ledger and overlap-resolve evaluation catalogs if consume or
wrap ledger apply changes.
