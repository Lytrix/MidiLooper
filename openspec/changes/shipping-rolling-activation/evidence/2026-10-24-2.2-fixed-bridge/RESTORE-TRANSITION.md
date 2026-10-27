# 2.2 — restored NOTE_EDIT / launch-source transition

## Scope and execution packet

User requested NOTE_EDIT stop → reboot reproduction on the 2.2 image, display/prepare/slice/pair evidence,
a minimal boot/restore launch-seed/display fix, and rerun of 3.7e — launch adoption before checkoff.
Track 8 slots 4/5 explicitly authorized as scratch in this session; no other clears/quarantine authorized.
Baseline: `eb2f0bc98c21fd9eda7e0b508179b4697ed41b5c`, working branch `feature/unified-rolling-playback-path`.

Owner: existing Track source selector → rolling resolver → immutable published view. No new owner, allocation,
source construction, scheduling policy, persistence format, or display-data authority. `viewBuildWork = {}` clears
scalar admitted-work accounting, not buffer ownership (`PlaybackViewAdmittedWork`). No evidence yet attributes a
reboot to that assignment. Historical review: searched DECISION_LOG for launch/seed/display preparation;
DEC-152 — commit-bind seed and DEC-147 — traversal position stand. Reuse existing selector/launch test harness.
No architectural reassessment trigger identified for complete source-descriptor assignment.

### Oracle (before firmware edit)

A rolling view that the resolver accepts is selected as one tuple: `viewOut`, `viewOut->source`, null `bridgeOut`,
and rolling-selected true. A positioned interval may legitimately retain a copied revision while the live root
moves on; selection must not mix that view with the live revision or a compatibility bridge. Existing launch
and emission tests preserve source/storage identity, exact-once playback, interval expiry, queued-start guard,
and no redundant launch demand. RED must fail the source identity assertion before correction. Full native,
shipping build at existing RAM1 floor, focused review and 3.7e device rerun required. This does not claim that a
source-tuple defect explains the reported reboot; that attribution requires matching device evidence.

## HITL recipe

- Reused STIM-NE-RESELECT-STACKED-01, existing minimal NOTE_EDIT and handoff probes, and STIM-WINDOW-CONTENT-01.
- Entry points: `host_midi_hitl.py run --preset note_edit_reselect_stacked`, minimal/handoff probe scripts,
  `host_midi_hitl.py run --preset windowed_content_parity`; forced reboot observation uses `capture_session.py`.
- Existing gestures only; no new MIDI bytes, stimulus changes or firmware markers.
- Markers: existing `boot_reset`, `BOOT,load,ok`, display (`DISP` / `disp_window`), `prepare,slice,*`, LCR pair,
  launch/rolling receipts. Preparation slices are LCR work, not the removed playback prepared-pair family.
- Tier registration/tests: N/A (no new marker); missing qualification-only launch receipts on shipping are not
  absence evidence. Evicted evidence cannot qualify the gate.
- Baseline shipping build: variables 96,192; code 392,040 + padding 1,176; locals 34,880; floor padding 840.
- Pin: `14e2192e0ff3868afb5f403ed8913f07a4ec0880df317a0341240b5839ff0639`.
- Uploader: Homebrew CLI 2.3, GUI quit; successful programming + Booting, exit 0; USB 16C0:0489 returned.
- No task checkoff until device evidence qualifies the transition. Report failure/setup limitations explicitly.

## Initial device evidence

1. `captures/2.2-note-edit-stop-reboot-red-01/`: base seed PASS, NOTE_EDIT confirmed, stacked selection refuses
   movement (stable ack absent). Managed stop/drain performed. Forced reboot acknowledged, boot_loaded 3.9 s,
   `crash=0`; prepare pair and later phases complete. Host HEX metadata alone did not prove flashed bytes.
2. Minimal edit probe `captures/2.2-note-edit-stop-reboot-min-red-01/`: pitch64 200→1448, all other notes unchanged;
   subsequent reboot reaches BOOT load ok and completes pair preparation. Not a reproduced reboot failure.
3. Explicitly flashed the built/preserved shipping HEX above (`captures/2.2-pinned-image/shipping-14e2192e.hex`).
   Minimal probe `captures/2.2-pinned-note-edit-red-02/` PASS; playing/stop probe
   `captures/2.2-pinned-note-edit-stop-red-03/` completes stop/drain but no movement accepted (not route PASS).
4. `captures/2.2-s37e-shipping-red-01/`: registered 32-bar launch/switch recipe executed on pinned shipping image;
   display leg ok, overall exit2: Tier-A eviction delta3 and no target win_gate (qualification-only) under transport.
   Forced post-stop reboot acknowledges, boot_loaded 3.9 s; preparation advances beyond pair into projection/channel.
   No reproduced crash/hang yet. Gate remains non-PASS.

## Completion evidence

### Native/build/review

- RED: `pio test -e native -f test_runtime_working_set_ownership` fails exactly the launch source identity
  assertion: `Expected 1 Was 2` (view copied revision1, selected source revision2). An initial test-only link error
  from an unnecessary `setCurrentTick` call was corrected before this behavioral RED.
- Fix: `resolveQualificationPlaybackSourceForTurn` transfers `rollingView->source` and clears `bridgeOut` when
  selecting the rolling view, matching its other reader branches. No display/prepare scheduling or buffer release changed.
- GREEN: RWO + windowed preview integration **291/291**; full `pio test -e native` **3432 cases / 7 skipped /
  3425 passed**, exit0. One focused independent review: no concrete findings; it explicitly does not qualify reboot.
- Both shipping and qualification builds pass at **96,192 / 392,040 + 1,176 / 34,880**, no RAM1 re-base.
- Shipping HEX `4ce466e06c154ace0ad9fd0c937d2eb9c4d1f124e7d2c89db2427a9ddc123a18`;
  qualification HEX `de4d6a6fc94fb924d0be81137880dd83e13da04f09ff87ca11ef2531e158b864`.
- `openspec validate shipping-rolling-activation --strict` passes. No Git commit/push/PR performed.

### Qualification gate reruns (explicitly authorized)

Exact gate command (suffix 01, then 02):
```
.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity \
  --build-env teensy41-capture-serial-window-qualification \
  --track-number 8 --loop-slot 4 --midi-channel 8 --record-bars 32 --boot-reset \
  --switch-target-slot 5 --switch-settle-s 170 \
  --out-dir captures/2.2-s37e-qualification-fixed-02
```

- `captures/2.2-s37e-qualification-fixed-01/`: exit2; target does commit but its restored length25344 (33 bars)
  is not two-bar aligned (`al=0`). Initial chat inference that it never became active was corrected from commit rows.
  Tier-A eviction delta3 also present.
- Corrected setup ONLY: registered base record-only, host clock120, track8 slot5, record24 bars,
  `captures/2.2-s37e-target-even-seed-01/`: PASS, exact18432 ticks, 384 On/384 Off, no open notes, persistence ok.
- `captures/2.2-s37e-qualification-fixed-02/`: exit2; display leg and snapshot checks pass; alignment passes;
  no window overrun; evidence-ring fails (Tier-A eviction delta3). **The broader verifier's other green terms do
  not close the slice-specific continuation oracle.** Retained target rows independently show:
```
[233.532] [INFO] LoopEnd playback commit track=7 3->4 start=0 len=18432
#CAP,233571780,DIAG,win_gate,t=7,s=4,x=win_serving,len=18432,...,al=1,...,dm=0,bld=0,...,rq=0
#CAP,237538479,DIAG,win_gate,t=7,s=4,x=win_seed_refused,len=18432,...,rq=5
#CAP,237572894,DIAG,win_frozen_readiness,7,4,build_drop,handoff_unreachable,1,101376,6,1884179984,6250519,0,18432,5,7,0
#CAP,238249442,DIAG,win_gate,t=7,s=4,x=win_blocked_by_budget,len=18432,...,rq=12
#CAP,334922884,RING,tier_a_evicted,3,first=182904963
```
  Launch serves about39ms after commit, but four seconds later successor preparation enters repeated unreachable
  handoffs and budget refusal. No `win_adopted_launch` receipt is claimed: the commit now seeds before the owner turn.
  These positive rows establish a continuation blocker even though eviction makes absence claims unsafe.

### Fixed shipping restore / display → prepare → pair sequence

Fixed shipping image restored by CLI2.3 (Programming + Booting, exit0), USB16C0:0489 returns.
`captures/2.2-fixed-shipping-restore/` is attach evidence only: immediate reboot refused while restored transport ran.
Then existing `transport_stop_sender` used with capture retained through two stop/drain edges;
`captures/2.2-fixed-shipping-final-reboot/session_20261024_214917.log` confirms accepted reboot and boot_loaded3.9s:
```
#CAP,2164004,DIAG,boot_reset,crash=0
BOOT,load,ok
#CAP,11832592,DISP,3,STOPPED,24576,0,0,0,0,1,0,16,0
#CAP,12131311,DISP,3,STOPPED,24576,29,44,29,29,1,0,16,29
#CAP,13469992,DISP,3,STOPPED,24576,254,509,254,254,1,0,16,254
#CAP,37152981,DIAG,prepare,slice,pair,us=3188,units=8,resume=8,indiv=0
#CAP,37195332,DIAG,prepare,slice,pair,us=68,units=8,resume=16,indiv=0
```
The capture retains the intervening idx/chunk and subsequent pair slices; no crash/hang was reproduced on this run.
Initial shipping long reboot sequence is in `captures/2.2-s37e-shipping-red-01/reboot/` (pair begins35.778744s).
Full loader transcripts and exact HEX files retained under `captures/2.2-pinned-image/`.

## Disposition / next authorized blocker

**Partial fix only. 2.2 — fixed/bridge removal remains unchecked; 3.7e — device launch/continuation NOT qualified.**
The incomplete reader tuple is fixed and red-proved. The reported NOTE_EDIT stop→reboot crash is **not reproduced**
and must not be attributed to the tuple or bind-time scalar reset. Next production-path work is the measured
post-seed successor demand → unreachable handoff → budget refusal on the aligned restored target, followed by
an intact-evidence rerun. No bridge-storage/cold-wrapper retirement in this session. No new architecture decision,
owner, bound-class or exclusion; this is an implementation correction under the existing commit-bind seed contract.
Board left on fixed shipping `4ce466e0…`, transport stopped after the final capture. No non-scratch clears or quarantine.

Final checks: `git diff --check` and strict change validation pass. Final restored preparation completes at
84.373281s (`prepare,slice,state`, indiv1) with LCR pair summary84.373362s (`ent=512,ins=512,pu=512,po=510`).
Verification transcripts retained in `captures/2.2-source-tuple-verification/`. Original three untracked session
files left untouched; only firmware/test correction, runtime/task status and this evidence document changed.
