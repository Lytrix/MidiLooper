# 2.2 — successor occurrence positioning and stationary display freshness

## Scope / investigation / approved reassessment

User requests repair of `handoff_unreachable` → `win_blocked_by_budget` and investigation of missing bars2–16
in the stationary post-stop viewport of a32-bar recording. Owner closes the reported NOTE_EDIT stop→reboot crash
as no longer occurring; retain historical observations, no further crash gate. Existing dirty source-tuple fix is preserved.

G0 — evidence verdict PASS: Track's rolling requester→pending/frozen qualification→admission→private Fill/Order→
ready→clock-boundary adoption owns successor work. Loop owns published content; Display owns its committed-prefix
cache and reads snapshot membership or visualCache fallback, not playback window storage. These paths share a Loop
and deferred opportunities, not request state. Bounded capture traces show96 qualified events on the24-bar restored
target and a fixed16-bar post-stop viewport. Display counts/source freshness are not tested by the old long-loop oracle.
Independent read-only display trace found successful partial `rebuildVisualCacheIdleSlice` mutations do not advance
`visualCache.revision` until all dirty bars complete; stationary draw-source reuse can therefore retain an old prefix.
Specific attribution to the owner's pixels remains device verification, not assumed from static code.

Historical review: DEC-155 — cost-priced rolling position intentionally requires `pricedLead + span <= loopLength`
and retains refusal on the24-bar switch target; it is not a new full-loop-source bug. DEC-147 — position refusal and
bounded retry, DEC-152 — commit-bind seed, DEC-153 — positioned launch span, DEC-154 — bound ownership all apply.
Formal reassessment: change the one-traversal position policy, not the service envelope. Alternatives: keep refusal
(rejected by user request), loosen service budget (rejected: false schedulability), reuse NOTE_EDIT's existing bounded
absolute future-occurrence search (preferred, user explicitly APPROVED in this session). Supersede only DEC-155's
one-traversal cap under DEC-156 — committed future-occurrence positioning. No new scheduling owner or working-set class.

## G2 — execution packet, before edits

### Successor contract

- Existing owners/functions: `Track::serviceRollingPlaybackWindow`, `earliestEligibleHandoffLead`, retained request,
  existing qualification/build/ready lifecycle and `resolveRollingPlaybackWindow`.
- Derive the same worst-window work price with unchanged quanta64/48, permitted advance64, H8 and one-bar reserve.
  Reuse the bounded64-occurrence absolute search already used by preview; retain selected absolute deadline once.
  Do not recompute/extend it on later turns, revision coalescing, or qualification; overflow/exhaustion refuses.
- Preserve source generation/root/coverage checks, sibling deadline protection, one-pass-late fault policy, transport
  cancellation, role arbitration, immutable copied windows and no early adoption at an earlier same-phase occurrence.
- Regression: density exceeding the single-traversal lead still qualifies privately, publishes ready, waits at earlier
  occurrences and adopts only at its absolute tick; no unreachable retry/budget storm for schedulable price. Existing
  cheap/within-loop behavior, invalidation/supersession and true refused capacity controls stay covered.
- No second family removal, no RAM1 re-base, no content/persistence format change. Existing fallback serves gaps;
  this is eventual handoff correctness, not a new physical latency or continuous-ring pipeline promise.

### Display contract

- `Loop::rebuildVisualCacheIdleSlice` already publishes each successful slice's notes/dirty-bar state. Advance the
  existing revision on every such publication, rather than only final cleanup, so the draw-source owner can observe it.
- Regression through existing production display harness: visual fallback paints a prefix at fixed viewport; another
  successful visible slice while other bars remain dirty must be observable without moving the viewport. Preserve
  publication-gap held-frame/content-interval coherence and current-root source policy. No repaint/whole-loop gather on MIDI stack.

## Verification / HITL recipe

Focused native: runtime working-set ownership, live Capture display boundary, preview integration, visual-cache suites.
Full native, both capture builds at existing RAM1 pins, one focused lifecycle review, strict OpenSpec and diff checks.
Reused STIM-WINDOW-CONTENT-01 authoritative preset with32-bar Record, scratch track8 slots4/5, even24-bar target;
qualification image permitted earlier in this session; no new markers/stimulus edits. Retain exact HEX/uploader2.3/boot,
source-qualified readiness, ready→serving and post-stop fixed-window note identities/counts, ring integrity and memory.
Existing device scratch authorization track8 slots4/5 only; no other SD deletion/quarantine. List stimuli before run.
No gates close without evidence; original crash closure is the owner's explicit decision, not inferred from this fix.

## Evidence

### Native RED/GREEN and implementation

- Future occurrence RED: existing valid18-bar/15-notes-per-bar fixture fails the new absolute-lead assertion;
  original requester chooses a first-traversal deadline. Initial96-event fixture setup was invalid because its last
  fixed48-tick note exceeded loop end; corrected the fixture rather than treating construction failure as RED.
- Implemented `positionCost` from the same worst-window work price; existing bounded occurrence search chooses
  a paid absolute lead. Transfer absolute deadline/epoch/positionedLater once into the fresh pending record.
  Earlier occurrences remain private; no service constants, buffers or runtime size changed.
- Focused review found independent main-loop requester could replace the rolling pending request after a revision
  change before worker selection. New production-order RED: expected deadline18432, got13824. Added owner/epoch/
  generation/coverage/deadline-qualified preservation in `requestCommittedPlaybackWindowForSlot`; rolling identity
  refresh retains its responsibility. Regression then qualifies, waits through earlier occurrence and adopts at H.
- Dense600-event fixture now asserts future waiting, no early view change and the same600 fallback events, rather
  than expecting the superseded lead refusal. Capacity/source-invalidity controls remain in the existing suite.
- Display RED: real idle slice + production draw-source/paint fixture expected16 notes, got1 at fixed viewport.
  `visualCache.revision` now increments on each successful partial publication, exactly once on final publication;
  no-op/denied paths unchanged. GREEN verifies all16 exact identities/spans while bars17–32 remain dirty.
  Fixture explicitly opens empty Capture to isolate fallback invalidation; it is not a full Record-stop source-handoff
  proof. Existing publication-gap held-frame tests pass. Record-stop frame retention remains a separate limitation.
- Display focused275/275. Final combined runtime/preview/display **302/302**. Final full native **3433 cases /
  7 skipped /3426 passed**, exit0. One focused review, its concrete finding fixed/red-proved/retested; no review loop.
- Both final builds pass unchanged **variables96,192 / code392,040 + padding1,176 / locals34,880**, floor840.
  Shipping HEX `1924b90c6b214c3380483e90bc9a926b3afb9567b15de33874ce20610e57da8b`;
  qualification HEX `b11f18a56b361b63b05e6ad99817942e39eb15a0fb829ba95a21cc72056e3cdd`.

### Hardware runs and no-upload reruns

Qualification flashed once using CLI2.3: Programming + Booting exit0, exact HEX and loader transcript retained in
`captures/2.2-successor-display-images/`. Run01 (`captures/2.2-successor-display-device-01/`) was interrupted by two
operator reboots; owner confirmed firmware needed2 reboots and now works properly, then requested rerun without upload.
Do not attribute these resets to a new firmware crash; retained banners show crash0.

Run02 command (no upload and no reset):
```
.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity \
  --build-env teensy41-capture-serial-window-qualification \
  --track-number 8 --loop-slot 4 --midi-channel 8 --record-bars 32 --no-boot-reset \
  --switch-target-slot 5 --switch-settle-s 170 \
  --out-dir captures/2.2-successor-display-device-02-no-upload
```
No disconnects. Display leg and snapshot checks PASS; all lane checks except ring integrity PASS. Overallexit2:
Tier-A evictions delta3 + wipe delta1. Positive retained target receipts:
```
[290.994] [INFO] LoopEnd playback commit track=7 3->4 start=0 len=18432
#CAP,291044159,DIAG,win_gate,t=7,s=4,x=win_serving,...,dm=0,bld=0,...
#CAP,295008000,DIAG,win_gate,t=7,s=4,x=win_requested,...
#CAP,295126000,DIAG,win_gate,t=7,s=4,x=win_ready,...
#CAP,392153392,DIAG,win_gate,t=7,s=4,x=win_track_not_playing,...
```
One request→ready in118ms replaces the prior captured retry storm. No handoff_unreachable/budget-refusal rows
were found in this capture; absence remains loss-qualified, not a clean-ring proof. Later-occurrence adoption was
not observed before teardown97s after ready. Do not claim device ready→serving closure from initial launch Serving.

Run03 used the same no-upload/no-reset command with `--switch-settle-s 230`,
`HITL_MANAGED_RUN_TIMEOUT_S=480`, and `captures/2.2-successor-display-device-03-no-upload/` (longer leg bound,
not a service-budget change). No disconnects; display and snapshot checks PASS. Overallexit2: Tier-A evictions3
and no target win_gate under transport; no switch commit receipt. Longer settle therefore does NOT close adoption.
The display-window oracle is not an exact bar2–16 content oracle. Run03's first post-stop receipt is
`#CAP,594166322,DISP,3,PLAYING,24576,273,0,273,273,0,0,16,17`; later moved-window receipts contain more notes.
This cannot close the owner's precise stationary Record-stop freshness report. Native partial-cache fix is proved,
but the Record-stop held-frame/source-publication path remains owed for end-to-end attribution and verification.

### Disposition / closeout

- **NOTE_EDIT stop→reboot crash CLOSED by explicit owner confirmation**, not a pending gate.
- Successor policy and overwritten-position defect fixed/native-verified; device demonstrates private ready progress,
  not yet later-occurrence adoption with intact evidence. **3.7e — device launch/continuation remains unqualified**.
- Partial-cache invalidation fixed/native-verified; precise post-Record stationary display content remains unqualified.
- **2.2 — fixed/bridge removal remains unchecked**; bridge/cold-wrapper retirement not performed.
- No upload or reboot after owner requested no-upload rerun. Board remains qualification `b11f18a5…`, managed teardown
  sent both idempotent MIDI Stop/drain edges. No non-scratch clears/quarantine, no Git commit/push/PR.
- Next discriminating device setup must prove the active/target switch boundary and retain enough time for H, plus
  exact fixed-window content before movement; do not spend another identical long run or waive ring integrity.
