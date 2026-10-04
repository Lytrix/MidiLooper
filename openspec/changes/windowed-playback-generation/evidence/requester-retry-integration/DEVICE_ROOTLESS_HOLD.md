# Device rootless launch hold — bugfix execution packet

Device image fb73aeedd01262ea283171e0d61f8bd3128bbffb38a6e13f7782b6c1705e9583.
Baseline `captures/immutable-launch-device-baseline` inconclusive (persistence
result missing/display-empty-after-overdub). Registered baseline24-bar record-only
variant seeded track8 slot5; confirmed length18432, but inconclusive on active
snapshot/persistence evidence. Existing registered-parent switch wrapper (temporary
S4B8 variant), `captures/immutable-launch-device-switch/session_20261004_205541.log`,
queued Loop5 at34.099s, hold at42.037s age3072ticks, no commit. Run interrupted;
not a qualified pass. User confirmed slot5/track8 empty display + endless queue.

User authorized stopping transport and read-only investigation, no slot clear or
overwrite. `captures/immutable-launch-device-stopped-observation/` confirms stopped,
then complete64-slot LIBOBS: t7/s4 length18432,events1144,passes1,kp1,kr0,kc0. Stored
content exists; published root absent. First observation buffered during transport
is also preserved. No content-loss claim and no wrap-tracking attribution.

Root cause: legacy pass-body restore deliberately installs passes without a root;
queueLoadedSnapshotPublish has no caller and bulk loaded hydration is disabled.
The immutable requester waited on current-root readiness without requesting its
source. No producer could discharge that demand. Existing cold-reactivation contract
(runtime-working-set-ownership/IMPLEMENTATION_CONTRACT.md section4) explicitly
permits preparing the current authority off-clock, including a root until phaseF.
DEC-108 source-current/readiness and existing Loop snapshot publication ownership
apply. Do NOT enable bulk boot hydration or build inline on the clock edge.

Fix: selected inactive launch target requests existing requestCommittedPassesRepublish
only when source not current and no source job is pending. This is the explicit
on-demand activation path, not queueLoadedSnapshotPublish's blanket boot hydration.
Loop owns queued source construction through the existing Snapshot lane; window lane
continues waiting and then consumes the complete current root. No new buffer/owner,
no loop-content mutation, no timing/capacity/RAM1 rebase. Request once, never restart
an in-flight source job. Preserve outgoing playback and pending gesture.

Oracle: real pass-backed rootless target -> production provision enqueues source
and retains launch recipe -> repeated provision does not reset source work -> existing
bounded source steps produce current root -> window maintenance makes launch ready.
Existing queued boundary fixture must still commit without inline build. Device
retest uses the preserved target (no re-seed/clear); verify queue->commit and output.
Focused ownership, full native, capture/RAM1, strict OpenSpec/diff. Hardware rerun
after user-authorized verification upload; retain any non-pass evidence.
Status: packet written before firmware fix; verification pending.

## Result
The missing dependency is fixed. `preparePlaybackLaunchForSlot` requests
`requestCommittedPassesRepublish()` when currency is absent and no snapshot job
is pending. Existing jobs are not restarted; an existing matching launch recipe
reports newly enqueued source work without replacing its identity. No other
firmware path, storage format, source index or timing constant changed.

Native fixture seeds real committed passes, settles the helper's record-source job,
then removes its test root to reproduce the pass-only restored state. Before fix:
`pass-backed launch must request its missing source, not wait forever`. After fix,
repeated provisioning interleaved with existing Loop source-worker steps completes
the source and real window maintenance produces4 initial events, with the outgoing
slot and pending gesture intact. Two initial fixture setup failures are preserved:
record helper had a pending job; trying its private clear method failed compilation.
Neither is counted as red-proof; the subsequent missing-request assertion is.
Focused123/123; full native2,776 total/2,769 passed/7 skipped/0 failed148.997s.
Capture build SUCCESS6.990s; RAM1 variables96,192/code391,512/padding1,704/locals34,880,
floor840 unchanged. Strict OpenSpec/diff pass. No commit.

## Device sequence and honest verdicts
1. `immutable-launch-device-baseline`: uploaded prior imagefb73aeed..., baseline
   INCONCLUSIVE_CAPTURE (missing persistence result, display-empty-after-overdub).
2. `immutable-launch-device-long-seed`: authorized24-bar slot5 seed, length18,432;
   INCONCLUSIVE_CAPTURE on active snapshot/persistence evidence.
3. `immutable-launch-device-switch`: old image queue34.099s, hold42.037s age3072,
   no commit. User interrupted and confirmed empty slot5 display/endless queue.
   `--boot-reset` was first rejected by wrapper argparse before device action;
   subsequent same-image upload succeeded. Initial runner-output interpretation
   said no gestures; raw capture corrected it and contains the queued switch.
4. Read-only observation with transport active buffered its responses. Authorized
   stop then captured fresh status and full64-slot inventory: targett7/s4
   `a=1,l=18432,v=1144,kp=1,kr=0,kc=0`. No content was cleared by observation.
5. Uploaded fix image5e8a81b1ba9c91e52f566c40a7d4e0101850ea19ccb016f16b80af1878f5723a.
   Explicit user approval: recreate slot4 only, preserve slot5. Unmodified wrapper
   `immutable-launch-device-switch-fixed` baseline PASS, but switch sentfalse:
   global last-state shortcut classified OD-stop ALREADY_SATISFIED as STOPPED.
6. TEMP variant `ROOTLESS-LAUNCH-STOP-REQUEST` of registered parent
   STIM-BASE-2-ONE-OD-01, via existing wrapper/runner: forces exactly one OD-stop
   request instead of the unscoped already-satisfied shortcut, keeps confirmation,
   disables retries. Canonical wrapper/stimuli unchanged. Executed adapter retained
   as TEMP_VARIANT.py.txt (evidence, not a registered/tooling release).
   `immutable-launch-device-switch-fixed2`: switch senttrue; device queue205.973s,
   hold213.925s, immutable readiness receipt233.572182s, actual LoopEnd commit
   233.897s `track=7 3->4 start=0 len=18432`. Cold wait27.924s. Generic baseline
   FAIL on injected held note83 (unmatched/out-of-range); NOT relabelled PASS.
   Ring shed3, no Tier-A eviction/critical rejection/overflow/wipe in reported stats;
   one head_bad/head_resync6-byte recovery retained. Not a pristine parity capture.
7. Standard content pipeline `immutable-launch-device-target-parity`,110s:
   dump1,144 effective rows; verifier INCONCLUSIVE_CAPTURE, no unique effective
   event recurs. Raw MIDI includes channel8 NoteOn878/NoteOff868 over109.873s;
   these are observations, not an inventory-equality proof. Display rows show
   slot4(zero-based) PLAYING length18,432 with nonempty frame253..273 and visual380.
8. Fresh final read-only capture `immutable-launch-device-final-observation/`
   session_20261004_213624.log: STOPPED,pending0,queue0/4; complete64-slot inventory
   has target `a=1,l=18432,v=1144,kp=1,kr=1,kc=1`. Stored event count preserved;
   the formerly missing root is now present/current. Device left stopped.

Specific bug outcome: observed endless source wait now completes; target content
and nonempty display/playback confirmed. NOT claimed: full content parity, cold
launch deadline compliance, pristine entire-run acceptance or continuous two-bar
qualification. The~28s cold-source delay is a remaining performance issue, not
hidden by extending the declared hold or changing capacity.
