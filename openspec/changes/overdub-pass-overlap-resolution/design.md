## Context

Capture append today rejects events via `isDuplicateCaptureEvent` / `eventsEquivalent` within `DUPLICATE_TICK_TOLERANCE`, walking `capture.store` backward with `if (evt.tick < lo) break`. After loop wrap, append order is high ticks then low ticks, so the early-out is invalid and post-wrap dense MIDI produces a `duplicate` storm (`183525`).

NOTE_EDIT already owns canonical overlap via `NoteGeometryResolver` → `resolveConstrainedGeometry` → `buildEditSessionActions` → `applyEditSessionActions` ([`edit-session-action-geometry`](../../specs/edit-session-action-geometry/spec.md)). Overdub does not call that path.

[`timeline-passes`](../../specs/timeline-passes/spec.md) defines pass kinds and commit boundaries. This design does **not** change wrap→pass/undo grouping.

Persistence (DEC-020 mid-pass, overlay, Phase 5) stays orthogonal — see Non-Goals.

## Goals / Non-Goals

**Goals:**

- Two-pass overdub overlap: one immutable **source pass** (pre-session canonical geometry) + one accumulating **overdubPass**.
- Per-note evaluate-on-insert across wraps; one session → one `commitCapturePass` / one undo.
- Same overlap decisions as NOTE_EDIT for equivalent geometry.
- Wrap-safe, bounded candidate lookup via existing chunk/window infrastructure (`CommittedEventRange` and related).
- Clear persistence boundary; parallel to persistence tracks without shared apply sessions.

**Non-Goals:**

- Per-wrap undo or pass-per-wrap.
- Persistence performing overlap; Phase 5 recovery; overlay; admit API.
- Changing DEC-020 mid-pass sealed-chunk writer semantics.
- Mandating a new global note index before auditing chunk/window APIs.
- `lastSeenTick` / last-append / undo grouping as semantic search domain.
- Critical reclaim policy changes.

## Decisions

### D1 — Source pass identity

**Decision:** The source for every overlap evaluation in an overdub session is the **pre-session canonical committed note geometry** of the active loop slot — the already-resolved musical state from committed `passes[]` **before** this overdub session began — exposed as **one immutable note-geometry view**.

**Access:** Prefer existing committed traversal — `LoopPasses::materialize` / committed event gather and **windowed** lookup via [`CommittedEventRange`](../../../include/CommittedEventRange.h) (`chunkIntersectsWindow`, wrap-aware). Do not scan ad-hoc “whichever historical pass row looks recent.”

**Naming in prose:** “source pass” means this single immutable pre-session canonical view. Do **not** say “all previous materialized passes” unless that phrase is explicitly defined as this view.

**Alternatives rejected:** last overdubPass row only; pending capture store as source; `lastSeenTick` map; full unsorted capture append walk as authority.

### D2 — Two-pass transform; source immutable

**Decision:** Source geometry is never destructively rewritten during overdub. Overlap consequences (`Add` / `Shorten` / `Hide`/`Remove` per canonical rules) accumulate as operations on the **pending overdub pass** (committed as one `overdubPass` at stop).

**Encoding:** Reuse `EditSessionAction` / editPass row patterns (`ShortenNote`, `HideNote`, create/add) where applicable; map onto overdub pass representation without mutating source pass chunks in place.

### D3 — Session vs evaluation boundary

**Decision:**

| Boundary | Behavior |
|----------|----------|
| Overdub session | `start → wraps… → stop → commitCapturePass()` → one `overdubPass` + one undo |
| Overlap evaluation | Every newly inserted overlapping note evaluated **on insert** |
| Loop wrap | Not a pass/undo boundary; does not suppress re-evaluation at repeated phase |

**Alternatives rejected:** resolve only at stop (would defer/suppress wrap-2+ semantics); resolve-per-wrap as new undo (future separate design).

### D4 — Overlap authority

**Decision:** Geometric overlap policy is owned by **`NoteGeometryResolver` / `resolveConstrainedGeometry`** (and action builder semantics). Overdub MUST NOT invent a parallel shorten/hide/min-length policy.

**Apply ownership:** Pending overdub-pass op accumulation is owned on the capture/commit side (`Loop` / capture commit path), not by mutating `NoteEditSession.store`. Same **decisions**, separate **apply target**.

### D5 — Min-length reconciliation

**Decision (v1):** Use shared runtime globals `Config::noteMinLengthTicks` and `noteMinLengthRemoveEnabled` (current default `DEFAULT_NOTE_MIN_LENGTH_TICKS` = 12). Document in spec; do **not** silently adopt product “32nd/16th” wording without a DEC.

**Reconcile** [`capture-pass-boundary-materialization`](../capture-pass-boundary-materialization/) Q16 (`removePairsShorterThanNoteMinLength` on capture tier): keep pair-length sanity on capture events; geometric hide/shorten of **source** notes follows edit constrained-geometry rules. Design task: document that both use the same tick threshold globals.

### D6 — Candidate lookup (183525)

**Decision:** Performance work finds candidates in the **source pass** for each inserted note via wrap-safe chunk/window intersection — not “cheap duplicate detection.” Duplicate is one possible overlap outcome.

**Forbidden:** reverse-tick monotonic early-out on append-ordered capture store as semantic terminator; `lastSeenTick` as authority.

**Interim:** WARN/CAP throttle for deny storms may ship before full geometry apply (behavior-preserving observability only).

### D7 — Persistence boundary

**Decision:** Persist sealed capture bytes and/or committed `overdubPass` per existing contracts. Persistence MUST NOT re-run geometry. Mid-pass seals remain capture bytes; overlap ops live on the pending overdub-pass / preview until commit.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Live evaluate-on-insert CPU on dense MIDI | Bounded chunk/window lookup; native perf fixtures; throttle CAP/WARN |
| Dual apply paths (NOTE_EDIT store vs overdubPass) diverge | Shared resolve/constrain; parity native tests |
| Q16 capture min-length vs geometry hide conflict | Same globals; explicit design note; tests for short source notes |
| Formal trigger if commit encoding ownership moves | PREFLIGHT + DEC if apply leaves documented owners |
| Confusing “source pass” with a single `recordPass` row after many overdubs | Spec language: pre-session **canonical** view, not “only recordPass” |

## Migration Plan

1. Phase 0 — OpenSpec + ARCHITECTURE-REVIEW (this change); runtime docs pointer.
2. Phase 1 — Source-pass view API pin + candidate lookup (wrap-safe); throttle denies; keep current accept/reject until resolve wired.
3. Phase 2 — Wire `NoteGeometryResolver` decisions → pending overdub-pass ops on insert; native matrix.
4. Phase 3 — Retire capture-store append-order dedup as semantic authority (may remain as non-authority guard only if proven redundant).
5. Phase 4 — Device verify wrap+bar41; archive when gates pass.

Rollback: per-phase commits; Phase 1 lookup+throttle alone restores display observability without full geometry.

## Open Questions

1. Exact helper that freezes the pre-session canonical view at overdub start (snapshot vs live materialize-from-passes) — audit `beginOverdubSession` / `commitCapturePass` call sites in Phase 1.
2. Whether pending overdub-pass ops are visible to live display/preview before commit (product) — default: yes via existing capturePreview / composed display paths without mutating source.
3. Product min-length 32nd/16th vs code default 12 — needs DEC if product wording wins.
