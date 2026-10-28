## 1. Regression fixture and attribution (packet A)

Owner, preconditions, invariants, non-goals and working-branch boundary: [design packet A](design.md#execution-packet-a--regression-observation-and-fixture). These are fixture/observation tasks, not permission to change production scheduling. No firmware repair task is generated while packet B is blocked.

- [x] 1.1 Pin the dirty-tree baseline and image provenance in `evidence/REGRESSION.md`, including the current burst variant and exact A/B delta; verify neither an installed image nor a pre-change control is inferred from source comments or HEAD alone.
- [x] 1.2 Extend the existing native display-boundary harness with the combined 22-overdub/20-owner-turn trace using production admission/dispatch ordering, controllable clock and DMA completion; verify three healthy admitted display phases submit and separate completion retires the same generation, while the original Record and qualified closed overdub NoteIds remain exact. Keep queue cap 4 and final 23 capture passes, without rescue turns inside the measurement window. Record the first failing assertion or explicit NON-REPRODUCTION; a passing direct-call control is insufficient.
- [x] 1.3 Add the unchanged-viewport healthy-frame control plus busy-DMA/recovery and source-freshness controls; reuse existing Record-head, focus/slot, wrap, transport Stop, undo/redo and geometry controls where adequate. Verify the oracle rejects missing actual progress and missing expected NoteIds independently, but does not reject suppressed transition telemetry. Run `pio test -e native -f test_live_capture_display_boundary`, `pio test -e native -f test_shared_capture_finalization`, `pio test -e native -f test_midi_service_drain`, and `pio test -e native -f test_display_dma_health`; record actual case presence/results.
- [x] 1.4 Record the discriminating burst A/B result and the named last-progressing boundary in `evidence/REGRESSION.md`; if native reproduction is absent, record the remaining evidence gap rather than selecting a speculative fix. Complete packet B with the exact predicate, minimal mutation and bounded transition oracle only when supported; otherwise leave it BLOCKED. Verify the historical review, resource/timing consequences and focused contract review before adding production repair tasks.

## 2. Verification and handoff

- [x] 2.1 Run full `pio test -e native` after fixture work and strict OpenSpec validation; record failures and known characterization exceptions explicitly, never count a failing characterization as a green suite. Update runtime status with whether a repair is actually ready.
- [ ] 2.2 Before any target diagnostic is added, fill the HITL authoring recipe, list existing stimuli, specify a fixed-size capture-gated observation at the demonstrated boundary, and price RAM1/interval costs; verify no automatic heartbeat/serial flood, source mutation, upload, reboot, clear or SD action is implied. This conditional task is NOT APPLICABLE only if native evidence suffices; record why.

## Production repair gate (not checked off by artifact readiness)

Apply result: packet A is complete for the native half — 22 presses, exact content identities, 23 capture
passes, burst A/B identical (86 frames one-bar / 145 long-loop), and full native green. Packet B remains
**BLOCKED**: native does not reproduce the device symptom, and recorded DMA counters show transfers
continuing, so the remaining question is device-only stale composition/source content. Task 2.2 is
**not performed** — native evidence does not suffice, and no hardware run was authorized.

### Open follow-ups (not in this change)

- Retained-Stop + new-Start chains still lose older intervals' passes (17 of 23 in the harsh shape). Separate
  from display and not repaired here.
- Device identification of the stale-content boundary needs owner-authorized hardware time and a registered
  stimulus.

## 3. Production repair (DEC-179)

- [x] 3.1 Restore applies the saved passes into an empty slot on parity divergence and reports the outcome honestly (`LoopSlotBodyApply`, `Superseded` for kept-existing); verify `test_4_2_a_parity_mismatch_into_an_empty_slot_restores_the_saved_passes` plus the populated-slot control `test_4_2_a_failed_parity_leaves_the_existing_slot_generation_intact` pass (64/64 `test_storage_loop_io`).
- [x] 3.2 Selected slot paints first: stopped-lane focus predicate accepts root-only restored content, realtime idle lane serves the selected track before the background walk; verify `test_live_capture_display_boundary` 41/41 including the 22-press chain and the stationary-viewport control.
- [x] 3.3 Painted content tracks pass growth: `Loop::commitCapturePass` marks affected display ranges for every committed capture pass; verify `test_painted_notes_follow_committed_passes_on_a_stationary_viewport` passes and that it fails with the marking restricted to `OverdubWrap`.
- [x] 3.4 Gates: full `pio test -e native` green (3565 / 3558 passed / 7 skipped / 0 failed) and `teensy41-capture-serial` SUCCESS with RAM1 unchanged (`variables 96,192 / code 390,584 + padding 2,632`). Device verification, upload and archive remain open.

Packet B is blocked until task 1.4 pins the exact repair. At that point add the narrowly specified repair and native/build/device verification tasks under the reviewed packet. Existing source-lifetime, publication and timing assumptions must not be redesigned inside Apply. Device closeout requires both display progress and preserved stop throughput; boot delay and MIDI drift remain separately unproven. No archive or shipped claim from this fixture-only checklist.

## 4. Boot first-paint hold (DEC-183/184)

- [x] 4.1 Make the DEC-183 gate row reachable and truthful: initialise the RAM2 rate-limit stamp in `setup()` (an uninitialised `DMAMEM` word made `now - stamp` negative for the whole boot and silently suppressed every row), emit from the owner selection in `loop()` once per second while the boot title drain is active *or* a slot owes paint, and report the restore queue depth. Device-confirmed: `drain=1, restore=1`, queue `24 -> 0` (`session_20261028_214829`).
- [x] 4.2 Release the boot title hold at the focus slot's commit (`BootGate::focusSlotSettled` / `BootGate::bootTitleMayRelease`), keeping the whole-queue contract as the fallback for an empty selected slot; the remaining restores keep draining behind the paint-first background path. Fixture first: `test/test_boot_focus_handoff` (RED with the header absent, GREEN after), 8 cases.
- [x] 4.3 Device-verify on three boots in `session_20261028_215511.log`: focus commit -> first content **0.46-0.47 s** (was 4.16 s), no slot served twice in any boot, all 26 restores still complete, cache build bounded, content 104 -> 359 notes unchanged.
- [x] 4.4 Gates: full `pio test -e native` **3580 / 3573 passed / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with RAM1 unchanged (`variables 96,192`, ITCM `391,112 + 2,104`).

Remaining, not repaired here: absolute boot-to-content is still 7.0-9.4 s, dominated by when the boot restore queue starts
(3.86-6.12 s on these boots) and the 366 ms `boot_commit`. Recorded as boot cost, not a defect.
