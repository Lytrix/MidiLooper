# 3.15d slice 3 — G2 execution packet (parity, refused-demand lifecycle, 2.12's save clause)

Parent: 3.15d — **serve short and long NOTE_EDIT from the same interval window reader**.
Slices: [1 interval composition](../2026-10-21-s315d-preview-interval/EXECUTION.md) `75d47454` ·
[2 clock-edge adoption](../2026-10-21-s315d-slice2-edge-adoption/EXECUTION.md) `0f71c75d`.
Branch `feature/unified-rolling-playback-path`.

## Scope (pinned by the owner declaration)

**In:** (a) the parity statement for short and long NOTE_EDIT — *content* parity between the merged fallback and
the interval window reader, established by an emission fixture at both ends of the admission boundary; (b) the
**session-edge release of a refused preview demand** — a demand the preview lane retained but the session can no
longer own must not outlive the session and must not keep the committed lane out; (c) fixture 2.12's `then save`
clause re-read while the preview **frame** (not merely the publication) is serving.

**Out:** admission constants and DEC-136's envelope, DEC-147 / `boundLaunchView`, any route change for short loops
(3.15f), the merged fallback's own composition, display (RWO S4e), the storage format, 3.7b, any new resident field
or pin rebase. **Stop condition:** if the parity fixture shows the *content* differs between the two routes, stop —
that is a slice-1 composition defect, not a route difference, and it is not sliced around.

## Why this slice exists — the measured starting point

Slice 2 made the preview the audible route while a session is active. Its remaining clause is the pair the task text
names first: **short and long NOTE_EDIT from the same reader**. Two facts bound that claim, and one of them is a
defect.

1. **The window reader cannot serve a whole-loop interval whose lead is below the declared price.** The admission is
   `leadTicks (768) + grains × (denied+1) × advance (64)`; a preview of a bare interval costs 3 grains, so the
   minimum lead is 2 304 ticks. A two-bar loop's interval is the whole loop and its lead is `1 536 − 768 = 768`.
   It is refused **by name** (`CannotCompleteBeforeHandoff`), not silently, and the merged reader serves it.
   *This is route parity, not content parity, and route parity is 3.15f's.*
2. **A refused preview request is retained as a promise that no session edge releases.** Probe (temporary fixture,
   two-bar loop, playhead 500, session open):

   ```
   PROBE after open:        demand=1 reason=2 sourceActive=0 owed=0 frame=0
   PROBE after visit:       demand=1 reason=2 sourceActive=0 owed=0 frame=0 published=0
   PROBE committed during:  outcome=1 (Excluded) demand=1
   PROBE after close:       demand=1 sourceActive=0 owed=0 frame=0
   PROBE committed after:   outcome=1 (Excluded) demand=1
   ```

   `reason=2` is `CannotCompleteBeforeHandoff`. The promise is the lane's own, its owner (the session's projection)
   is gone after the close, and `cancelPreviewWindowForSlot` cannot release it: it early-returns on
   `!runtime->previewSourceActive`, which is exactly the state a *refused* request leaves. The committed requester
   reads `runtime->pendingViewRequest_.active` first and answers `win_busy` — so on any loop whose committed route
   *is* windowed, the slot's lane is held out for as long as that promise lives. (On the two-bar loop the committed
   route is ineligible for its own reason, `win_window_ineligible`, which is why the probe's outcome is `Excluded`
   rather than `Busy`; the poisoning is visible on a demoted loop, and the fixture uses one.)

## Transition oracle

| # | Transition | Oracle (measured, not asserted from intent) |
|---|---|---|
| **P3-a** | Two-bar NOTE_EDIT session → served | The lane's request is refused (`CannotCompleteBeforeHandoff`), nothing publishes, no turn binds a preview frame, and the traversal emits the **session's composition** — committed notes plus the session's edit, each On/Off exactly once — with the ledger empty at the end. Content parity, by fallback. |
| **P3-b** | The same content on a long loop → served | The same note set, same ticks, on a 64-bar loop: the preview frame serves and the emitted `(tick, note, on)` sequence over the compared range is **identical** to P3-a's. This is the parity claim: one composition, two routes, one stream. |
| **L3-a** | A refused preview demand meets a session edge | Open a session at phase 0 on a demoted loop (lead 0 → refused, retained). Close it. The demand is **released** (`pendingViewRequest_.active == false`) and the committed requester's answer is no longer `Busy`. RED before the change; GREEN after. |
| **L3-b** | An edit while the preview frame is serving | The stale window never emits the new revision's content, the edited note sounds exactly once, the removed identity's sounding is closed once, the frame is not `Blocked`, and the lane ends serving the new revision. |
| **S3-a** | A save while the frame is serving | The body is the committed checkpoint (2.12's clause), the frame is still `Serving` the same `Preview` revision and storage generation afterwards, and the next turn emits the same stream (no re-send, no gap). |

**Pre-fix expectations:** P3-a, P3-b, L3-b and S3-a are *pins* over slice-1/2 behaviour and are expected GREEN before
the change; only **L3-a** is a RED. That is stated here so the RED count is honest: this slice's production change is
the release, and the other four fixtures exist to prove the release did not buy itself a content or persistence
regression.

## The smallest change the RED proves

`cancelPreviewWindowForSlot` releases the lane's **own retained demand** before its `previewSourceActive` early
return, keyed on the demand's source identity (`origin == Preview`) so a committed or capture demand is never
touched. Retention semantics (DEC-108 §13c: "a refusal is a promise") are unchanged for every demand whose owner
still exists; this only ends a promise whose owner (the session projection) is being cancelled. The admission
envelope, every constant, the routing and the pins do not move.

**As executed (2026-10-21):** the release alone was enough for the fixture's first form but **not** for the close path —
`closeNoteEditSession` flushed the session's last display advance *after* the yield, so that flush ran the requester
against a session that was still open and re-retained the promise behind the cancel. The executed change therefore also
**flushes before the yield** (`EditManager::closeNoteEditSession`), which is the same invariant stated from the other
side: the preview requester must never run after the lane's source has been yielded. A third release in the requester's
post-exit branch was written, measured unnecessary with that ordering, and removed. See
[`EXECUTION.md`](EXECUTION.md) for the edit-by-edit RED table.

**Scope decided after the slice (owner, 2026-10-21, option 2):** the fixture's parity statement is recorded as **content
parity only**. The literal route claim — *serve short and long NOTE_EDIT from the same interval window reader* — stays
open and the short-loop route-parity decision is **delegated to 3.15f**; this slice is not evidence that the two routes
are identical.

## Owner and path

- `Track::cancelPreviewWindowForSlot` (`TRACK_COLD_MEM`) — the release, before the early return.
- `EditManager::closeNoteEditSession` — the flush moves before the yield (added on measurement; see above).
- `test/test_windowed_preview_integration/test_windowed_preview_integration.cpp` — P3-a, P3-b, L3-a, L3-b, S3-a.
- Docs: this packet, `EXECUTION.md`, `tasks.md` 3.15d, the three runtime docs.

## Verification recipe

- Focused: `pio test -e native -f test_windowed_preview_integration` (29 cases: 25 existing + 4 new — P3-a and P3-b are the two
  halves of one fixture, so their streams are compared inside it).
- Full: `pio test -e native`.
- Both builds, pins unchanged; `openspec validate windowed-playback-generation --strict`; `git diff --check`.
- No device run: the audible route is still 3.15f's, and the release is observable only where a demand was retained.
