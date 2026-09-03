# Overdub occupy — stop hide must not re-admit same-start record

**Status:** Device **PASS** [`204043`](../../captures/session_20260901_204043.log). Host fixtures shipped. Do not reopen the `768→1151` re-admit.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**Parent:** [`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](overdub_occupy_same_start_shorter_hides_longer_bugfix.md)  
**Evidence:** fail [`201956`](../../captures/session_20260901_201956.log) `draw_src` `id 71` `63@768→1151`; pass [`204043`](../../captures/session_20260901_204043.log) frame `63@768` len 95  
**Does not authorize:** RC2b tails; Stage 2 orphan `0→L`; reconstruct LIFO pairing; `keepShortestEndAtPitchStart` on the draw-source gather (forbidden by [`display_seal_handoff_capture_retention_bugfix.md`](display_seal_handoff_capture_retention_bugfix.md) / `150650`)

---

## Debugging boundary

```text
DEC-051 wrap seal at phase 0
    ← closed on 201956 (no WRAP 2304→96, no DNTE 0→3072)
Stage 2 orphan N@0
    ← gated closed
same-start Hide at NoteOn
    ← already correct (resolveOccupancyAtNoteOn)
sealed overlay released at revision >= Rseal
    ← closed on 204043 (Hide stays projectable; 768→1151 did not return)
display keepShortest on adopt/idle
    ← stays; not used on draw-source gather
Shorten same-start sibling drop
    ← [`overdub_paint_shorten_same_start_sibling_bugfix.md`](overdub_paint_shorten_same_start_sibling_bugfix.md) (`123236`)
```

---

## Problem

[`201956`](../../captures/session_20260901_201956.log) Stage 1 seal-at-0 is clean. At overdub stop, occupy totals are `hide=4, shorten=0`. Compose/effective keep the short overdub (`768→864`). After snapshot-current, `resolveDisplayNotesByDrawSource` skipped the sealed overlay and painted both:

```text
id 95  63 @ 768 → 864
id 71  63 @ 768 → 1151
```

`1151` is the occupy-shortened record sibling (`as=768, ae=1151`), not a wrap-held rewrite.

## Invariant

```
A sealed Hide stays projectable on the painted committed prefix while
companion IDs exist, including after the selected representation reaches
the seal revision.

At a pitch+start named by that Hide, display keeps the short overdub and
does not re-admit the longer record sibling.

Source view still keeps both noteIds so occupy can Hide the longer one.
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO — `Loop` still owns companion IDs; display only consumes `applyPendingNoteChangesToDisplayNotes` |
| State transition change? | NO — no capture/track transition added or reordered |
| Unbounded interval owner? | NO — overlay is bounded by the sealed companion ID list; same-start scan is bounded by the existing display-note prefix |
| Reuse | YES — extend `applyPendingNoteChangesToDisplayNotes` / `applyPendingHideAndShortenToNotes` |

## Implementation

- `hasSealedCompanionOverlay()` is true while `sealedCompanionOverlayIds_` is non-empty (appended across wraps). `lastOverdubSealCompanionIds_` stays last-wrap-only for admission.
- Draw-source, live-capture paint, and visual-cache apply run while that list exists — not only while `sealedCompanionOverlayRequired()` (revision stale).
- After display Hide/Shorten apply, drop a longer same-pitch+start sibling only at pitch+starts named by a Hide. Source-view apply does not drop siblings (`002145` norow).

Host: `test_sealed_hide_keeps_short_overdub_after_snapshot_current`, `test_sealed_hide_drops_same_start_longer_when_note_id_differs`.

## Device closeout ([`204043`](../../captures/session_20260901_204043.log))

| Gate | Result |
|------|--------|
| Frame `63@768` | len **95** (not 1151 / 576) |
| Post-stop `draw_src` | none |
| Snapshot-current seam | `70795769` `pending=0` `root_rev=15`/`playback_rev=15`; later `DFRAME` stays **81** |
| Flatten `WRAP` paint | 33 scanner lines; **0** wrap-held rows in the frame |
| SEVT pairing | 125/125 linear; no `0→3072` |

Same-start occupy paint is **not** a remnant on this capture: last COORD + occupy Shorten/Hide is the frame (`63@0→335` = last `0→576` shortened by later `336→576`; `1104`/`1536`/`2304`/`69@48` match last overdub length). Do not reopen to force reconstruct's shortest sibling.

## Parked — reopen if recaptured (`69@3024` frame miss)

Not Now implementing. On [`204043`](../../captures/session_20260901_204043.log): `SEVT` `N@3024` `F@3071`, effective `69@3024` len 48, stopped frame omits that row (`DFRAME` 81; effective count 82).

| Gate | Reopen when a future capture shows |
|------|-------------------------------------|
| Stopped-frame miss | A pitch+start present in `SEVT` and `dnte,src,effective` but absent from the stopped `dnte,src,frame` (this session: `69@3024`) |

RC2b / `79@240` / `150650` reopen gates live on their owner plans — do not absorb them here.
