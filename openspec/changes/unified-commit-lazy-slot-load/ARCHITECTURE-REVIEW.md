# Architecture review — unified-commit-lazy-slot-load

Per-phase gates before firmware for that phase. Authority: frozen plans under `docs/plans/unified_publish_pipeline_*`.

## North star

All runtime-visible loop changes occur through **Commit**. Loading is one producer of committed state. Boot sync-commits the audible set; runtime defers explicitly requested loads.

## Per-phase gates

### Phase 0 — Docs / OpenSpec

| Question | Answer |
|----------|--------|
| Owner | Architecture docs + this OpenSpec change |
| Ownership change? | NO |
| Transition change? | NO |
| Behavior-preserving? | YES (docs only) |

### Phase 1 — Rename

| Question | Answer |
|----------|--------|
| Owner | `Loop` / call sites of published→committed APIs |
| Primary invariant | Commit vocabulary; Pass vs Events; no new Flat |
| Ownership change? | NO |
| Transition change? | NO |
| Behavior-preserving? | YES (identifier rename) |
| Reuse | YES — rename existing symbols |
| Phase scope | Rename + tests/guides only |

### Phase 2 — Audible-only boot

| Question | Answer |
|----------|--------|
| Owner | `StorageManager` boot restore + `bootInteractiveReady` + `main.cpp` |
| Primary invariant | Interactive ready when audible set COMMITTED; no full-set auto-drain |
| Ownership change? | NO |
| Transition change? | YES — ready gate / enqueue policy (approved this change) |
| Behavior-preserving? | NO (intentional boot UX) |
| Reuse | YES — `isAudibleBootSlot`, existing sync load OK |
| Phase scope | Boot queue + ready gate + BOOT_LOAD guide |

### Phase 3 — Cooperative session

| Question | Answer |
|----------|--------|
| Owner | `SlotLoadSession` |
| Primary invariant | Scheduling outside session; Commit inside load path |
| Ownership change? | NO |
| Transition change? | NO (internals) unless boot switches to step-loop |
| Behavior-preserving? | Prefer YES for boot UX |
| Reuse | YES — extend `SlotLoadSession` |
| Phase scope | Session advance; optional factor of load body |

### Phase 4 — Deferred on-demand

| Question | Answer |
|----------|--------|
| Owner | `processDeferredLoopSlotRestore` + focus/select request path |
| Primary invariant | Priorities audible + requested only; no speculative fill |
| Ownership change? | NO |
| Transition change? | YES — when unloaded slots hydrate (approved) |
| Behavior-preserving? | NO vs today’s full-set / blocked mid-play |
| Reuse | YES — extend deferred restore |
| Phase scope | Runtime deferred request + idle defer while PLAYING |

### Phase 5+ 

Separate gates when started; not MVP.
