# Overdub paint — pending overlay eligibility (open-hold skip)

**Status:** RC1 host shipped. RC3 NoteId Off pairing **reverted and parked** (`44f47e94`). Truth capture is [`191913`](../../captures/session_20260905_191913.log). RC2 still pinned.  
**Date:** 2026-09-05  
**Kind:** bugfix (display realization; reconstruct pairing parked)  
**Evidence:** [`captures/session_20260905_172734.log`](../../captures/session_20260905_172734.log) (RC1); [`captures/session_20260905_175026.log`](../../captures/session_20260905_175026.log) (parked `52@664`); [`captures/session_20260905_191913.log`](../../captures/session_20260905_191913.log) (Bugs 1–4)  
**Parent:** occupy-paint close [`overdub_paint_effective_frame_parity_bugfix.md`](overdub_paint_effective_frame_parity_bugfix.md) [`172213`](../../captures/session_20260905_172213.log)  
**Next:** [`overdub_occupy_paint_191913_findings_bugfix.md`](overdub_occupy_paint_191913_findings_bugfix.md)

**Does not authorize:** gather `keepShortest`; source-view sibling drop; redefining occupy On; changing `holdStart - 1`; applying pending to `overdubSourceViewNotes_`; painting B from pending Add; `#41` Stage 2; withdrawn `77@336` / `69@5328`; `applyCapturePlayheadTails` patch (`79@240`)

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn          ← trust
applyPendingHideAndShorten        ← RC1 shipped; 191913 Bug 2 continues this owner
reconstruct Off pair (pitch LIFO) ← RC3 reverted and parked (175026 finding kept)
Off/seal Hide vs Shorten          ← RC2 still pinned; 191913 Bug 3 is invented Shorten
applyCapturePlayheadTails         ← still parked
```

Sequence: **RC3 pairing reverted → 191913 truth → Bug 4 → Bug 1 → Bug 2 → Bug 3. Reassess reconstruct pairing only after those owners.**

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

### RC3 pairing reverted and parked

`44f47e94` made tagged Offs pair by `NoteId`. That fixed `175026` `52@664` (RC1b `mismatches=[]` on `191913`) and is **not** kept. The global pairing contract is not understood.

Keep the finding. Do not add fallbacks. Reconstruct Off walk is the exact pre-`44f47e94` LIFO / wrap pair.

Parked diagnostic `diagnostic_only_reconstruct_175026_tagged_off_pairing` (`TEST_IGNORE`): LIFO today = 144; desired later = 376. Not an invariant.

Parked question (do not answer here): is `NoteId` a stable reconstruct-pairing identity across every event source that reaches `NoteUtils`?

---

## RC2

Do not edit occupy until a host pin names whether Off or seal turns Shorten into Hide.
