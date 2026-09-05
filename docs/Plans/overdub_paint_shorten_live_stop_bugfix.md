# Overdub paint — pending overlay eligibility (open-hold skip)

**Status:** RC1 host shipped. Occupy-paint recapture [`175026`](../../captures/session_20260905_175026.log): RC3 unparked (`52@664` E=144 / F=363). RC2 still pinned.  
**Date:** 2026-09-05  
**Kind:** bugfix (display realization + reconstruct pairing)  
**Evidence:** [`captures/session_20260905_172734.log`](../../captures/session_20260905_172734.log) (RC1); [`captures/session_20260905_175026.log`](../../captures/session_20260905_175026.log) / [`captures/host_midi_hitl_occupy_paint_20260905_175138.json`](../../captures/host_midi_hitl_occupy_paint_20260905_175138.json) (RC3)  
**Parent:** occupy-paint close [`overdub_paint_effective_frame_parity_bugfix.md`](overdub_paint_effective_frame_parity_bugfix.md) [`172213`](../../captures/session_20260905_172213.log)

**Does not authorize:** gather `keepShortest`; source-view sibling drop; redefining occupy On; changing `holdStart - 1`; applying pending to `overdubSourceViewNotes_`; painting B from pending Add; `#41` Stage 2; withdrawn `77@336` / `69@5328`; `applyCapturePlayheadTails` patch (`79@240`)

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn          ← trust (664 selected wrap 1971)
applyPendingHideAndShorten        ← RC1 shipped; not the 52@664 fail
reconstruct Off pair (pitch LIFO) ← RC3 (this slice)
Off/seal Hide vs Shorten          ← RC2 still pinned
applyCapturePlayheadTails         ← still parked
```

Sequence: **RC1 shipped → occupy-paint recapture → RC3 → RC2 pin only if still needed.**

---

## RC1 — transform eligibility is open-hold, not suffix position

**Owner:** `Loop::applyPendingHideAndShortenToNotes`

**Invariant:**

```text
A pending Hide/Shorten applies to any painted note carrying the
target NoteId, regardless of whether that note came from the
committed prefix or sealed/closed capture suffix.

The only display note excluded from the pending transform is an
open capture hold whose end is intentionally extended by
applyCapturePlayheadTails().
```

`liveCaptureBegin` stays the sibling-drop / Hide-by-overlap prefix end (`150650`). Sealed preview is draw source, not a second transform authority. No pitch+start fallback matcher.

**Architecture checkpoint:** ownership NO · transitions NO · unbounded NO · reuse YES.

**Host:** `test_pending_shorten_172734_open_hold_eligibility_matrix` — prefix 1207, sealed suffix 1207, open hold 1208/1208, unrelated sealed unchanged, source-view `ae=1352`.

---

## RC3 — `52@664` is reconstruct LIFO steal, not occupy / overlay

`52@664` is a **this-pass occupy Add**, not a source note RC1 failed to shorten.

| Fact | Log |
|---|---|
| OD2 On storage 664 | `MI` + `COORD` 664 |
| Occupy selected wrap source `52@1971` | `lcr,part,as=1971,ae=1581,hs=664` |
| Live Off 1040 | `COORD` 1040; `consume,norowid,id=15,s=664,e=1040` |
| Later On 1028 Shortens this-pass `id=26` to 1027 | `mismatch,lid=26,lst=664,hs=1028`; `mmspan,s=664,e=1027,id=26` |
| Store Offs | `SEVT N@664` + `F@808` (`mmevt` **id=11**) and `F@1040` (`mmevt` **id=26**) |
| Reconstruct / effective | `52@664` length **144** (`808-664`) |
| Frame | `52@664` length **363** (`1027-664`) |

`NoteUtils` reconstruct Off pairing is **pitch-stack LIFO**, not `NoteId`. `F@808` (`id=11`, source wrap) therefore steals this-pass `N@664` (`id=26`). Effective dump is `gatherCommittedEvents` + that same reconstruct, so **E=144 is the pairing artifact**. Frame 363 is occupy Shorten of `id=26` (`holdStart-1` at 1028).

Same class as parked dual-Off `79@240` / `80@104`. Not RC1 overlay. Not occupy On. Not `applyCapturePlayheadTails`.

Do **not** paint 144 to match E. That would score reconstruct as occupy. Catalog: Shorten never lengthens; this-pass occupy Shorten to 1027 is the painted product.

Prior PASS [`172213`](../../captures/session_20260905_172213.log) has no `52@664` — this key is this run’s grid, not an RC1 overlay regression.

### RC3 fix — tagged Off pairs by NoteId

**Owner:** `NoteUtils` reconstruct Off walk (same function that currently takes `stack.back()`).

**Invariant:**

```text
When a NoteOff has a valid NoteId, pair it with the open On that
carries that id. Untagged Offs keep current LIFO / wrap rules (Gate 5A).
```

**Architecture checkpoint:** ownership NO · transitions NO · unbounded NO · reuse YES — extend existing reconstruct pair. No new owner. Do not change `resolveOccupancyAtNoteOn`. Do not patch `applyCapturePlayheadTails`.

**Host pin** `test_reconstruct_175026_tagged_off_does_not_steal_this_pass_on` in `test_noteutils_reconstruct`:

- `N@664 id=26`, `F@808 id=11`, `N@1028`, `F@1040 id=26`
- Today: `52@664` length 144
- After: `52@664` is `664→1040` (376), not 144; `F@808` stays on `id=11`

**Then device:** occupy-paint RC1b. If dumps become `E=376` and `F=363`, stop — that leftover is gather applying the 1028 Shorten (do not bundle). If `E=F=363`, RC1b closes.

---

## RC2

Do not edit occupy until a host pin names whether Off or seal turns Shorten into Hide.
