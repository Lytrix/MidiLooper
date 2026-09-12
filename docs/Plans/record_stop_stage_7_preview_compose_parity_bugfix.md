# Stage 7 — preview compose parity (`missed_preview` / `compose_miss`)

**Status:** **IN PROGRESS** (2026-09-12)  
**Kind:** bugfix / investigation  
**Parent:** [`record_stop_snapshot_publication_gap_display_bugfix.md`](record_stop_snapshot_publication_gap_display_bugfix.md) — stage 7 / OpenSpec §6.6.7  
**Telemetry owner:** `logDrawSourceLayerParity` in `DisplayNoteResolve.cpp`

---

## Decision gate (from parent plan)

After publication converges (`snap=1`), re-check late-overdub `draw_src,layers`:

- `missed_preview=0` and `compose_miss=0` → close as gap-handoff artifact; **no firmware**
- Non-zero post-publish → investigate preview vs compose identity; fix with native fixture

---

## Anchor capture — [`session_20260912_130411.log`](../../captures/session_20260912_130411.log)

21-bar record → long overdub; RC1/RC2 + publication **PASS**.

| Checkpoint | µs timestamp | Evidence |
|------------|--------------|----------|
| First `kind=publish` | `96074325` | `snapshot_unit,kind=publish` |
| First `snap=1` clean | `96142977` | `missed_preview,0,compose_miss,0` |
| First `snap=1` non-zero | `111807617` | `missed_preview,7,compose_miss,7` (~15.7 s after first publish) |
| Tail plateau | `150063718` | `missed_preview,22,compose_miss,15` (`frame≈298–304`) |

**Verdict:** gate **failed** — non-zero `missed_preview` / `compose_miss` persist **after** current snapshot publish. Stage-7 firmware investigation is in scope (not pre-publish gap-handoff only).

---

## Debugging boundary

```text
publication / snapshot current (snap=1)     ← trust converged
  → draw-source gather + compose            ← stage 7 owner
  → applyDrawSourceOverlays / recover*      ← related (see overdub_bar1_compose_miss)
  → painted frame vs capturePreview         ← missed_preview / compose_miss symptom
```

**Not in scope:** GAP-8c viewport supplement (reverted); GAP-8b stale-root predicate (reverted §6.6.5).

---

## Hypothesis (parent plan)

Likely `upsertDisplayNoteAtPitchStartFrom` / hold-path suffix boundary when snapshot is current — closed preview rows admitted to compose but dropped from final frame, or compose identity mismatch at same `(pitch, start)`.

Related shipped slice: [`overdub_bar1_compose_miss_bugfix.md`](overdub_bar1_compose_miss_bugfix.md) (recover path `committedEnd` — does not cover this tail).

---

## Next steps

1. Slice `130411` tail: correlate first `missed_preview>0` with `draw_src,preview_miss` rows and overdub wrap/stop boundaries.
2. Native fixture from that slice (closed preview + current snap + compose_miss).
3. Trace `resolveDisplayNotesByDrawSource` compose vs frame stages for the failing NoteId.
4. Device re-validation on same record→overdub recipe after fix.
