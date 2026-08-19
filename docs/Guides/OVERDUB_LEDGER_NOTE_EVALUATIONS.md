# Overdub ledger note evaluations

**Status:** Durable evaluation catalog. **Not architecture authority.** Update this file when ledger / occupy-catch-up / playback-order owners change.  
**Date:** 2026-08-19  
**Companion:** [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md)  
**Active investigation:** [`../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)

This catalog is every **note evaluation** that decides whether an identity is open in `ActiveNoteLedger` (**open ledger identities**) or whether a committed playback event is applied to the ledger. It is **not** overlap geometry and **not** source-view covering identities.

CAP `n=` = open ledger identities (`collectOverdubNoteOnParticipantIds`). CAP `a=` = source-view covering identities (`collectOverdubSourceHoldParticipantIds`). They are allowed to differ: they answer different questions.

Firmware for Gate 5B is **not approved**. **Gate 5A shipped** (equal-tick apply pairing). HITL [`161349`](../../captures/session_20260819_161349.log): extra covering `a>n` **0**; remaining mismatches are `n>a` (more open ledger identities than covering spans) — do not treat that as a ledger-owner bug for this RC. Gate 4 pinned (pre-5A Length `6073` moved Off@168). **Gate 5B** Length `6073` 551 effect: after pairing, B reconstructs `168–360` and covers 312; provenance not proven — no implementation. Do not change ledger owners to “fix” source-view geometry. At hold 312, intended `n=1 a=0` is A still open in the ledger; `a=0` does not mean the ledger is wrong.

**Update this catalog in the same change** if any of these moved: `ActiveNoteLedger::applyPlaybackEvent` / `noteOn`, `rebuildPlaybackOrder`, `CommittedPlaybackLedgerCatchUp`, `collectOverdubNoteOnParticipantIds`, wrap catch-up exclusion.

---

## What this catalog answers

Given a committed playback NoteOn or NoteOff, is this identity in the open set, and in which order are equal-phase events applied?

Ledger contents: zero or more applied NoteOns whose Off has not yet been applied. Sparse table of `Entry { channel, note, noteId, startTick, velocity }`. No `endTick`. No exclusive-end math.

**Ledger Entry identity:** `(channel, pitch, noteId)`. `Loop::allocateNoteId` is a per-loop monotonic counter (`nextNoteId_`, starts at 1). `kInvalidNoteId` is 0. Identity is **loop-local**, not global across tracks or slots.

Two different membership tests — do not collapse them:

| Test | Key | Where |
|---|---|---|
| Duplicate / open lookup, tagged Off | `(channel, pitch, noteId)` | `findIndexByNoteId` |
| Untagged Off | `(channel, pitch)` LIFO newest | `findNewestIndex` |
| Full-loop reconciliation | `noteId` only (any NoteOn in merged events) | `eraseOpenNotesMissingFromCommittedNoteOns` |

Offs are identity-bearing only when `evt.noteId != 0`. Committed Offs are often untagged (DEC-042: do not stamp capture Offs). A second apply of an Off after catch-up uses **whatever tagging that playback event actually carries**, not “the same logical Off as tagged.”

---

## Three predicates — not one

`applyPlaybackEvent` return is **not** “the ledger mutated.” Header contract: *“Returns false if the event must not be emitted.”*

```
event selected for ledger processing
        ↓
ledger state mutated?          (Entry push / erase / no-op)
        ↓
caller permitted to emit MIDI? (applyPlaybackEvent return)
```

| # | Predicate | Owner | Meaning |
|---|---|---|---|
| 1 | Reach the ledger? | Clock cursor / USB `shouldApply` | This committed event is offered to `applyPlaybackEvent` |
| 2 | Ledger mutated? | `noteOn` / erase / no-op | Did an Entry appear, disappear, or stay unchanged |
| 3 | May emit? | `applyPlaybackEvent` return | Clock `playbackCursorAdvanceSend` sends only when true. Catch-up **ignores** the return (`(void)applyPlaybackEvent`) and never sends |

**Ledger application success is not equivalent to ledger mutation.** Do not “fix” a duplicate On by returning false — that would skip MIDI emission (`playbackCursorAdvanceSend` returns before `sendMidiEvent`). Duplicate-On no-op is intentional ([`overdub_occupy_duplicate_open_identity_bugfix.md`](../Plans/overdub_occupy_duplicate_open_identity_bugfix.md)): mutation skipped, return **true**, clock still emits.

| Event | Mutation | Return | Clock emit |
|---|---|---|---|
| NoteOn, first time | push Entry | true | yes |
| NoteOn, same `(channel, pitch, noteId)` already open | **no** (no-op push) | **true** | **yes** |
| NoteOn, table full (`kMaxOpenNotes`) | **no** (refuse, never evict; `overflowed_`) | **true** | **yes** |
| NoteOn, untagged (`noteId == 0`) | always push (duplicate check requires a known id) | true | yes |
| NoteOff, known id, Entry found | erase that Entry | true | yes |
| NoteOff, known id, Entry **not** found | **no** (orphan; no LIFO fallback) | **false** | **no** |
| NoteOff, untagged, lane has an Entry | LIFO erase newest on `(channel, pitch)` | true | yes |
| NoteOff, untagged, lane empty | **no** | **false** | **no** |
| other (CC, etc.) | **no** | **true** | yes |

Table-full On does **not** return false. Overflow still permits emit; the extra identity is absent from the open ledger identities.

---

## Owner chain

```
committed merged MIDI
    ↓
rebuildPlaybackOrder          (clock apply order)
    ↓
playCommittedLoopMidi
    ↓
Track::applyPlaybackLedgerEvent
    ↓
ActiveNoteLedger::applyPlaybackEvent
    ↓
open Entries  →  open ledger identities  via collectOverdubNoteOnParticipantIds

USB occupy (separate writer, same ledger):
    ↓
CommittedPlaybackLedgerCatchUp::shouldApply
    ↓
applyOpenClosedIntervalEvents     (phase order, Off then On per phase)
    ↓
applyPlaybackEvent                (return ignored; no MIDI)
```

Capture overdub emit (`playbackCursorAdvanceSendCapture`) does **not** write the ledger. Mute still writes it (`playbackCursorAdvanceApplyLedger`).

---

## Evaluations, in order

### 1. May this event reach the ledger?

| Evaluation | Owner | Reach | Skip |
|---|---|---|---|
| Clock path | `playCommittedLoopMidi` → `playbackCursorAdvanceSend` / mute apply | Committed playback On/Off in cursor order | Capture-only emit |
| USB occupy catch-up gate | `CommittedPlaybackLedgerCatchUp::shouldApply` | `lastTickInLoop != UINT32_MAX` **and** `occupyPhase > lastTickInLoop` **and** not an overdub wrap commit interval | Equal tick (`occupyPhase <= lastTick`); wrap interval — see wrap exclusion below |
| Interval membership | `IntervalProjection::didPlaybackEventCross` | Event phase in `(lastTickInLoop, occupyPhase]` | Outside the open-closed interval |

### 2. Catch-up vs clock ownership (lifecycle)

Catch-up **only advances the ledger's knowledge** of committed playback events. It does **not** establish that those events have been consumed by the normal playback cursor. It does **not** send MIDI, rebuild merged events, or mutate `lastTickInLoop` / `nextEventIndex`.

| Evaluation | Owner | Meaning |
|---|---|---|
| Catch-up vs cursor | Catch-up + playback cursor | Catch-up may reconstruct ledger state without consuming or repositioning the cursor. Clock later owns whatever cursor interval its current `lastTickInLoop` defines; catch-up does not consume or advance that cursor |

**Duplicate application of the same committed playback interval is expected** after USB catch-up (pin [`101319`](../../captures/session_20260819_101319.log); HITL [`103234`](../../captures/session_20260819_103234.log)). The second application is not guaranteed to be the same event representation: it uses **whatever identity tagging the playback event actually carries**.

| Second apply (clock after catch-up) | Ledger | Emit |
|---|---|---|
| Tagged NoteOn, same `(channel, pitch, noteId)` already open | no-op push (one Entry) | yes — do not skip send when already open |
| Tagged NoteOff, Entry already gone | orphan, no mutation, no LIFO fallback | **no** (`applyPlaybackEvent` false) |
| Untagged NoteOff, lane nonempty | LIFO pop of whatever is now newest on `(channel, pitch)` | **yes** |
| Untagged NoteOff, lane empty | no mutation | **no** |

Do not treat the tagged-orphan row as the default for “the same Off.” Committed Offs are often untagged (DEC-042). An untagged second Off is LIFO, not an orphan.

Do not make occupy catch-up run when `occupyPhase <= lastTick`. That would be occupy repairing clock, rejected on this branch.

### 3. Wrap interval exclusion — why

`shouldApply` is false when `Loop::shouldCommitOverdubWrap(lastTickInLoop, occupyPhase)`.

**The wrap commit interval is excluded from USB catch-up because those events are expected to enter the ledger through the normal clock/wrap path** (`catchUpOverdubWrapPlaybackLedger` → `playbackCursorAdvanceSend`), **preventing two writers from independently establishing ledger state for the same committed interval.**

Do not remove this exclusion merely because the wrap ticks also lie in `(lastTick, occupy]`. Do not move wrap ledger ownership onto USB occupy or onto the source-view handoff.

### 4. What `lastTickInLoop` is

`playCommittedLoopMidi` assigns `loop.lastTickInLoop = tickInLoop` on the clock path. Catch-up reads it and **must not** write it.

**`lastTickInLoop` is a playback-cursor boundary, not a statement about the latest event existing in storage and not “the last phase whose events were applied to the ledger.”** Empty phases still advance it. Sparse committed streams do not. Catch-up may inspect occupy phases beyond that boundary; the cursor does not move.

```
clock owns cursor boundary
occupy may inspect beyond cursor
occupy never advances cursor
```

`lastTickInLoop` can therefore lag the most recently inspected occupy phase indefinitely.

### 5. Empty phases / sparse streams

Example: `lastTickInLoop = 100`, next committed event at 300, occupy = 200.

| What happens | Result |
|---|---|
| Catch-up `shouldApply` | true (`200 > 100`, assuming no wrap) |
| Interval walk | no event crosses `(100, 200]` — ledger unchanged |
| `lastTickInLoop` after catch-up | **still 100** |
| Later clock to 300 | cursor still owns `(100, 300]`; event at 300 is not lost |

Do not advance `lastTickInLoop` merely because an interval was inspected. That would silently drop later events from both catch-up and (if someone then skipped clock) from apply.

### 6. How the event is applied (mutation)

`ActiveNoteLedger::applyPlaybackEvent` / `applyPlaybackEventBody`. Off does **not** compute an exclusive end. Nested same-pitch notes can both be open (more than one open ledger identity on one pitch). DEC-042: the ledger is not one Entry per `(channel, pitch)`.

**Untagged Off vs tagged Entries — same lane:**

Tagged and untagged state **coexist** on one `(channel, pitch)` lane. An untagged Off uses `findNewestIndex(channel, pitch)` and **does pop a tagged Entry**. Proven: `test_ledger_note_on_pushes_untagged_off_pops_lifo` — On id=42, On id=99, untagged Off removes 99, then 42.

```
On C60 id=A
On C60 id=B
Off C60 untagged  →  removes B (newest), A remains
```

A **tagged** Off for a missing id does **not** fall back to LIFO (orphan, return false). That is a different identity model from untagged Off and can change the open ledger identities without matching a specific id.

### 7. In which order are equal-phase events applied?

| Path | Owner | Order |
|---|---|---|
| Clock | `rebuildPlaybackOrder` in [`TrackPlaybackWindowBuild.cpp`](../../src/Track/TrackPlaybackWindowBuild.cpp) | Sort by phase then tick; then reorder equal-phase+tick groups **Off before On** (same keys as `NoteUtils::sortMidiEventsChronologically`) |
| USB catch-up | `applyEventsAtPhase` | Per phase in `(lastTick, occupy]`: all Offs, then non-Offs. A global Off-then-On two-pass cannot close a NoteOn that starts in the same interval (DEC-042) |

Shipped clock invariant ([`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md), HITL **PASS** [`001021`](../../captures/session_20260819_001021.log)): at equal phase, abutting same-pitch replacement last-writes the new On. **Do not change this** as part of wrap-continuity or source-view geometry work.

### 8. After a full-loop rebuild, which open Entries survive?

`reconcilePlaybackLedgerAfterFullLoopRebuild`:

| Evaluation | Owner | Keeps Entry | Drops Entry |
|---|---|---|---|
| Stream is full-loop | `isFullLoopMergedPlaybackWindow` | Yes — run erase | Windowed gather — skip erase |
| On still in committed stream | `eraseOpenNotesMissingFromCommittedNoteOns` | Tagged Entry whose `noteId` has a NoteOn **anywhere** in merged events (`evt.isNoteOn() && evt.noteId == id`; channel/pitch not consulted); untagged Entries always stay | Tagged Entry whose On is gone (`DIAG,ledger,erase`) |

**Reconciliation answers “does this `noteId` still have a NoteOn in the committed stream?”, not “should this identity currently be sounding?”** It is **not** the ledger Entry key `(channel, pitch, noteId)`. A NoteOn at any tick in the rebuilt stream keeps the currently open Entry for that `noteId`, even if channel/pitch on the stream event differ from the Entry. Untagged Entries are skipped and stay.

### 9. What are the open ledger identities at occupy?

| Evaluation | Owner | Counts |
|---|---|---|
| Lane filter | `Loop::collectOverdubNoteOnParticipantIds` | Every open Entry with matching `channel` and `pitch` (inserts `entry.noteId`) |
| Newest-on-lane accessor | `ActiveNoteLedger::noteId` | Compatibility only — **not** occupy. Occupy uses `forEachActive` / the collect above |

The open-ledger-identity count is the size of that id set. Capture `#CAP DIAG,lcr,part` `n=` is this collect. `a=` is the source-view covering identity count (`collectOverdubSourceHoldParticipantIds`) — see overlap-resolve overview, adjacent table.

---

## What the ledger does **not** evaluate

| Not evaluated | Where that lives instead |
|---|---|
| Exclusive end / Shorten / Hide | `resolveConstrainedGeometry` |
| Present-at-hold covering | `displayNotePresentAtHold` on source-view notes |
| Whether a NoteOn is still live capture (uncommitted) | `extractOpenCaptureNoteOns` — open B never reaches this ledger |
| Pairing Off to On in storage | `reconstructDisplayNotes` |
| Source-view membership after rebuild | `rebuildOverdubSourceView` |
| Whether open ledger identities should equal source-view covering identities | It must not be forced. Mismatch is a diagnostic, not a coupling license |

An open B that has no recorded NoteOff is not a ledger Entry. Wrap carries it as live capture only.

---

## Open ledger identities vs source-view covering identities (deliberate)

```
clock applies committed On/Off  →  ledger Entries     →  open ledger identities
resolver geometry in source view → present-at-hold    →  source-view covering identities
```

Example that is **correct** once A's Length-167 DisplayNote is preserved (`endTick=167`, covering `s < 167`):

| Hold | Open ledger identities | Source-view covering identities | Why |
|---|---|---|---|
| 150 | `{A, B}` if both Ons applied | `{A}` | DisplayNote A `144–167` covers 150 (`150 < 167`). Canonical exclusive `[144,168)` exists only inside reconstruct |
| 312 | `{A}` (Off@264 closed B; Off@360 not yet applied) | `{}` if A is Length-167 and B is `168–264` | A covering `s < 167`; B covering `s < 264`. Neither covers 312. **`a=0` does not mean the ledger is wrong.** Do not write a hold-312 occupy oracle that expects B covering 312 |

Mismatch logs (CAP `n=` ≠ `a=`) are diagnostics. They are not a license to patch occupy collect, stamp Offs, or couple ledger Entries to source-view spans.
