# 3.15e — migration slice 1: completion record

**Packet:** [`EXECUTION-PACKET.md`](EXECUTION-PACKET.md) · **Status:** **native verified**, production behavior unchanged
(the echo still serves), no DEC yet (the invariant lands with slice 3, when a consumer exists).

## What landed

**One build identity, two halves.** `RuntimePlaybackViewFill::beginLiveContinuation` continues a *completed* committed-base
fill from one append-stable live source into the **same** destination, the **same** ordered index and the **same** source
identity; `RuntimePlaybackViewBuild::beginLiveOverlayContinuation` is the build-level attach (allowed only at
`Phase::Complete`, once). Appends go through the existing `extendLiveCapture` → Fill → `extendPrivateOrder` path, so the
ordering stays off-clock.

**One ordering owner for the whole window.** The ordinary fill→order transition now begins the order through the *storage*
overload and the build owns `destination_->complete` (`true` exactly when the ordered index covers every retained row), so
`extendPrivateOrder` can continue the same index after an append. That is the prerequisite the Record lane's append path
already relied on; the snapshot modes keep their own completion flags.

**Provenance, not a second route.** `kLiveOverlayPassOrigin` / `kLiveOverlayCanonicalRank` (`RuntimePlaybackView.h`) are
named constants: the overlay half's rows carry the sentinel `pass` with their own ordinal in `event`, the committed base
keeps the baseline's own pass index, and the window reports `passOriginsReady` (one table for both halves) with
`canonicalRanksReady == false` (the committed rank map no longer describes the whole window). The emission contract this
enables for slice 3 is exactly the owner's: a committed-origin row takes normal occupy/closure ledger semantics; a
live-overlay row is emitted from the same ordered window and does **not** acquire committed-pass ledger ownership
(`221334` preserved).

**Capacity over base + overlay, refused by name, never partial.** The destination storage is the only capacity: when the
next overlay row no longer fits, `refuseOverlay(CombinedCapacityExceeded)` clears the unpublished window, drops
`sourceFilled`/`complete`/the provenance flags, and detaches. No overlay row is truncated and no committed row is evicted.

**Pass commit invalidates the overlay identity.** The continuation captures the live projection + content generation at
attach. A re-based capture (the wrap's `beginCapture`) makes `nextGrain()` deny and **clear** the candidate's
`complete`/`sourceFilled` (const-safe mutation through the stored destination pointer), `continuationRefusal()` name
`ContinuationStale`, and `extendLiveCapture` refuse. The next build derives from the committed source.

**Identity for slice 2.** `overlayCaptureGeneration()` (fill + build) exposes the live capture generation the overlay
corresponds to, and the window's `source` carries it alongside the base's revision/generation — so role arbitration can
tell a complete-looking window whose overlay is stale from a current one.

## Owner's constraints, point by point

| Constraint | Where it is enforced |
|---|---|
| one build identity (one coverage, one index, one identity) | attach requires `Phase::Complete` on the *same* builder and reuses `destination_`; `fill_.begin` is never called again |
| named sentinel, not a magic id | `kLiveOverlayPassOrigin` / `kLiveOverlayCanonicalRank`, documented with the emission contract |
| capacity over base + overlay, named refusal, no partial window | `refuseOverlay` (clears) + `CombinedCapacityExceeded` |
| monotonic append | `extendSourceCursor` extension only; the cursor is never rewound (`cursor_` stays at the frontier) |
| pass commit invalidates | `nextGrain()` clears the candidate; `ContinuationStale`; fresh build re-derives |
| generation identity exposed | `overlayCaptureGeneration()` + `view.source.captureContentGeneration` |

## Size discipline

The fill's five flags/named-refusal now live in **one** packed byte (`flags_`), because the per-slot runtime pin is exact:
the first two-field version measured `7 920` against the pinned `7 904` and was re-packed before landing. Final sizes are
the pins themselves: target `sizeof(LoopPlaybackRuntime) == 7 904`, host `== 10 104`, both unchanged.

## Gates

| Gate | Command | Result |
|---|---|---|
| Focused | `pio test -e native -f test_playback_prewarm` | **243 / 243** (241 existing + 2 new) |
| Full native | `pio test -e native` | **3 386 / 7 skipped / 3 379 succeeded / 0 failed** (baseline 3 384 / 7 / 3 377) |
| Default build | `pio run -e teensy41-capture-serial` | SUCCESS, `code 391 688 + padding 1 528` — **byte-identical to the approved pins** |
| Lane build | `pio run -e teensy41-capture-serial-window-qualification` | SUCCESS, `code 391 848 + padding 1 368` — **byte-identical to the approved pins** |
| Spec | `openspec validate windowed-playback-generation --strict` | valid |
| Whitespace | `git diff --check` | clean |
| Device | — | **NOT RUN** (3.15f) |

The 3.15e emission oracle (`test_3_15e_a_live_overdub_is_served_exactly_once_through_the_wrap_and_the_pass_handoff`) and
every live-overdub fixture stay green, which is the "production emission remains on the old echo path" clause: no request,
no selection and no resolver branch was added.

## Mutations (each restored byte-identically)

| # | Mutation | Result |
|---|---|---|
| M1 | overlay rows written without the sentinel (provenance block removed) | `an overlay row must never look like a committed pass: Expected 65535 Was 0` — exactly the provenance case |
| M2 | `refuseOverlay` no longer clears the candidate | `a refused window must not be left partially filled: Expected 0 Was 4` — exactly the capacity case |
| M3 | `extendLiveCapture` no longer extends the source cursor | `Expected 7 Was 5` — the new pair never lands, so the append case fails |
| M4 | the build's staleness invalidation removed from `nextGrain()` | `a stale overlay half must not leave a complete-looking window` — exactly the invalidation case |

Restores verified by `sha256`: fill `4e25ca01f8549794…`, build `99b2f803ee4beedf…`.

## Owed after this slice

- **Slice 2** is the pressure point the owner named: a per-overdub request identity plus **role arbitration** among three
  demands (committed successor, NOTE_EDIT preview, live overlay) on the three existing view roles, with `ready` contention
  resolved by name — and the RAM1/pins re-checked.
- **Slice 3** then wires the edge: adopt the window, skip the echo/sort when it serves, and apply the ledger only to
  committed-origin rows (the DEC belongs here).
- Not claimed: any audible change, the wrap-held continuation NoteOn (slice 4), device parity (3.15f).
