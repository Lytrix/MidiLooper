## Context

Capture append today rejects events via `isDuplicateCaptureEvent` / `eventsEquivalent` within `DUPLICATE_TICK_TOLERANCE`, walking `capture.store` backward with `if (evt.tick < lo) break`. After loop wrap, append order is high ticks then low ticks, so the early-out is invalid and post-wrap dense MIDI produces a `duplicate` storm (`183525`).

NOTE_EDIT already owns canonical overlap via `NoteGeometryResolver` → `resolveConstrainedGeometry` → `buildEditSessionActions` → `applyEditSessionActions` ([`edit-session-action-geometry`](../../specs/edit-session-action-geometry/spec.md)). Overdub does not call that path.

[`timeline-passes`](../../specs/timeline-passes/spec.md) defines pass kinds and commit boundaries. This design does **not** change wrap→pass/undo grouping.

Persistence (DEC-020 mid-pass, overlay, Phase 5) stays orthogonal — see Non-Goals.

## Goals / Non-Goals

**Goals:**

- Stable **`overdubSourceView`** at overdub start: materialize-aware canonical note geometry for the session.
- Per-note evaluate-on-insert across wraps against that same view; one session → one `commitCapturePass` / one undo.
- **`overdubPass` is a complete delta**: Add plus Shorten/Remove (Hide) of source notes — closer to `editPass` than to a dedup cache.
- Same overlap decisions as NOTE_EDIT for equivalent geometry.
- Wrap-safe candidate lookup preferring existing chunk/window infrastructure; physical backing of the source view is an implementation choice.
- Clear persistence boundary.

**Non-Goals:**

- Per-wrap undo or pass-per-wrap.
- “Freezing the loop” or inventing FrozenPass / FrozenGeometry domain nouns.
- Persistence performing overlap; Phase 5 recovery; overlay; admit API.
- Changing DEC-020 mid-pass sealed-chunk writer semantics.
- Mandating a new global note index before chunk/window audit proves insufficient.
- `lastSeenTick` / last-append / undo grouping as semantic search domain.
- Critical reclaim policy changes.
- Combining all `183525` fixes into Phase 1.

## Decisions

### D1 — `overdubSourceView` (not “freeze”)

**Decision:** At overdub start (`Loop::beginCapture(Overdub)` / `TrackUndo::beginOverdubSession`), the system establishes a stable semantic view called **`overdubSourceView`**.

Do **not** call this a freeze. The loop is not frozen; committed geometry is not generally frozen by `beginCapture(Overdub)` today. The view is simply the **stable semantic baseline for this overdub session**.

```text
begin overdub
      → establish overdubSourceView (stable for session)
      → (wraps + per-note resolve…)
stop overdub
      → commit overdubPass (complete delta)
```

**Stability:** Every inserted note in the session evaluates against the **same** semantic baseline. Result must not depend on unrelated materialization changes mid-session.

**Content (semantic contract):** Materialize-aware canonical note geometry including effective `editPasses`. Must **not** use bare `CommittedEventRange` alone when edits exist.

**Physical representation (not prescribed):** Implementation may use materialized `MidiEvent` vector, reconstructed note spans, chunk/window-backed views, or a combination — provided the semantic contract holds. Prefer existing chunk/window infrastructure over a new global index.

**Ownership:**

| Owner | Role |
|-------|------|
| **Track** | Overdub session lifecycle (`beginOverdubSession` … stop/commit) |
| **Loop** | Provides/creates access to canonical materialized state → `overdubSourceView` |

No new top-level Manager. Exact field/class placement deferred to smallest consistent design at implement time.

**Naming:** Prefer `overdubSourceView`. Alternative if explicitly a snapshot object: `overdubSourceSnapshot`. Avoid `freeze*`, `FrozenPass`, `FrozenGeometry`.

**Alternatives rejected:** live rematerialize each insert as the v1 semantic baseline; last overdubPass only; pending capture as source; `lastSeenTick`; bare CER when `editPasses` active.

### D2 — OverdubPass is a complete delta

**Decision:** Source material is never destructively rewritten. The committed `overdubPass` records the **complete delta** that transforms the source view into the overdub result:

- **Add** — newly recorded notes
- **Shorten** — changes to source notes
- **Remove/Hide** — source notes made inactive by overlap

Reuse `EditSessionAction` / editPass patterns where applicable. `OverdubPass` today is chunk-IDs-only — encode placement is Open Q4 before Phase 2 firmware.

Examples: long A + short B → `Shorten(A)` + `Add(B)`; covering X over A,B,C → `Remove(A/B/C)` + `Add(X)` per canonical rules.

### D3 — Session vs evaluation boundary

| Boundary | Behavior |
|----------|----------|
| Overdub session | `start → wraps… → stop → commitCapturePass()` → one `overdubPass` + one undo |
| Overlap evaluation | Every newly inserted overlapping note evaluated **on insert** |
| Loop wrap | Not a pass/undo boundary; must not skip re-evaluation at repeated phase |

### D4 — Overlap authority

Geometric policy: **`resolveConstrainedGeometry` / action-builder semantics** (NOTE_EDIT parity). Do not invent capture-only overlap policy.

**Apply:** Pending overdub-pass delta owned on capture/commit side (`Loop` path), not `NoteEditSession.store`. Prefer free constrain/build helpers — do **not** open a fake NOTE_EDIT session (`NoteGeometryResolver::resolve` is session-gated).

### D5 — Min-length

**v1:** Shared `Config::noteMinLengthTicks` / `noteMinLengthRemoveEnabled` (default `DEFAULT_NOTE_MIN_LENGTH_TICKS` = 12). No overdub-specific constant. Product 32nd/16th wording needs DEC if it should replace code defaults. Q16 capture pair-remove stays on capture tier with same globals.

### D6 — Phase 1 scope vs later `183525` work

**Phase 1 (this refinement):**

- Establish `overdubSourceView` at overdub start
- Prove materialize-aware + stable across wraps
- Native tests: source selection, candidate lookup into the view, source immutability
- **Do not** wire into append accept/reject yet
- **Do not** bundle reverse-tick early-out fix, full geometry resolve, or encode into Phase 1

**Later phases:**

- Per-note overlap resolution + wrap-safe efficient lookup into the view
- Replace invalid reverse-tick early-out
- Encode Add/Shorten/Remove into overdubPass delta
- Device verify `183525`
- Deny-log throttle **separately** if RING still floods (observability only — not a semantic workaround)

**183525 framing:** Efficient wrap-safe candidate lookup into the stable `overdubSourceView` for repeated per-note overlap evaluation — **not** “make duplicate detection faster.” Exact duplicate is one overlap outcome.

### D7 — Persistence boundary

Persist sealed capture bytes and/or committed `overdubPass` per existing contracts. Persistence MUST NOT re-run geometry. Mid-pass seals remain capture bytes; overlap delta lives in RAM until `commitCapturePass`.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Dense MIDI CPU on evaluate-on-insert | Bounded chunk/window lookup; native perf fixtures |
| Dual apply paths diverge | Shared constrain/build; parity tests |
| Encode into chunk-only `OverdubPass` | Pin Q4 + PREFLIGHT before Phase 2 |
| “Freeze” language invents domain noun | Use `overdubSourceView` only |
| Half-policy if lookup wired to deny early | Phase 1 tests only; wire in later phase |

## Migration Plan

1. Phase 0 — OpenSpec + ARCHITECTURE-REVIEW (done; this refinement).
2. **Phase 1** — Establish `overdubSourceView` + native source/lookup/immutability tests (no deny wiring).
3. Phase 2 — Connect per-note resolve → accumulate delta; encode Add/Shorten/Remove; early-out retirement path.
4. Phase 3 — Retire capture-store dedup as authority; device wrap+bar41; optional throttle if needed earlier as separate commit.
5. Archive when gates pass.

## Open Questions

1. **Exact Loop API names** for establish/clear/query `overdubSourceView` (placement audit at Phase 1 implement).
2. **Backing representation v1** — events vs note spans vs hybrid (semantic contract fixed; pick smallest consistent design).
3. **Live preview of pending delta** — `capturePreview` today is capture MIDI only; product pin for Phase 2.
4. **Phase 2 encode target:** (A) companion `editPass` rows, (B) extend `OverdubPass` / pending-op buffer on `Loop`, (C) other — **PREFLIGHT** if new owner or dual writers. Cannot represent D2 with chunk IDs alone.
5. **Product min-length 32nd/16th vs code default 12** — DEC if product wins; v1 = code globals.
6. **`shouldRestoreCommittedOverlapOnOverdubStop`** coexistence once insert-time geometry exists.
7. **DEC-020 mid-pass** remains raw capture MIDI while delta is RAM-only until commit.

## Architecture invariants (refined)

| Question | Decision |
|----------|----------|
| Loop frozen at overdub start? | **NO** |
| Stable `overdubSourceView`? | **YES** |
| Established at overdub start? | **YES** |
| Semantically stable for session? | **YES** |
| Includes `editPasses`? | **YES** |
| Source mutated directly? | **NO** |
| `overdubPass` only new notes? | **NO** — complete delta |
| Can contain Shorten/Remove of source? | **YES** |
| Evaluate each new overlapping note? | **YES** |
| Multi-wrap session? | **YES** |
| Wrap creates undo? | **NO** |
| Stop creates overdubPass/undo? | **YES** |
| `lastSeenTick` authority? | **NO** |
| Source representation prescribed? | **NO** |
| Prefer chunk/window infra? | **YES** |
| Persistence resolves overlap? | **NO** |
| New top-level Manager? | **NO** |
