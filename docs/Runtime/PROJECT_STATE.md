# Project state (execution context)

**Current handoff — S4.1 lease release verified and checked; F4 completion-failure policy fixed (2026-09-26).**
Two slices since the last checkpoint. **S4.1's lease leg** is verified natively with no firmware change
(three fixtures; the retired slot is released by the next same-track playback reset, refused while a live
finalization owns it, and the protection is triple-layered and red-proved by staged guard removal) and **S4.1
is checked**, its device leg recorded as an evidence obligation because the `DIAG,ws_release,…` emission and
its Tier-A promotion already exist. **F4 / DEC-091** is a firmware change: a persistently failing workspace
completion is now **discharged and reported** instead of re-queued at the queue head, where it blocked every
other persistence item and kept `hasDeferredSaveWork()` true for the rest of the boot (refusing the save
pre-flight and the read-only slot-payload observation). Full native **2,469 passed / 7 skipped**;
capture-serial SUCCESS with `code 392,360 / padding 856` (byte-identical; the named queue API the first cut
added cost 256 B of ITCM and was withdrawn rather than widening the floor); `include/` untouched. Device
evidence for the failure path is owed — it is an SD fault path with no fault injection. Records:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — working-set lease release on a same-track slot switch" and § "F4 — a failed workspace completion is
discharged"; decisions `DEC-090`, `DEC-091`. Recovery hardware content/Undo/Redo acceptance and the parked
DMA repair remain open and unchanged.


**Previous handoff — S4.1 lease release verified; S4.1 checked; DMA still deferred (2026-09-26).**
S4.1's last behavior leg is closed with **no firmware change**: the working-set lease of a retired slot is
released by the **next same-track playback reset/switch** (not by retirement itself), refused with
`RefusedBusy` while a live finalization owns the slot, and the live-finalization protection is
**triple-layered** (track-level early return → per-slot predicate → the runtime's own `reset()` refusal),
each layer red-proved by staged guard removal. Three new fixtures in `test_runtime_working_set_ownership`
(23/23); full native **2,468 passed / 7 skipped**; capture-serial SUCCESS with `padding 856` (floor 840);
`src/` untouched. **S4.1 is checked**, its device leg recorded as an evidence obligation only — the
`DIAG,ws_release,<slot>,<generation>,<outcome>` emission already exists and is Tier-A, so a run plus an
oracle that reads the row is what is owed. Record:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — working-set lease release on a same-track slot switch". **Next recommended native slice:**
`FinalizeWorkspace` retry policy (a persistent completion failure retries at the queue head and blocks the
queue behind it; owes a bounded-failure `DEC` before code) — see
[continuation packet](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md). Recovery
hardware content/Undo/Redo acceptance and the parked DMA repair are unchanged and still open.


**Previous handoff — S4.1 persistence identity closed as verified; DMA still deferred (2026-09-26).**
S4.1's persistence-identity remainder is settled with **no firmware change**: the deferred save's footer
records the workspace focus at **footer-write time** (content identity stays the interval's slot), verified
by four new cases in `test_storage_drain_restore` that pause the real worker with the footer due, hand off
A→B through the production selection route, and reload — plus a no-switch control, a post-footer handoff and
an SD write fault; red-proved. Full native **2,465 passed / 7 skipped**, capture-serial SUCCESS with
`padding 856` (floor 840), `src/` untouched. **DEC-090**; record:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — persistence identity during handoff". Next recommended native slice: S4.1's remaining
working-set **lease** release on a same-track slot switch, and then the `FinalizeWorkspace` retry-policy
question found while building the fixture — see
[continuation packet](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md). Recovery
hardware content/Undo/Redo acceptance and the parked DMA repair are unchanged and still open.


**Previous handoff — recovery committed; DMA deferred (2026-09-26).**
Recovery/loaded undo is committed in `f29539e3`. Automatic repair and healthy boot
completed on hardware; full device content/Undo/Redo acceptance remains open.
Blocking-SPI display update is user-confirmed. **User parks DMA repair until after
`runtime-working-set-ownership` completes.** Default firmware still enables DMA;
use the preserved comparator profile when an explicitly approved upload must retain
the working display. No timing gate is waived. Next recommended native work:
**S4.1 — persistence identity during handoff**, starting with a paused-worker
contract fixture, not an assumed active-slot replacement. See
[continuation packet](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md).

## Earlier session checkpoints


**2026-09-26 — blocking-SPI visual comparison PASS (user observed).**
User confirms “Yes, the screen updates now.” Same recovery/renderer source, existing
`MIDILOOPER_DISPLAY_DMA=0` switch only. Device reached load=ok, restore admission
complete, undo hydrated and transport stopped. This isolates the frozen-screen
behavior to the DMA display route; it does not establish the precise DMA defect.
Blocking SPI remains installed as a diagnostic workaround; no real-time timing
acceptance or permanent DMA fix claimed. Serial capture remains open.
Evidence: `captures/display-blocking-diagnostic-20260926/metadata.json` and session log.


**2026-09-26 — blocking-SPI diagnostic uploaded with explicit user approval.**
Persistence idle verified before reset. Capture-serial upload SUCCESS using
`captures/display-blocking-diagnostic-20260926/platformio.ini`; actual device
`#DMA_XFER` shows init=0, dma_frames=0, increasing blocking_frames. Serial capture
remains open. Visual outcome awaiting user; no display fix or timing gate claimed.
Recovery source unchanged. Image/config identity is pinned in that capture directory.

**2026-09-26 — display issue after recovery upload.** User sees a frozen first
piano-roll frame while MIDI and renderer/DMA counters progress. Cause unproven.
Existing blocking-SPI diagnostic variant builds; not uploaded. Capture stays open.
Evidence/config: `captures/display-blocking-diagnostic-20260926/`.

**2026-09-26 — recovery firmware uploaded with explicit user authorization.**
`teensy41-capture-serial` upload SUCCESS; SHA256
`4b13749ab9d97b0699fb02f813a3a9a26b7475a60391e19ffa784c21cec93255`
matches the host-verified image. Confirmed standalone read-only reader before upload;
archived and cleared the incident hold for this approved recovery installation.
Device reports runtime metadata repaired, `BOOT,load,ok`, and a settled checkpoint:
`admission_complete=1`, `interactive_ready=1`, `undo_hydrated=1`,
`transport_stopped=1`, `deferred_prewarm_idle=1`, `free_chunks=391`.
Serial held open through verified persistence idle, then closed. No content/clear
stimuli. This establishes successful recovery boot, not per-loop content parity,
hardware Undo/Redo roundtrips or second-boot acceptance; those remain open.
Evidence: active-change workspace-loss `authorized-recovery-upload/`.


**2026-09-26 — automatic boot metadata repair implemented and host-verified.**
DEC-089 — boot metadata repair validates the complete workspace before generating
runtime metadata; preserves source files and corrupt originals; blocks saves after
refusal. Actual boot owner restores the copied 64-slot library and derives 122 undo
actions; healthy reboot retains them. Six isolated cases include 276 interruption
cuts. Full native 2,461 passed / 7 skipped; Python 16; capture-serial SUCCESS with
RAM1 padding 856 (floor 840). No upload/device action; hardware acceptance and the
incident hold remain open. Evidence: active-change
`evidence/2026-09-26-workspace-loss/BOOT-REPAIR.md`.


**2026-09-26 — approved load-time undo implemented; host recovery passes.**
DEC-088 — load-time undo reconstruction supersedes the earlier zero-after-load
policy. All 122 actions derive from saved passes on load/reload with empty undo
headers; all 60 nonempty slots pass Undo/Redo, and background commits preserve
runtime Redo. Redo starts empty and is created by Undo. The isolated runner verifies
hydration before/during/after restore. Candidate reconstructed-undo-04 preserves all
65 original files plus a 628-byte bundle (443,431 bytes total). Capture-serial build
passes with RAM1 padding 840, unchanged. No upload/device/card action; standalone
reader and incident hold remain. Hardware recovery is still unverified. Evidence:
active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-FIX.md`.

**Verification complete:** full native 2,461 passed / 5 skipped; focused native 68
passed; recovery load/reload 122 actions and all 60 nonempty slot roundtrips;
Python 16 passed; OpenSpec validation passes. Capture-serial firmware SUCCESS,
RAM1 padding 840. No device actions. See UNDO-LOAD-FIX.md and preserved logs.


**2026-09-26 — correction: reconstructed undo after reload needs a policy change.**
The prior “next fix” diagnosis missed DEC-057 — snapshot body format and the active
working-set spec: LSV1 load deliberately starts with undo depth zero. The older
content-history reconstruction spec has a newer snapshot delta that changes it.
The 122-action host recovery remains valid; it is not authorization to reverse the
load contract. Proposed change: derive per-slot undo from sufficient saved passes,
redo empty, saved snapshot authoritative, no persisted undo stack. Approval is
required before that normal-load behavior changes. No firmware/device action.
See active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-POLICY-REVIEW.md`.


**2026-09-26 — undo reconstructed and host-tested; reload blocks installation.**
Existing C++ content-history owner derives 122 actions (60 record, 11 overdub,
51 boundary changes) across all 60 nonempty loops. Full Undo → Redo preserves
materialized MIDI bytes, loop lengths and published note identities/content on all
60; four empty slots have no actions. Diagnostic bundle: 8,846 bytes. Production
load/hydration fixture retains only 9 actions after reload, so the wrapper exits 2
and the candidate is not safe to install. Original files byte-identical; firmware
unchanged; no device access; hold remains. Original cross-slot chronology, clear
snapshots and old redo branch remain unrecovered. Evidence: active-change
`evidence/2026-09-26-workspace-loss/UNDO-RECONSTRUCTION.md`.


**2026-09-26 — workspace reconstruction passes host load/restore acceptance.**
Candidate `captures/midilooper-folder-copy-20260926-195228/reconstructed-03/candidate/`
preserves 65 original files and adds a 628-byte runtime bundle (total 443,431 bytes).
All 64 slots restore; 7,840 capture-event records match source byte-for-byte, 57 snapshot
inventories match. Explicit replacement settings: stopped/120 BPM, first nonempty
slot per track selected/enabled, unmuted, fresh global undo. Source decode 3/3;
reconstruction 2/2; affected ordinary-native regression 12 passed/2 explicit skips;
Python 16 passed. Reusable C++ writer is host-tested only; automatic device recovery
is not implemented. No SD-card writes/upload/device access. Hold remains latched.
Evidence: active-change `evidence/2026-09-26-workspace-loss/WORKSPACE-RECONSTRUCTION.md`.
S1.6c.2 — capacity revalidation remains blocked on actual hardware recovery/measurement.


**2026-09-26 — host production decoder audit complete.** All 64 copied current
slots decode using an isolated Teensy-compatible event-layout adapter: 7,840 raw
MIDI events; 57 effective snapshots with 2,766 note identities. Three nonempty slots
use raw pass format; four slots empty. Direct Mac decoding was invalid because
MidiEvent is 32 bytes on Mac versus 20 on Teensy. Firmware unchanged. Three focused
native tests pass through the repeatable host wrapper; no device access. Evidence:
active change `evidence/2026-09-26-workspace-loss/PRODUCTION-DECODE-AUDIT.md`.
Runtime metadata reconstruction/load validation remains next; no restore candidate
approved, normal firmware hold unchanged, S1.6c.2 — capacity revalidation blocked.


**2026-09-26 — offline recovery audit complete; no device access.** All 64 current
slot files pass the firmware checksum/footer checks; 60 have nonzero loop lengths.
60/64 lengths match the preserved pre-loss census; differences are track 8 slots
4–7. Saved revisions hold at most 22 nonzero-length slots, so current files remain
the primary recovery candidate. Full note decoding and compatible workspace metadata
are not yet validated; no restore candidate is approved. S1.6c.2 — capacity
revalidation remains blocked. Evidence: active change's
`evidence/2026-09-26-workspace-loss/OFFLINE-RECOVERY-AUDIT.md`. 16 host tests pass.
Normal firmware/recovery hold stays latched; standalone reader remains installed.


**2026-09-26 — USB folder copy COMPLETE, authorized reader installed.**
Copied only `/MidiLooper` through the standalone read-only reader with the card left in place:
**1,007 file entries / 6,837,184 bytes**, transfer CRC32 and independent local SHA-256/length/CRC
verification all pass. Output: `captures/midilooper-folder-copy-20260926-195228/download-05/`;
report: `captures/midilooper-folder-copy-20260926-195228/verification.json`.
Contains 65 current files (64 slots plus workspace metadata), 73 saved-set files, and historical
`current.bad.*` trees. This proves surviving files, not their semantic validity or recoverability.
One duplicate path, `current.bad.211210/workspace.bin`, has distinct 154-byte and 77-byte entries;
both are preserved (second under `duplicate_entries/000854.bin`), not overwritten. Initial transfers
are retained as incomplete evidence; USB per-line acknowledgements fixed unreliable bulk sending.
Reader firmware SHA256 `a915b18c5a12175ef7c25dddcc8932840418fd61018ca4201ade467e78cacce2`;
9 host tests pass. No SD writes, normal-looper boot, recovery or restore occurred. Reader remains
installed; normal-looper upload/recovery hold stays latched. User's explicit approval covered this
reader installation and folder copy only. Recovery analysis can now use the host copy.

**2026-09-26 — standalone USB reader installed with explicit user approval; copy pending USB port.**
User approved replacing/resetting the running firmware solely to read `/MidiLooper`. Standalone
`pio run -d tools/midilooper-reader -e teensy41-capture-serial -t upload` succeeded. macOS
sees USB Serial `16c0:0483`, serial `15494480`, but has not exposed a `/dev/cu.usbmodem*`
port. No export command was sent and no SD access occurred through the reader. Asked the user
to allow the accessory if macOS prompts. Installation evidence:
`captures/midilooper-folder-copy-20260926-195228/installation.json`. The normal-looper
upload/recovery hold remains latched; do not restore the normal firmware automatically.

**2026-09-26 — USB folder reader prepared locally; no device access.** User requests only
`/MidiLooper` over USB, leaving the card in place and unrelated data excluded. Added
`tools/midilooper-reader/`: separate read-only utility firmware (no looper boot recovery/save
code) plus a streaming Mac downloader with file CRC32, SHA-256 manifest, path checks and
incomplete-transfer handling. Seven host tests and the standalone Teensy build pass.
NOT uploaded or device-verified; incident hold remains set. Installing the reader replaces
and resets the running firmware and needs explicit approval. Folder-only copying supersedes
full-card imaging as the requested approach, without authorizing upload, repair or device access.
Contract and commands: `tools/midilooper-reader/README.md`.

**2026-09-26 — INCIDENT: the device workspace no longer loads; host shutdown defect fixed (host-only; no device access).** The device comes up empty: `CurrentSet meta missing .../current/temp/runtime.bundle.bin`, `Current workspace load failed`, `Boot recovery chain exhausted; starting empty`, `BOOT,load,fail`, `free_chunks=480/512`, all 64 slots empty — where the same library read **58 committed / ~386-403 free** minutes earlier, and the post-loss observation is *complete* (64/64 slots, `incomplete_window: false`), so it is not an artifact. **Cause, and it is this session's tooling:** the deliberate clear of the authorized target admitted a deferred workspace save, and `--prepare-only` returned immediately, closing the serial port mid-write (that session's last line is `PERS,work,...,TrackMeta,track:7,start,ok` with no completion); the next run's upload then reset the board on top of it. **Established:** the load failure and the empty runtime. **NOT established:** that the slot payloads are erased, or the exact failure chain — a load failure is not an erasure, and the card may still hold every payload and saved set. **Fixed (host-only):** `scripts/hitl/device_shutdown.py` — canonical stop → drain → stop → drain, completion **verified** through the existing idle owner (never slept), judged from the log **tail** (anchoring at "now" reads an empty suffix as idle and reported success before the save was admitted — a real defect in the first cut, caught by its own test), disconnection/timeout named apart, incomplete shutdowns **latched**, and `DeviceSession` draining on *every* exit (normal, early return, exception) rather than per-`return`. The driver and `hitl_boot_census` both refuse an upload/reset while latched; the hold is currently set with the incident context. 15 host tests use the incident's own preserved logs as the decisive fixture (clear-run reads active, clean run reads idle — a fixed sleep could not tell them apart); a false green in the structural test was found by red-proving it and fixed. Evidence preserved with per-file sha256, commands and build identities under `openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/` (byte-exact via `.gitattributes`), and the read-only SD imaging procedure awaits authorization. **Device validation of the shutdown fix is PENDING** — it is host-tested only and cannot be device-validated while the hold is latched; its tests support the correction, they do not demonstrate it on hardware. **S1.6c.2 — capacity revalidation is BLOCKED (hard):** it needs the loaded N=56 library; an empty-library run cannot replace that experiment. No boot, no card access, no quarantine, no load, no rebuild.


**2026-09-26 — S4.1 reconciliation: the A/B/C run is the release acceptance, and the mid-pass journal resolver is fixed.** Owner correction, verified against the code: the accepted-switch retention run is not merely *related to* S4.1 — it is S4.1's device acceptance. "The slot leaves the active set" is A switching to B while its deferred finalization holds the occupancy; "the guard reports no live owner" is the release marker's `captureStopOwner == nullptr` clause, which is the `ownsLiveFinalization` term of `noLiveOwnerForSlot` (S3.2); it is identity-qualified (`canonical_release,4,40,6144,8` names the retiring owner's slot **and generation**); and **C's start is what performs the guarded working-set release** — `startRecordingTrack` → `releaseBackgroundPlaybackMergedMidiEventsMemory` → `releaseEligiblePlaybackRuntimeSlots` → `releaseSlot(slot, generation)` — followed by C's reuse, which is S4.3 verbatim. **S4.1's "rerun blocker" text was stale:** the per-interval ledger clear is converted (`clearStoppingIntervalLedger()` clears its own slot; the comment names the old global sweep as the blocker), `resetAll` is deleted and replaced, the only remaining `clearAllLedgers()` caller is the deliberate `sendAllNotesOff` sweep, and the slot-threaded persistence reads are converted. **Genuinely still wrong and now fixed:** the mid-pass journal resolver read `getActiveLoopIndex()` for a **stop in flight**, so an interval pinned to slot 5 with the active slot moved to 7 was journalled under 7; the selected-track fallback keyed a queued chunk to whatever slot was selected when the drain ran. The rule moved to its owner as `Track::captureWorkSlot()` (with `Track::pinnedStopSlot()`), refuses rather than guesses, and the resolver is now a thin loop over it. Fixture `test_s4_1_capture_work_slot_uses_the_pinned_interval_slot` asserts 1 while the active index is asserted to be 3 (so a rule that read it cannot pass), plus a refusal control and a live-capture control; **red-proved** (*Expected 1 Was 3*). Full native **2,459 passed / 1 skipped**; firmware SUCCESS, `code 392,376 / padding 840` (floor; the rule is FLASH-resident). **Recorded, not quietly dropped:** the working-set lease release is currently *inferred* from the sweep mechanism rather than asserted by name (the oracle asserts L4's finalization-capacity release); and `DeferredSaveJobStages`' footer read is a **design question** — the deferred save walks *live* state per stage, so capturing only the footer at admission would make the saved image internally inconsistent, and a snapshot-at-admission save owes its own `DEC`. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.1 reconciliation".


**2026-09-26 — S4f.3 device acceptance re-run after the Tier-A fix: `RETENTION_PASS`, exit 0, zero concessions.** Run `captures/s4f3-retention-20260926_181732`, identity **`git_commit 5629e7e4f96d` / `firmware_hex_sha256 da59a7eeb3e0`** (a firmware hash distinct from every earlier run, so this is the promoted image). **20/20 checks `ok`, `concessions` empty.** The point of the run is that the rows whose absence forced `INSUFFICIENT_EVIDENCE` at 17:56 are now present in a completed run: `DIAG,reentry_window_open,4,1121,6144`, `DIAG,canonical_release,4,40,6144,8`, `DIAG,reentry,deny,1311,2,5,1` and `DIAG,reentry,bound,1706,2,5,0` — while `capture_integrity` again reports no ring eviction, the signature that the evidence was being *shed*, not evicted. **The corrected L5 ordering holds:** the bind (59,698,903 us) precedes the release record (59,723,633 us) by 24.7 ms, and the release is now read as an informational maintenance observation rather than the availability edge. Window **1550.3 ms**; C's retained request was **staged** inside it with no start executed before the release, and exactly one `RECA,6` follows the release. **L7 is loop-scoped and the run shows why:** the whole-playback review returns FAIL/`inventory_equal=False` on all 36 cycles because track 8 slots 1–4 still hold committed content whose notes sound alongside the loop under test (reported as `EXTRA`); the deciding evidence is the dump's own completeness (`declared counts {'effective#0': 2, 'record#0': 2}` match parsed) with note 67 in **both** storage and effective rows agreeing on tick/type/channel/note/velocity, `storage_matches_effective=True`. **Staging was verified, and the clear is attributable:** targets cleared as a separate deliberate step (never silently), then a read-only census re-verified eligibility — committed count **61 → 58**, exactly the three slots — and the run was gated on that census with `--no-clear`. **Driver bug found by running and fixed (`5fc74bf9`):** the prepare reported `clear_5_wrong_slot` for all three targets while the device had cleared exactly the right slots (target 5 → `active_slot=4`, 6 → 5, 7 → 6) — the firmware identity fields are 0-based and the driver's `slot_number` is 1-based, compared raw; the presses were always correct and only the verification was wrong. Invisible before the identity existed, since any marker was accepted — which is how a preparation could report success on a slot it never cleared. Also recorded: the driver's live `transport_stopped` returned false while the take committed and L7 read a complete dump, so the live check is a convenience, not a gate. **Scope closed: retention plus successor capture at that precise scope.** **Still open and not claimed:** A/B content preservation (the dump covers the active slot only), the `companion path stalled; raw pass only` fallback's content consequences, and MIDI-output playback parity, which is separate from this loop-scoped parity. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "The rerun after the Tier-A fix".


**2026-09-26 — the identity-grade evidence was being *shed* by design; logging fixed, no device run.** The unexplained `INSUFFICIENT_EVIDENCE` on the verified-empty rerun is explained: **it was never a deferral or flush problem.** `CaptureLineTier::isTierALine` decides what the timing-critical shed may drop, and it drops non-Tier-A diagnostics **silently, by design, without reporting a loss** — and none of the three identity-grade tags were in its allowlist (`grep` count 0 for `DIAG,reentry,`, `DIAG,reentry_window_open,`, `DIAG,canonical_release,`). So the shed removed exactly the rows the acceptance is computed from, while `capture_integrity` correctly reported no *eviction*: shedding is not eviction. **The three tags are now Tier-A** (Tier-A is displaced only by Tier-A; these are a handful of one-shot edge rows per run), costing **no RAM1** — the predicate is called by a `FLASHMEM` function, so `code 392,376 / padding 840` is unchanged at the approved floor. **A latent bug in that very fix was caught before shipping:** the comparison was first written with a hand-counted length of 24 for `"DIAG,canonical_release,"`, whose `strlen` is **23**; including the NUL makes `strncmp` match only a tag that ends there, so the real row would never have matched and the evidence would have been shed again with the same silent signature. It now uses `strlen(...)`, pinned by a boundary test and red-proved (restoring 24 fails it). **The timely edges now carry identity as well**, so attribution no longer depends on the ring: `Overdub stop refused: <reason> slot=<n> interval=<n>`, `Track cleared @ tick <n> (active_slot=<n>)` (appended through `logTrackEvent`'s existing format parameter, so the established prefix survives every existing substring matcher), and both `Clear ignored — … (track <t> slot <n>)` forms. **Consequence — preparation is auditable:** `_clear_slot` used to accept *any* `Track cleared`/`Clear ignored` marker, which is exactly how a preparation reported success on a slot it never cleared; it now parses the identity and returns pass / fail (a clear naming a **different** slot → retry, not success) / unknown, recorded as `identity_grade: false` rather than silently counted as proof. Bases are explicit because mixing them is the trap: `active_slot=` and the `Clear ignored` field are 0-based (matching `#CAP` identity and the census), while `MIDI: Clear selected slot N` is 1-based and deliberately not used for identity. **Also fixed:** the oracle's wire-form docstring documented `DIAG,canonical_release` with 3 fields where the emitter and regex use 4, and a **stale host test** (`..._blocks_clear_without_retry`) supplied two sequencer probes for a retry path that makes three, erroring on `StopIteration` regardless of this change (confirmed pre-existing by stashing); it is replaced by two cases asserting the current, stricter behaviour. **Verification:** firmware SUCCESS (`padding 840`), all four new strings in the fresh ELF; native **2,457 succeeded / 1 skipped** (up 2); oracle 39 green (up 3), gesture plan 22 (up 7), clear precondition 28, capture transitions 48. Both new guards red-proved by mutation. **The device gate does not move:** retention plus successor capture still needs a rerun reaching `RETENTION_PASS` with zero concessions; what changed is that the previously-unexplained `INSUFFICIENT_EVIDENCE` should no longer be reachable for this reason. A/B preservation, the raw-only companion fallback and playback-output parity remain open. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "The evidence edges are Tier-A now".


**2026-09-26 — S4f.3: the concession loophole closed and the evidence gaps filled (no device run).** Conceded links no longer yield a pass: **`RETENTION_PARTIAL` (exit 5)** is its own verdict and `RETENTION_PASS` (**exit 0**) is reserved for the complete, unconceded chain — `--allow-setup-issue` now counts as a concession too, since allowlisting a report issue is an operator claim rather than evidence (a partial result must not close the device gate). **L5 closed:** `beginDeferredOverdubStopForSlot` now emits `reentryDecision("bound", tick, interval, slot, depth)` on the admission-committed edge, reusing the existing emitter and wire format, so a denial and its later binding correlate on the **same interval identity** instead of being inferred; a `bound` record for the denied interval *before* the release is a `RETENTION_FAIL`. **RAM1 floor preserved and measured:** `padding` byte-identical at **840** with FLASH `+104 bytes`, because the function is `TRACK_COLD_MEM` (FLASH-resident) and never enters the ITCM segment the floor measures — no recovery was needed. **L3:** the retained row now names track *and* slot, and the oracle requires both plus positional ordering (recorded as positional, since a human log row has no timestamp). **L7:** now requires the **existing content pipeline** — the `!CAPTURE_CONTENT` dump for the successor slot's real pitches plus `content-review.json` parity; a review alone is parity-only and insufficient. **36 host tests green**, including the complete unconceded chain reaching `RETENTION_PASS`/exit 0 with zero concessions; firmware SUCCESS. **Step 3, the first authorized no-concession device run, was NOT performed:** it needs a host sequence for the real gestures, and that flow does not exist (`queued_switch.py` is a Phase-1 skeleton; the action layer has no per-slot record press or slot/track select). Building it is host work with device round-trips, so it is recorded as the next step rather than rushed. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (the concession loophole closed).


**2026-09-26 — S4f.3 retention oracle tightened (still no device run).** The first oracle could falsely report `RETENTION_PASS`; the owner reproduced two cases and both are now refused: C starting **before** A releases capacity (was a pass → now `RETENTION_FAIL`, exit 2, failing L3 and L6) and removal of B's **identity-specific** denial with the generic `successor_stop_denied_capacity` row left in place (was a pass → now `INSUFFICIENT_EVIDENCE`, exit 4). The oracle now demands an **ordered chain** — L1 A's finalizer outstanding → L2 B's stop denied **with slot and interval** → L3 C's request retained → L4 capacity released → L5 that same B stop bound → L6 C starts exactly once **after the release** → L7 C recorded the expected MIDI → L8 bounded follow-up with no duplicates — where any missing or reordered link is never a pass. New verdict `INSUFFICIENT_EVIDENCE` (exit 4) separates "staged but a link lacks telemetry" from `SETUP_INCOMPLETE` (exit 3, "this run is not evidence"), and integrity gates now block a pass on missing build identity, ring overflow, a truncated log, unconfirmed setup transitions, and any base-report issue that is not explicitly allowlisted. **Telemetry gaps are now reported rather than inferred:** L5's identity link is unavailable (`captureInterval_` is emitted only by `reentryDecision`, while stop-tier records carry `frozenGeneration` = `staging.captureGeneration`, a different identity space), L7 is count-level because no per-note capture record has a caller, and L3 rests on a human log line — each passes only via an explicit, recorded **concession**. **Corrections:** exit-code separation does not establish that out-of-range notes were generator artefacts (an allowlisting is recorded as an operator claim), and the earlier "no finalizer-pause control is required" is **withdrawn** — the 1.335 s window is a timing observation from a session with no boot banner, so the hook stays a contingency. Evidence: 29 host tests (one negative per way to lose a link), CLI reproductions at exit 2 and 4, and the real log returning `SETUP_INCOMPLETE` (exit 3) while still measuring the window; three oracle bugs found by its own tests were fixed. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (the retention oracle tightened).


**2026-09-26 — S4f.3 device-retention stimulus and oracle prepared (NOT run).** Per owner direction, prepared only: no device run, no target slot assumed authorized. **Assessment — telemetry and exposure already exist, so no new instrumentation and no pause hook is needed.** The requirement is decidable from existing records keyed on slot and interval identity: `DIAG,reentry_window_open,<slot>,…` (DEC-067 `successor_start_window_open` = the predecessor's finalizer admitted and outstanding, named by slot), `DIAG,canonical_release,<slot>,…` (the denied window closes), `DIAG,reentry,deny/deny_resource,<tick>,<interval>,<slot>,<depth>` plus `ODUB,stop,admission,…,successor_stop_denied_capacity`, and `RECA,<slot>,<tick>` (the start, so exactly-once is a count) — never the 21 µs `OVERDUB_STOPPING` state that defeated the interruption fixture. **Measured window:** real device records `reentry_window_open,0,16387,1536` (136,629,769 µs) → `canonical_release,0,1536,8` (137,965,158 µs) = **1,335,389 µs (1.335 s)** for a 2-bar loop, comfortably wider than two host gestures; that session has no boot banner, so the figure is a timing observation to reconfirm under a fresh boot. **Oracle:** `scripts/hitl/retention_probe.py` → `retention-review.json`, verdicts and exit codes keeping setup distinct from retention (`SETUP_INCOMPLETE` 3, `RETENTION_NOT_EXERCISED` 2, `RETENTION_FAIL` 2, `RETENTION_PASS` 0), with `fresh_boot`, `tempo_verified` (read-back required), `seed_content_on_a`, `finalizer_admitted_on_a`, `capacity_released_after_a`, `capacity_denied_window_wide_enough`, `successor_stop_admission_denied` (inside A's window), `successor_started_exactly_once`, `activation_invariant_held`, `no_stop_obligation_replay`. Validated without a device: **11/11 host tests** (`scripts/test_retention_probe.py`) and a real-log run that returns `SETUP_INCOMPLETE` while still measuring the window. **Stimulus:** `STIM-BASE-2-ONE-OD-01-TEMP-RETENTION-A` authored via the sanctioned `stimulus temp` fork, not registered, sidecar under the change directory; pass criteria live in the oracle. **Setup gap:** the three targets (A/B/C on one track) are **not yet authorized** — the documented scratch slot is a single slot (track 5 / slot 1). **Adjacent defect, reported not fixed:** `scripts/hitl/stimuli/yaml_view.py` `dump_yaml` emits invalid YAML (dict branch tests `isinstance(item, list)` while `StimulusSpec` fields are tuples). Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (device-retention stimulus and oracle).


**2026-09-26 — S4f.3 HITL attempt — no pass, and the new path is uncovered.** Flashed the verified image (`firmware_hex_sha256 14a1aea7…`, `git_commit e743b97c…`) and ran the registered entry points per the hitl-content-verification skill (fresh upload; documented scratch slot **track 5 / slot 1**). The stop-interruption wrapper reached the fixture (`PLAYING->OVERDUBBING: 1`, record `32/32`, persist `ok=True`, `OVERDUB_STOPPING: 1`, intervention sent) but returned `INCONCLUSIVE_CAPTURE` / `no_device_stop_inside_stopping`; the log shows the `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` window is **21 µs** because the admitted stop defers finalization (DEC-078), so the fixture's premise is architecturally mismatched rather than flaky, and no delay value can hit it. The base leg's `overdub_unmatched_open_notes` / `overdub_out_of_range_notes` are the skill's setup verdicts. The baseline preset (`STIM-BASE-2-2-01`) returned `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` with `RECORDING: 0` — the known base-preset setup race already attributed to the still-open selection fixture — at a correct tempo (`record loop length 1536/1536`). **Tempo must be re-pinned per run**: the device was found at 160.0 BPM and the DEC-086 fader pinned it back to 120.0. **No registered stimulus covers this slice's behaviour**, so these runs are regression context only, and the owner declined a temporary variant this session — the retention path is **host-verified only**. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3 (HITL attempt).


**2026-09-25 — S4f.3 (PARTIAL 3) — accepted-switch retention under unavailable stop admission.** A switch/start requested while the predecessor's stop **cannot be admitted** is no longer dropped: `TrackManager::retainRecordStartForSlot` records it in the **existing** manager pending-start owner (no new owner, no new storage) without moving the audible set, and `handleToggleRecordForSlot` calls it on the uncommitted path. `finalizeCaptureAndSelectSlot` now performs **explicit supersession scoped to the finalized slot** (`clearQueuedRecordingTrack(trackIndex, captureSlot)`, plus dropping the retained request's phase reference when it is that slot) instead of clearing the whole track's pending-start state before it knows the switch will commit. Three corrections came with it: the overdub guard is **slot-scoped** (`hasLiveCaptureStopJobForSlot` / `hasAcceptedInteractionForSlot`) because `hasPendingDeferredOverdubStop()` is **track-wide** and would let another slot's job stand in for this interval's unadmitted stop; re-entering `stopOverdubbing()` on an already-admitted stop is avoided because it **re-latches** (its guard fires when a job is already pending) and would block the very switch being made; and **the activation hazard was found and fixed** — `handlePendingRecordStart` activated with the unconditional `Track::setActiveLoopIndex`, moving the active slot underneath an unadmitted predecessor stop, so it now uses `TrackManager::setActiveLoopIndex` and **explicitly asserts** that a failed activation leaves the slot unchanged with the predecessor's stop in flight. Evidence: `test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds` (A's stop held, B overdubbing with its stop capacity-denied, C's switch/start retained through 2,304 dispatches, capacity released, B's stop binds, C starts and records, no double execution), **red-proved three ways** with distinct failure lines, and a **false green found and removed** (the replaced fixture returned early from a characterization branch in the default gate while holding nothing). Full native `2,448 passed / 1 skipped`; firmware SUCCESS. **RAM1 is now exactly on its approved floor** (`code 392,376 / padding 840`; 272 bytes spent, gating the diagnostic recovered none because the strings are FLASH-resident) — the next slice here must free RAM1 or revisit the floor. **Immediate repeated handoffs (B→C while A finalizes) remain explicitly open**: retention is proven, immediate execution is the separate finalizer-capacity question. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3.


**2026-09-25 — S4f.3 (PARTIAL 2) — the switch carries the start.** `finalizeCaptureAndSelectSlot` returns whether it committed, and the capture-switch branch of `handleToggleRecordForSlot` queues the record start for a record target at that commit point with **no** phase reference, so an empty/cleared target starts on the transport's bar grid and the predecessor's loop timing never becomes an accidental dependency of the target's start. Fixture `test_the_switch_gesture_carries_the_start_without_a_second_press` asserts the request after one gesture and drives the dispatcher with no further press; red-proved by disabling the carrier (`1 failed, 11 succeeded`) while the two-gesture acceptance fixture still passed, so it discriminates. Green both gate modes; full native `2,448 passed / 1 skipped`; firmware SUCCESS, `code +64 / padding -64`, above the 840-byte floor. Still open: the **uncommitted** switch — reachable when `stopOverdubbing` denies admission on capacity (DEC-067), where the press is dropped rather than retained — plus the unconditional pending-record clear at the top of the switch path. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.3.

**2026-09-25 — S4f.6 — accepted-start carrier; S4b.1's acceptance passes end-to-end.** `TrackManager::startRecordingTrack` returns `bool` and refuses **per slot** (`Track::hasStopInFlightForSlot`), so a stop bound to A no longer blocks a start on B; `handlePendingRecordStart` activates the target first and consumes the request only on success, so a refusal retains it. **Retraction:** the refusal did not "strand" a retained request — the dispatcher consumed **unconditionally**, so it **discarded** it; the earlier path review's mechanism sentence is corrected in the packet. The acceptance fixture's characterization branches are deleted (claims unconditional, fallback press removed) and it is **red-proved** against the pre-S4f.6 guard (`2 failed, 10 succeeded`); the unadmitted-stop fixture is renamed because its old name overclaimed (switch only) and now also drives the start. Full native `2,448 passed / 1 skipped`; firmware SUCCESS, RAM1 byte-identical (`padding 1,176`). Open: S4f.3's carrier (the switch's own gesture still does not carry the start), the newly found unconditional pending-record clear at the top of `finalizeCaptureAndSelectSlot`, check 7's cross-track and cleared/populated coverage, and repeated B→C as a finalizer-**capacity** case. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4f.6.

**2026-09-25 — S4b.1 (second pass) — fixture tightened; unadmitted-stop retention already holds.** The acceptance fixture now requires an **admitted** finalizer and drives B through the production per-slot gesture and real MIDI handler against an empty target. The admitted-and-paused case remains blocked on **S4f.3**, but a separate fixture proves the **unadmitted-stop** case already retains the switch and start and executes them once admission allows, without another gesture. Storage/display boundaries crossed by the gesture are observable for switch suites; the real edit-depart closure was rejected as too heavy. Open: S4f.3, cross-track switch, and repeated B→C (finalizer capacity, not `Omax` alone). Full native `2,448 passed / 1 skipped`. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4b.1.

**2026-09-25 — S4b.1 — switch acceptance executable; blocked on S4f.3.** The exact handoff gesture (A overdubbing → stop admitted, finalizer paused → switch to B → B records MIDI while A finalizes) is a fixture in `test_runtime_working_set_ownership`, asserting B's recorded events through the production switch and MIDI route. It fails at the switch: the path holds the active slot while the stop is unadmitted/in flight, which S4f.5 requires and which S4f.3's accepted-interaction carrier must replace. Removing the guard passes the acceptance but breaks `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`, so it was reverted; the fixture asserts the current behavior by default and the requirement (red) under `S4B_ACCEPTANCE=1`. Cross-track and repeated B→C switches remain unproven and need the bounded overlap policy. S4.4's "envelope stricter than the model" claim is retracted: the census mislabeled an outgoing finalizer as `LiveCapture`. Full native `2,447 passed / 1 skipped`. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4b.1.

**2026-09-25 — S4.4 — declared cap policy reports, and does not refuse.** `Track::reportEnvelopeBreachIfAny` reports a declared-envelope breach as `reason=cap_reached` (distinct from an allocation failure) via the new `MemoryMonitor::reportDeclaredCapDenied`, with no new storage, while letting the activation proceed. The first cut enforced the caps at the activation entry and broke the suite: the declared envelope is stricter than the resource model (per-slot capture roles are supported; 85 fixtures arm a second slot), so enforcement would reject supported flows. Enforcement reverted, observation kept; enforcing caps remain the lease's own. The refuse-vs-queue decision is deliberately left open — it needs a cap the model also enforces or an architecture decision. Full native `2,446 passed / 1 skipped`; RAM1 byte-identical. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.4.

**2026-09-25 — S4.3 — reactivation reuses released capacity.** Release retires a slot's working set but keeps its `LoopPlaybackRuntime` in `runtimeBySlot_`, so re-activation re-admits into the same object at a stable address with its generation advancing rather than accumulating. A four-cycle fixture with a baseline control asserts address identity, role-set stability and a stable chunk pool, and is red-proven with the sweep disabled. The entry has a production caller already (`startRecordingOnTrack` sweeps every other track, plus the memory-pressure path). Native has no PSRAM, so the byte-level device claim stays open with S2.2/S2.3. Full native `2,445 passed / 1 skipped`, RAM1 byte-identical. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.3.

**2026-09-25 — S4.1/S4.2 — the `(slot, generation)` release entry.** `TrackPlaybackRuntime::releaseSlot` is the single, identity-qualified release entry (`NotHeld` / `StaleGeneration` / `RefusedBusy` / `Released`), applied per slot behind `noLiveOwnerForSlot` by `Track::releaseEligiblePlaybackRuntimeSlots`; the predicate-free `resetAll` is deleted. The slot's runtime generation now advances on release, so the S3.1 lease release path is live. No new state (identity is the lease generation), `sizeof(TrackPlaybackRuntime)` stays 32, RAM1 byte-identical. Both fixtures red-proven. Full native `2,444 passed / 1 skipped`. Open: the persistence footer read and the role-transition sweep callers. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.1/S4.2.

**2026-09-25 — S4.2 — generation-qualified finalization identity.** `captureStopOwner` is now qualified by the owner's generation (new `captureStopOwnerGeneration`, with `set`/`clear` as the only writers), so a predecessor's late cleanup on a reused runtime object is a rejected no-op instead of nulling a successor's marker. Fixture covers A-releases / C-reuses / late-A with the matching-identity control and is red-proven. The field fit existing padding: `sizeof(LoopPlaybackRuntime)` stays 6,344 and RAM1 is byte-identical. Full native `2,442 passed / 1 skipped`. Open: the per-slot runtime generation is not bumped on release, so the lease's release methods remain uncalled. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.2.

**2026-09-25 — S4.1 (first slice) — per-interval ledger scoping.** A stop used to clear every allocated slot's ledger, so a sibling slot's stop erased a finalizing predecessor's open-note suppression state. `Track::clearStoppingIntervalLedger()` now scopes the clear to the stopping interval, both converted stop routes call it, and the deliberate global sweep (`sendAllNotesOff`) is unchanged. The fixture is red-proven against the pre-fix behavior; an earlier version of it was a false green and that is recorded. Full native `2,441 passed / 1 skipped`, capture-serial builds, RAM1 `variables` unchanged. Open remainder: the memory-pressure `resetAll` conversions, the `DeferredSaveJobStages` footer read, and the `(slot, generation)` release surface (S4.2). Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S4.1.

**2026-09-25 — S3 gate — acceptance suite landed (`test_runtime_working_set_ownership`).** Boot/load as an admission boundary, the record arm's role establishment, liveness-guarded release per owner kind (with retirement controls), and selection acquiring nothing — asserted over the production admission paths, linked as a firmware-boundary suite. 4 cases green; full native `2,440 passed / 1 skipped` (98 suites); `test_playback_prewarm` unchanged; firmware RAM1 byte-identical. **Gate stays open** on the S2b device gates and the S2.2/S2.3 current-root budget, which are device measurements. Record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3 gate.

**2026-09-25 — S3.3 — selection acquires nothing.** A fixture browses three committed slots through the production selection mechanism and proves each slot's lease stays unheld (`None`, generation 0) with no runtime admitted, so selection cannot depend on working-set availability; the record arm is the positive control. No production line changed and firmware RAM1 is byte-identical — the churn counters a first cut added were rejected by the `LoopPlaybackRuntime` size pin and removed rather than re-pinning. `test_continuous_capture` 101/101; full native `2,436 passed / 1 skipped` (clean build, 97 suites). Packet: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3.3.

**2026-09-25 — S3.2 — per-owner-kind liveness predicates and their discriminating fixture.** `noLiveOwnerForSlot` is now a list of named terms (`hasLiveCaptureStopJobForSlot`, `hasAcceptedInteractionForSlot`, `ownsLiveFinalization`, `ownsInFlightPreparedHandover`) with unchanged meaning and callers; the job-owned undo root (S5.2a) and deferred reconstruction (S5.2e) remain inexpressible and are recorded with their stages, and the published pair stays deliberately not a term. `test_continuous_capture` gains a fixture holding one owner kind at a time with two inverse controls; suite 101/101, full native **2,520 passed / 1 skipped**, RAM1 flat, capture-serial builds. Packet: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3.2.

**2026-09-25 — S3.1 — working-set lease vocabulary landed and routed.** `WorkingSetLeaseSlot` (per-slot, on the externally allocated `LoopPlaybackRuntime`) plus `WorkingSetLeaseTable<MaxSlots>` give the runtime a generation-qualified identity over a declared cap; operations are O(1) and allocation-free and report `CapReached` / `InvalidRole` / `NotHeld` / `StaleGeneration`. The record arm (`prewarmPlaybackForSlot`) and the boot-cold overdub start (`admitOverdubStartWorkingSet`) both record `PlaybackWindow | LiveCapture`; the boot pass moved into `Track::prewarmBootPlaybackRuntimeSlots()` so the admission predicate and the lease share one owner. Behavior unchanged: `test_playback_prewarm` and full native **2,434 passed / 1 skipped**; capture-serial builds with `variables` flat (`code 391,912 / padding 1,304`) and both struct pins intact. Release is S4.1's; S4.2 reads the generation for `captureStopOwner`. Packet: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S3.1.

**2026-09-25 — S1.6c.2c — loop-edit tempo fader (DEC-086), device-verified.** Fader 4 on the loop-edit control channel now sets the global tempo: **ch16 CC102, one BPM per step over 60–187 BPM**, applied immediately through `ClockManager::setBpmFloat`; entering loop edit sends the current tempo back so the motor parks at the whole-BPM value. The law is the dependency-free `TempoMapping` header, so firmware and host tests cannot drift (`test/test_tempo_mapping`, 10/10). Device-verified via `scripts/hitl_tempo_fader_probe.py`: CC60 → **120.0 BPM exactly** (`captures/tempo-pin-120b/`); under the first 72–200 BPM law, CC32/100/64 → 104.3/172.8/136.5 (`captures/tempo-fader-01/`). RAM1 unchanged. Added at the owner's request as fixture support for the 24-bar capacity revalidation, which previously could not pin the device tempo.

**2026-09-25 — S1.6c.2b — no-clear selection fixture repaired (host-only).** The no-clear record path skipped the clear *and* the slot selection, so the recorded `snapshot-reuse-24bar` run armed track8/slot1 against a configured slot 4. Selection is now a pre-arm step that stops the transport, taps the Loops row and retires the ghost capture, with slot identity proven by a bounded wait on the target's DISP row (`_await_observed_record_slot`) before the arm press; `select_loop_slot=False` keeps the edit path from a second tap. 17 focused host tests green; full host script suite unchanged at its 8 pre-existing failures. **The device capacity gate remains open:** the corrected 24-bar run reached RECORDING but the fixture note stream did not start (1 note-on, 0.6 s take, `record stop did not reach PLAYING`), and the failed runs left the authorized witness slot 4 populated, so the guard now refuses. Resume after the owner re-verifies an empty target, with the tempo pinned via DEC-086.

**2026-09-25 — display transfer is DMA-backed and recoverable (`recoverable-async-display-dma`).** The SSD1322 framebuffer now leaves the CPU over SPI DMA, with blocking SPI retained as both the post-failure fallback and the rollback. Activation is the default for every build (`MIDILOOPER_DISPLAY_DMA`; `=0` compiles the CPU-driven baseline). Device: `init=1` with `dma_frames` climbing and `blocking_frames=0`, and per-frame `DFRAME` transfer time **44–53 µs against the 4,495–4,670 µs baseline** (~100x); the `=0` build records `init=0, dma_frames=0, blocking_frames=900` and 4,496–4,802 µs, so the census is the control that makes the comparison trustworthy. The timeout recovery re-arms the GRAM window before its resynchronisation frame — the pre-fix write did not start at row 0 after a partially shifted aborted transfer. Recovery is device-verified with injected faults (`MIDILOOPER_DISPLAY_DMA_FAULT=1` reject, `=2` timeout, with a fired-event count): both transient variants reach `DMA recovered` after the cooldown with one blocking frame in the window, and the permanent variants degrade at the third consecutive failure and then serve every later frame over blocking SPI. A dependency-free policy object (`SSD1322_DMA_HEALTH`) carries the bounds, cooldown and census so the host test drives the production transitions (9/9); `DisplayManager` publishes a bounded on-change `#DMA_XFER` census under `SESSION_CAPTURE`. RAM1 is byte-identical to the pre-change build (`96,160 / 391,448 / 1,768 / 34,912`). **Complete and archived 2026-09-25 (14/14):** `recoverable-display-dma-transport` is now the normative spec. Task 4.1 added deferred-transfer admission so a frame that the busy transport refuses stays pending instead of being acknowledged untransferred (`admitDisplayTransfer`, `advanceDisplayFramePhase(ran, transferDeferred)`, device-smoked at `dma_frames` 1 → 136 with `blocking_frames=0`); 2.1/2.2 verified `abortAndReset()` on hardware — two injected timeouts then `DMA recovered` with `dma_frames` 13 → 119 on the same channels, i.e. the wrapper is left idle and reusable with no channel re-initialisation. The managed baseline-vs-DMA pair returned `INCONCLUSIVE_CAPTURE` on the base preset's setup-race class, present with DMA off too and owned by the still-open `runtime-working-set-ownership` selection fixture — not by this change. Not yet on `dev`: it rides `feature/runtime-working-set-ownership`, so `DELIVERABLE_TRACKING` updates at the PR.

**2026-09-25 — S1.6c.2 — snapshot construction reuse implemented, device gate open.** Candidate-private pitch/page/membership edits now persist across one construction (DEC-085), with cached tail/count updates and cached-event sort-state reset. Native **2,402 passed / 1 skipped**, focused snapshot **42/42**, target build green; RAM1 unchanged (code391,368 + padding1,848; variables96,160; locals34,912). Image `94b09b51…`. Fresh N=56 census: external payload **7,006,576 → 3,238,256 bytes**, snapshot slabs **151 → 36**; all 64 observed slot records match the earlier census (both lack consistent-view attestation). This saves **3,768,320 bytes (~3.59 MiB)** without deleting content or shrinking the capture ring.
Stopped-only follow-up (`captures/snapshot-reuse-24bar/stopped-heap.log`): internal arena356,800B, allocated chunk bytes199,392B, free chunks157,408B, largest free chunk122,768B, unused tail12,288B; external payload5,246,288B, free3,100,380B, largest request3,046,620B. Both scans complete. This disproves interpreting the *stopped* 12KiB tail as total available heap. It does not retroactively measure free lists at the earlier live refusals; tail-based snapshot admission remains the next hypothesis to verify without live scans. No leak/fragmentation verdict from this one stopped reading.


The stress rerun is **INVALID_TARGET_INTERRUPTED**, not acceptance: configured track8/slot4 but the no-clear harness armed selected **track8/slot1**, recorded/overdubbed and persisted `slot:7:0`. Interrupted and explicitly stopped; `STOP_STATUS` confirms STOPPED/pending=0. No clear/delete/rename or cleanup. User reports device85.4BPM, explaining host24-bar timing yielding17devicebars (13,056ticks). No fatal/external refusal, but seven late `snapshot_reserve/heap_low` rows and minimum heap tail12KiB mean overall heap correctness remains open. Host precondition now fails before gestures unless the latest device-time DISP since track selection verifies the configured slot; host tests56/56. No further content runs until the selection fixture is corrected. Role-specific reservations remain unchanged pending their transition/admission contract. Evidence: `captures/snapshot-reuse-preflight/`, `captures/snapshot-reuse-24bar/`, `captures/snapshot-reuse-image/`. Recorded in local runtime checkpoints; capacity acceptance remains open.


**2026-09-25 — S1.6c.1 — known owner lifetime fixes delivered in the working tree:** whole-take release now frees origin/rank side arrays (24,576 bytes of previously retained payload); draw-source compose reuses pending-paint backing, filters slide inclusion in place, and retires obsolete paint before allocating a larger copy. Selected-bridge authority and compose/paint separation preserved. Red→green regressions; focused 301/301, full native **2,399 passed / 1 skipped**; capture-serial SUCCESS, RAM budgets unchanged. Final image `10695e07…`. **Capacity is not fixed:** the same N=56 24-bar HITL (`captures/heap-ownerfix-24bar-02`) still fatals ~47.62 s into RECORD on a 6,096-byte DisplayNote vector reserve, before overdub. S1.6c — owner correction/capacity revalidation remains open. No live heap scan or content-clearing workaround. Board recovered: N=56, chunks403, target empty, zero differences in compared content fields across 64 slots. Edits uncommitted.


**2026-09-25 — S1.6b — original workload rerun:** same effective N=56 24-bar record/24-bar overdub configuration, user track 8 slot 4, no CLEAR, image `a71b50eb…`. `captures/heap-rerun-24bar-01/` reproduces fatal exhaustion ~32.08 s into RECORD: 8192-byte DisplayNote vector growth request, caller `0x6008c919`, external live 8144 KiB / 1176 blocks, reported internal tail 8 KiB. No overdub or record stop reached; harness SERIAL_INCOMPLETE. No heap scan during the workload. Stopped preflight scan complete; no same-boot after-sample because firmware aborted. Recovery returns N=56 / 403 free chunks, target empty, zero differences in 10 compared content fields across 64 slots (not a byte/write guarantee; observation consistent_view=false is not a new stability proof). Exact display container and leak/fragmentation remain unresolved. Evidence: active runtime review packet and `captures/heap-rerun-24bar-01/result-summary.json`. No firmware/script changes in this rerun; OpenSpec gates remain unchanged.


**2026-09-25 — S1.6d — stopped heap diagnostics:** added capture-only `!HEAP_STATS` under MemoryMonitor. No periodic/live allocator inspection or allocation probes. Refuses transport, active tracks/capture/stop work, deferred storage/prewarm, selected external clock, and pulses received within the last second. Raw allocator chunk/gap metrics, unused internal tail, completeness and scan duration are separate. DEC-084 records the explicit stopped/clock-quiet exception. Native **2,394 passed / 1 skipped**, focused diagnostic **5/5**, capture-serial build green (RAM1 variables 96,160; code 391,368; padding 1,848; locals 34,912). Initial stopped readings complete in ~49 ms; final stopped/clock-refusal device gate passed in `captures/stopped-heap-03/` (exact refusal, no scan). S1.6d is checked. Work remains uncommitted. This adds evidence, not a malloc leak verdict or the S1.6c — owner correction; capacity gates remain open.


**2026-09-25 — S1.6 heap correctness:** physical external accounting now excludes internal
fallback, balances tags on address reuse, and reuses saturated-table tombstones. Three native
red regressions → 5/5; full native 2,389 passed / 1 skipped; capture-serial green and flashed,
RAM1 unchanged. Capacity sweeps remain paused. The N=56 canonical 24-bar run still fails in
recording: 5,216-byte request from `DisplayManager::resolveDisplayNotesByDrawSource`, resolved
against pinned ELF, external live8,142KB with no accounting misses. This is not attribution of
the older 8KB request or proof of a leak. S1.6a is complete; S1.6b allocation/lifetime attribution
and S1.6c owner fix remain open, as do S1.5 overdub refusal and S2b.3 workload-peak budget.
See active runtime-working-set-ownership ARCHITECTURE-REVIEW.md § S1.6 and
`captures/heap-attribution-24bar-01/allocation-attribution.json`. Earlier lower-N/reclassify
recommendations are superseded by this heap-correctness priority. Work is uncommitted.
Recovery confirmed N=56 / 403 free chunks, zero observed content-field differences across
64 slots, and the failed target still empty. Board recovered; no clear/delete/rename.


**2026-09-25 — S1.5 device refusal-point attempted on the N=56 library: REFUSED IN THE RECORD PHASE, so the overdub reserve admission was never reached. S1.5 stays open.**
Pinned capture-serial image `b289af53…`, git `1ccff16a`; target **user track 8 slot 4**
(verified empty, authorized this session); fixture `STIM-BASE-2-2-01-TEMP-S15-24BAR` — the RWSO-24CAP
canonical shape with `--record-bars 24 --overdub-bars 24` and `--no-clear-before-record`. Pre-flight
(read-only): complete 64/64 observation, payload 59, initial playback 8, committed 56
(pass-backed 56 / root-backed 53), `chunks_free 403`, `live 6846 KB`, `heap_free 84 KB`, target empty.
The device entered RECORDING at 89.169 s; from 91.961 s `snapshot_reserve` denied every ~1 s
(`need=40960B`, `heap_free` 68 → 0 KB over 39 denials); at 130.538 s
`MEM_EXT_REFUSED bytes=8192 tag=0 heap_free=0KB live=8527KB chunks_free=391` plus the fatal exhaustion
line, then no response (no crash report, no reboot). `Overdubbing started` = **0** and
`op=overdub_start` denials = **0**, so the **overdub reserve admission was never reached** — this is the
S1.3 record-phase hard-failure class (internal heap, not chunks: 391 of 512 free; `snap` unchanged),
not the S1.5 overdub-refusal clause. Bracket **(2, 56]**: admitted at the 2-slot RWSO-24CAP library
(2026-09-23), refused here; S1.3's 2-bar slope recorded its first named denial at N = 55.

Recovery boot is identical: banner, `reconcile=0`, all readiness fields, complete observation,
payload 59, initial playback 8, committed 56 (56/53), **0 content-field differences over all 64 slots**,
target (8,4) still empty, `chunks_free 403`, `live 6842 KB`. No clear/delete/rename; only the authorized
target was addressed.

**S1.5 remains open.** The next bounded step is a target that lets the 24-bar loop exist at an N with
internal-heap headroom (record it, then overdub it), or an explicit decision to treat the N = 56
record-phase refusal as the S1.5 / S2b.3 capacity boundary. No firmware change and no `DEC-###` owed.
Evidence `captures/s15-24bar-t8s4-01/refusal-analysis.json`, `captures/s15-preflight-01/`,
`captures/s15-recovery-02/`; full record
[`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ S1.5 — device refusal-point result. Work remains uncommitted on `feature/runtime-working-set-ownership`
(HEAD `1ccff16a`).

**2026-09-25 — S1.3 residency slope COMPLETE: dense probe reached hard allocation failure.**
The fixed-role two-bar slope remains N=10..56 (46 verified additions). A separate dense
four-bar probe at N=56, user track 8 slot 4, supplied a one-clock note cadence across pitches
0–127. Recording started; at 41.448245 s device time, before record stop, the allocator refused
8,192 bytes with 4 KB internal heap free (`tag=0`) and emitted the fatal exhaustion line.
This is the measured failure point for this library/workload, not a universal smallest N or a
64-bar capacity claim. The untagged allocating caller is not identified. The last sampled free
chunk count was 393: do not classify this as chunk-count exhaustion.

The failed take had no verified persisted payload; the fixture withheld its survival reboot.
Separate crash recovery and a final read-only boot confirm the original 56 loaded slots,
unchanged other observed content fields, matching boot roles, and an empty failed target.
Board is stopped at track 6 slot 3. No clear/delete/rename or existing-slot overwrite.

S1.3 is checked: measured slope plus hard failure/error, alongside the previously completed
synthetic 60-descriptor characterization. S1.5 — 24-bar overdub refusal, S1.4 — undo/pin baseline,
S2b.3 — workload-peak budget and the other device gates remain open. No S3 — lease ownership start.
Host tests **42/42**, OpenSpec/diff checks pass. No firmware edits/build this slice; pinned image
`b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208` reused.
Evidence: `captures/s13-dense-evidence/pressure-result.json`, `captures/s13-dense-t8s4-01/`,
`captures/s13-dense-recovery-01/result.json`; existing slope in `captures/s13-slope-evidence/extended-slope.json`.
Work remains uncommitted on `feature/runtime-working-set-ownership` (HEAD `1ccff16a`).

<!-- S13_EXTENDED_PROGRESS_BEGIN -->
**Historical checkpoint — superseded by dense-probe completion above: S1.3 extended sweep N=10..56.**
46 two-bar additions passed exact 62-event / 1,536-tick SD/RAM survival before and after reboot.
All boot roles stayed fixed; no other observed content fields changed; 45 repeated intermediate
owner baselines matched exactly. Free chunks 449->403; snapshot telemetry 2,187->4,196 KB;
slab-used 2,491->4,774 KB; tracked live 4,221->6,846 KB. Tracked live/high includes the core
`extmem_malloc` internal fallback and is NOT a physical PSRAM-free oracle.

First named denial occurred at N=55 while recording the 56th slot (user track 8 slot 3):
`snapshot_reserve / heap_low`, 40,960-byte margin + 32,768-byte floor, heap 48 then 44 KB;
17 denial lines, zero wrapper allocation refusals. Publication deferred; the take persisted in
legacy format and survived reboot exactly. This is a temporary publication-admission boundary,
not a hard residency/canonical failure. S1.3 remained open at that earlier checkpoint;
S1.5 — 24-bar overdub refusal and S2b.3 — workload-peak reserve remain OPEN. S3 is not started.

Growth stopped at that boundary. The supervisor initially advanced because survival passed;
the next target (track 8 slot 4) was interrupted during preflight before recording. The host
runner now returns a stop status on named memory denials even if survival succeeds. Final fresh
read-only boot confirms 56 loaded, same observed content/roles, stopped focus track 6 slot 3.
No clearing/deleting/renaming or populated-slot overwrite. User authorization for empty slots
was sufficient; this stop is a measured boundary, not exhausted authorization.

Evidence: `captures/s13-slope-evidence/extended-slope.json`, `extended-episode-peaks.json`,
`captures/s13-empty-anchor-t8s3-01/`, `captures/s13-sweep-closeout-01/`. Host tests 40/40;
OpenSpec validates. Firmware unchanged this slice (SHA256 `b289af53…`, code 391,304 / padding
1,912); prior native evidence 2,384 passed / 1 skipped remains applicable, not newly rerun.
Changes remain uncommitted on `feature/runtime-working-set-ownership`.
<!-- S13_EXTENDED_PROGRESS_END -->

**2026-09-25 — S1.3 fixed-role growth MEASURED, range 10–15 loaded slots.**
Five two-bar takes into user track 6 slots 4–8 passed exact SD/RAM event-and-length parity
before and after reboot (62 rows / 1,536 ticks each). All ten boot checkpoints used the same
stopped transport and boot playback footer; focus was restored to track 6 slot 3. Each loaded-set
comparison added only its intended target and lost none; existing observed content fields stayed
unchanged. Four repeated intermediate baselines reproduced every reported owner total exactly.
Payload inventory stayed **36**; content union/pass count **10 -> 15**, root count **8 -> 13**.

Post-restore free chunks: **449, 448, 447, 446, 445, 444** at N=10..15. Cold reservation walk
remained **512 -> 480**, all walked credits zero (not workload peak). Snapshot telemetry grew
**2,187 -> 2,410 KB**, slab-used **2,491 -> 2,745 KB**, live **4,221 -> 4,799 KB**; heap_free
stayed 84 KB. Live increments are **65, 352, 64, 33, 64 KB**: retain the N=11->12 jump,
not a single linear per-slot attribution. Subsequent code/capture attribution identifies the 64->65 canonical-chunk backing-block boundary
(328,448 bytes); see the extended review entry.
No observed MEM_EXT_REFUSED, MEM_DENIED, FATAL or RING overflow in the ten measurement logs.

**Stopped because all approved empty scratch slots are now populated, not because of capacity.**
S1.3 and S1.5 remain unchecked: failure point not reached, two-bar workload only. S2b.3 remains
open because workload peak reservation is unmeasured; S3 does not start. Next device growth needs
explicit authorization for additional verified-empty slots outside the prior scratch range (the final
manifest lists candidates), or permission to recycle a named scratch subset. No clear/delete/rename
or recording outside track 6 slots 4–8 occurred this slice. Board left stopped, focus track 6 slot 3.

Evidence: `captures/s13-slope-evidence/slope.json`; detailed runs `s13-slope-slot4-04` and
`s13-slope-slot5-01` through `s13-slope-slot8-01`. Full native **2,384 passed / 1 skipped / 0 failures**;
decoder 51/51; host 35/35; OpenSpec/diff checks pass. Firmware unchanged from the prior green
capture-serial image (SHA256 `b289af53…`, code 391,304 / padding 1,912); only host tools/tests/docs
changed this slice. Work remains uncommitted on `feature/runtime-working-set-ownership`.


**2026-09-25 — S1.3 controlled one-slot survival PASS (uncommitted branch work).**
Track 6 slot 3 / `(5,2)` was verified empty, then received one two-bar take. Take identity
matched start/stop/commit/admitted target. Two full SD reads were identical; the production
legacy parser with an explicit Teensy event-layout adapter decoded **62 events / 1,536 ticks**,
exactly equal to stopped RAM. Only then was the unchanged firmware re-uploaded for the survival
boot. Restored RAM and two new SD reads match all events and the original file hash. Loaded
union **9 -> 10** (passes 9 -> 10, roots 8 -> 8), payload inventory stays 36 because an existing
empty payload gained content. Settled free chunks **450 -> 449**; after-boot attested checkpoint
449. No clear/delete/rename, no non-scratch recording. Evidence:
`captures/s13-controlled-01/` and `captures/s13-controlled-survival-01/`.
Verification: full native **2,383 passed / 1 skipped / 0 failures**; host **29/29**;
firmware build and OpenSpec validation pass.
This closes the one-slot fixture blocker, not S1.3's multi-point slope or S1.5's refusal point.
Next: define fixed-focus/active-role cohorts within the remaining approved scratch slots, compare
complete content sets (detect eviction), and measure through the first classified failure. The
selected/initial-playback slot changed in this fixture, so its one-chunk difference is descriptive,
not an isolated per-slot slope. S2b.3 still needs workload peak reservation; S3 remains gated.


Native evidence recorded: each denied/cancelled slot retains 16,384 fixed event-capacity units
and 8 canonical credits; same-slot retry reuses measured buffers, three slots total 49,152 units.
Full native: **2,379 passed / 1 skipped / 0 failures**; focused 99/99; host 23/23.
See `captures/s13-native-cancel-retention/`. No production cleanup fix or device claim.


**2026-09-25 — S1.3 native retention characterization:** failed record admission followed by
real manager cancellation retains the slot's prewarmed fixed event buffers and canonical credits.
Repeating the same slot reuses the measured buffers; distinct slots retain distinct sets.
This supports the existing per-slot ownership diagnosis, not an exact device-byte attribution
or a new capacity/slope checkoff. Firmware and board untouched in this slice.


**2026-09-25 — S1.3 continuation: stopped readback recovered without reset; arm capacity refusal.**
Explicit MIDI Stop made identity stats and library observation available on the same boot. The
settled observation reports 36 payloads / 9 loaded slots; track 6 slots 3-8 have no committed events.
The repaired fixture handles selection-created ARMED, explicit MIDI Start/Stop, and delayed RID.
Final attempt on track 6 slot 4 refused record_start/prewarm_failed (4096-byte external refusal;
heap_free=4KB). No stimulus notes; failure Stop confirmed by stats. Earlier slot-3 attempt started
an empty recording and was stopped in the next run, length=0. No clearing, reboot, or firmware edit.
23 host tests pass. Added-population range remains 0: this warmed-state refusal is not a slope
or controlled capacity checkoff. Next is native prewarm/arm-cancel attribution; SD-event parity
is still unavailable through the current diagnostic commands. Details in the active review packet.


**2026-09-25 — S1.3 residency-slope fixture takeover.** Host regression repaired: note streaming,
record stop, fresh identity checks and nonzero failure status. Seven new host tests; no device run.
The unfinished legacy-only synchronous payload probe was withdrawn. Stored-event parity remains
unverified, reboot withheld, and the earlier “payload did not survive” attribution below is historical
and superseded by the take-correlation investigation. An enqueued row not yet transmitted at readback
is outstanding evidence, not proof of permanent ring loss. S3 — lease ownership remains gated.

**2026-09-24 (seventeenth session) — `runtime-working-set-ownership` S1.3 — the content-representation
correction, and the corrected telemetry CONTRADICTS the "28 root-backed" premise: only 9 of the 36 verified
payload slots hold content. The slope fixture is blocked at its first step by a saved payload that did not
survive reboot; no refusal occurred, so S1.5 — refusal point stays open and the measured range is 0 added
slots.** The trace had removed the presumed restore-instability blocker, so no restore investigation was
repeated. **S2b.7 — drain hardware confirmation stays separate.**

Owner direction: correct the representation gap first, then run the residency measurements. **One thing at a
time**, and the scratch-range limit is never called a capacity failure.

- **The gap, fixed.** `has_committed_content` / `committed_at_checkpoint` reported only `Loop::passes`, so a
  slot whose restore installs a published root read as `0`. Corrected: `hasPassContent` + `hasRootContent`
  reported **separately**, `hasCommittedContent` their **union** (a slot with both is one loaded slot);
  `hasRootContent` means a root **is installed** (`Loop::publishedSnapshot()`), with `rootIsCurrent`
  reported beside it (an installed-but-stale root is still content — playback reads it through the
  stale-root overlay); invalidation now includes the **root publication identity**. Slot wire `k`/`kp`/`kr`/
  `kc`; end wire `m` + `P`/`R`, inside the 192 B capture cap. Native **2,378 / 1 skipped / 0 failures**,
  firmware **SUCCESS** and **flashed**.
- **Revalidation (two independent read-only boots, identical):** payload **36**, initial-playback 8,
  `committed_at_checkpoint` **9** (`pass_backed` 9, `root_backed` 7). The content set is `(0,0) (1,0) (1,1)
  (1,2) (2,0) (3,0) (4,0) (5,0) (5,1)`; **every other payload slot has 0 passes, 0 length, 0 events and no
  published root at all**. So the corrected loaded count is **9, not 36**, and the sixteenth-session
  "8 pass-backed / 28 published-root-backed" attribution is **contradicted** — the publish path installs a
  root whenever it adopts, and these 27 slots never did. The observation correction is what falsified the
  inference; the remaining question is why 27 offered, token-verified, `terminal/committed` payloads adopt
  nothing.
- **Owner census (S1.1) and S2b.3 on the same boot:** settled `chunks_free 450`, `slab_live 78`, `slab_used
  2,455 KB`, `snap 2,155 KB` (flat 173→209 s) — the same owners the trace recorded, so the **owner total**
  holds while its **per-slot** reading does not; `post_restore_census free_chunks=450` vs required 16 →
  **met**, reservation walk 512→480 then 480→480, `credits=0` on all 16 rows.
- **Slope (S1.3) — BLOCKED, 0 added slots, no refusal.** Recording (6,1) saved (×4) and rebooted; the boot
  reports `Track 6 loaded state: EMPTY`, N=36 (no new payload) and an **unchanged** owner census. The next
  reset did not take. **No `#MEM_EXT_REFUSED`, no `refused` terminal** — this is a fixture-path blocker, not
  a capacity failure, so **S1.5 — refusal point stays open**.
- **Separate:** **S2b.7 — drain hardware confirmation** (the three transferred unchecked tasks) is untouched
  and does not gate this investigation. Full evidence:
  [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
  § *S1.3 — observability correction* → *Task 3* / *Task 4*.

**2026-09-24 (sixteenth session) — `runtime-working-set-ownership` **S1.3 — slope-fixture definition**: the read-only trace **FALSIFIES the "28 unloaded" premise** and defines the controlled increasing-population fixture. No device run, no firmware edit, no sweep.**

Owner direction: continue directly in `runtime-working-set-ownership` (no new sub-change); keep **S2b.7 —
drain hardware confirmation** separate. Start read-only: trace representative slots, explain the four
initial-playback slots, test the observation's content accuracy, and define the fixture **before** any sweep.

- **All 36 payload slots are loaded, not 8.** Boot offers every payload slot and all 36 reach
  `terminal/committed` (the RRQ log matches the observation's request/terminal columns exactly). The 8/28
  split is a **representation** split, not loaded vs unloaded: `commitLoadLoopJobPublish` →
  `applyLoadedLoopSlotBodyCommit` branches on `usesLoopSnapshotBodyFormatV1`. **Legacy** payloads go through
  `Loop::adoptPersistedSnapshot`, which moves `snapshot.passes` into `Loop::passes` → pass-backed (8 slots).
  **LSV1** payloads install a **published snapshot root** (`loop.storePublishedSnapshot(...)`) and leave
  `passes` empty → **root-backed (28 slots)**, even though playback reads that root
  (`Loop::tryGatherPublishedSnapshotEventsInWindow`).
- **The root-backed 28 consume residency.** `#MEM_OWNERS` over the boot: `slab_live` 0 → 78, `slab_used`
  0 → 2,455 KB, `snap` 0 → 2,155 KB, `chunks_free` 480 → 453, then **flat** after ~12.8 s; the
  `capture_prewarm` walk reports `credits=0` on all 16 rows (canonical reservation flat, S2b.1), so what grows
  is payload/root residency.
- **The four required slots absent from the pass-backed set:** `(2,1)` and `(4,0)` are **root-backed**
  (loaded; a representation difference only); `(6,0)` and `(7,0)` are `enabledMask=0x1` but **payload-absent**
  and never offered (`Track 6/7 loaded state: EMPTY`) — a payload-less enabled slot, not a restore failure.
- **Observation accuracy:** payload presence/token, enumeration, request state/terminal, the initial-playback
  flag and window framing are **accurate**; `has_committed_content` / `committed_at_checkpoint` / the `a,h,l,v`
  content identity were **pass-backed only** and **under-reported** the 28 root-backed slots — **not
  spec-conformant** with "whether the slot holds committed content". **Corrected the following session**
  (the representation correction at the top of this file); the finding was owned by
  `runtime-working-set-ownership` and the fixture did not depend on it.
- **Fixture defined (not run):** fix one active role (transport stopped, no record/overdub), vary `N_payload`
  one slot per boot inside the approved scratch range (track 5 slot 1, track 6 slots 1–8), and report
  `N_payload` / `N_pass` / `N_root` separately with reservation, payload, root and directory residency; the
  failure point is the first refusal (`#MEM_EXT_REFUSED` / a `refused` terminal / `chunks_free` at reserve).
  The pinned parameters are in
  [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
  § *Pinned fixture parameters*. **The slope run and the failure point remain unmeasured.** Full trace:
  [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
  § *S1.3 — slope-fixture definition*.
- **Separate:** **S2b.7 — drain hardware confirmation** (the three transferred unchecked tasks) is untouched
  and does not gate this investigation.

**2026-09-24 (fifteenth session) — `restored-library-stability` Packet B **2.2 — complete-manifest stability** is DEVICE VERIFIED: PASS across three consecutive boots; **2.1 — TBD-1 scratch authorization** is not required for the reused fixture; **2.3 and Packet C are transferred to `runtime-working-set-ownership` S2b.7** (unchecked there) and the change is closed and archived.**

Owner direction: take **2.2 — complete-manifest stability** (compare the *entire* expected manifest across
three consecutive boots), **reuse the existing device captures first**, and clear nothing merely to repeat
evidence already available. **2.1 — scratch authorization** is needed only if a fixture requires emptying a
slot, so it is not required for the reused fixture; **2.3 — drain fixture** and **Packet C — S2b.7 drain
hardware confirmation** were transferred to `runtime-working-set-ownership` **S2b.7 — drain-driven restore
advancement** (recorded there unchecked, with their independence clause) and did not delay this closeout.

- **Reuse first — the existing captures settle two eligible boots, not three.** New read-only tooling
  (`scripts/hitl/library_manifest.py` + `scripts/hitl_library_manifest_stability.py`, tests
  `scripts/test_library_manifest_stability.py` **14/14**) compared all five existing `LIBOBS` captures
  (`captures/s13-libobs-stability-existing.json`). `full-02` and `incomplete-03` are eligible fresh boots;
  `full-01` is the pre-fix truncated long-form wire (`complete=0`); `incomplete-02` is a deliberately
  incomplete window (0 slots, counts `None`); and `incomplete-01` is **not a boot** — no `boot_reset`, no
  banner, `checkpoint_reached=False`: it is a mid-session attach at ~72 s uptime on the **same boot** as
  `full-02`, and its manifest sha equals `full-02`'s. "Three consecutive boots" therefore cannot be
  satisfied by relabelling; only the missing boots were captured.
- **The gate.** The expected manifest was recorded from the existing Packet A boot `full-02` (reuse), and
  three consecutive fresh read-only boots were captured back-to-back
  (`captures/s13-libobs-stab-01/02/03`, each `boot_reset` + banner, `complete=1`, upload reset before each;
  only `!LIB_OBSERVE` sent; 0 `FATAL`, 0 `#MEM_EXT_REFUSED`, 0 destructive-command lines). **All three match
  the expected manifest in full — 0 manifest / lifecycle / settled-count differences over 64/64 slots — and
  all four complete-manifest boots share manifest sha256 `4e451a07d0a94c18…`**: payload inventory 36
  (tracks 0–5), initial playback-required 8 (`(0,0) (1,2) (2,1) (3,0) (4,0) (5,0) (6,0) (7,0)`), committed
  at checkpoint 8, observed completions in the window 0; `turns=64`, `restarts=0`, `admission_complete=1`,
  `every_initial_observed=1`. **Recorded precisely: no differences in the observed fields; no destructive
  commands issued.** Identical observed records — or even identical file hashes — do not prove unchanged
  payload bytes or the absence of a write, so the write statement rests on the command log (only
  `!LIB_OBSERVE` sent) and the manifest statement on the observed fields. **The 36-vs-8 gap was read as a
  disposition:** 36 payload-present slots, 8 in the **pass** representation, 8 initial-playback-required
  (the two 8-sets differ; overlap 4). **Corrected 2026-09-24 (seventeenth session, on device):** the
  sixteenth-session reading that the remaining 28 are "published-root-backed and resident" is
  **CONTRADICTED** — the corrected per-slot telemetry reports **9** content-bearing slots of 36 verified
  payloads, with every other payload slot adopting zero passes and installing no root (see the
  seventeenth-session entry at the top; two independent boots agree). The three-boot result stands as
  repeatability of the fields it measured **only**, and it never covered a *new* payload surviving a
  reboot. The runtime slope fixture (`S1.3`) varies the **loaded** population and reports the two
  representations separately. Report
  `captures/s13-libobs-stability-3boot.json`, identity
  `captures/s13-libobs-stability-3boot.identity.json` (`git_commit 74cadc7b`,
  `firmware_hex_sha256 ede704b614b7e9cd…`). No firmware edit, so no G2 packet was required; the read-only
  recipe follows `hitl-content-verification` § 9.
- **Reading.** The restored library's *observed* manifest reproduces exactly across three consecutive boots:
  the Packet A "instability" was an observation artifact, with **no loss to attribute** and **no
  boot-to-boot manifest drift**. **Task 2.1 is closed as not required for the reused fixture** (nothing was
  cleared). **Task 2.3 — drain fixture and Packet C — drain hardware confirmation are TRANSFERRED** to
  `runtime-working-set-ownership` **S2b.7 — drain-driven restore advancement** (recorded there unchecked), as
  is **4.2 — drain-spec correction**; the closeout checklist holds **no remaining mandatory library task**.
  **Next runtime work (recorded, not started): S1.3 — slope-fixture definition**, which must explain why 28
  verified payloads remain unloaded and why only four initial-playback slots overlap the committed set;
  repeatability does **not** establish correct hydration or a usable increasing-population fixture.
  **[Answered 2026-09-24 (sixteenth session, inferred): the "28 unloaded" premise was said to be falsified
  — they were read as published-root-backed and resident. Seventeenth session on device: that reading is
  CONTRADICTED — the corrected per-slot telemetry reports 9 content-bearing slots of 36 verified payloads,
  and this gate's `committed_at_checkpoint` was a subset only of those. Its repeatability is repeatability
  of the fields measured, never of a new payload surviving a reboot.]**
  The gate
  is over the *current* library, not a constructed cleared fixture, and is not a capacity finding. Full
  record:
  [`restored-library-stability` (archived)](../../openspec/changes/archive/2026-09-24-restored-library-stability/ARCHITECTURE-REVIEW.md)
  § Packet B; `tasks.md` 2.2. **Archived** through the normal workflow (2026-09-24).

**2026-09-24 (fourteenth session) — `restored-library-stability` Packet A 1.0–1.4 landed; 1.5 is open on upload authorization. The read-only re-analysis finds NO loss cause in either sweep.**

Owner direction: proceed with Packet A. **Landed, in order:**

- **1.0 — execution-packet gate.** `openspec/changes/restored-library-stability/ARCHITECTURE-REVIEW.md`
  created with the G2 packets for the two firmware-editing slices (**1.2** classifier, **1.3** on-demand
  observation; the packet for 1.3 states the SD-read-admission contract, the invalidation/restart rule and
  the bounded-traversal oracle). No firmware edit preceded its packet.
- **1.1 — measurement corrections recorded with citations** in the same artifact (a–g). The live claims
  were corrected in `runtime-working-set-ownership` S1.3 / S2b.3 / the S2b gate, and the two historical
  paragraphs in `PROJECT_STATE.md` / `CURRENT_WORK.md` now carry an explicit
  `[Attribution CORRECTED 2026-09-24 …]` marker. No durable doc still asserts the catalog reconcile, or
  `n_committed_slots`, as a demonstrated cause of loss.
- **1.2 — the pure classifier.** New `include/StorageManager/LibraryObservation.h` +
  `src/StorageManager/LibraryObservation.cpp`: observed fields, a **current state set** over
  `empty`/`populated`/`pending`, and a **nullable historical cause**, with `empty` never a cause.
  `test/test_library_observation/` — **13 cases green**; a mutation of the `unknown` fallback to
  `content loss` fails exactly L01/L02/the unobserved case, then reverts byte-identical. The capture-side
  mirror `scripts/hitl/library_observation.py` carries the same contract
  (`scripts/test_library_observation_classifier.py`, 13 tests OK).
- **1.3 — the bounded on-demand observation.** `src/StorageManager/LibraryObservationSession.cpp`, owned
  by `StorageManager`, serviced at the **unconditional observation tail in `main`** (after
  `emitPostRestoreCensusIfReady`) and never from the admission-gated load/restore frame; requested by
  `!LIB_OBSERVE`; emitted as the distinct `LIBOBS` record family. Declared bound: **2 slots and 2 SD token
  probes per invocation**, 2 emitted records per invocation, 64 slots total, bounded epoch restarts.
  Invalidation covers `(requestGeneration, setEpoch)` **and a content revision identity** (committed pass
  count, highest committed pass id, loop length, committed event count re-read at each slice), re-checked
  immediately before each record is emitted. `test/test_library_observation_session/` — **8 cases green**
  through the real service hook, including a commit-pending blocked load (O02), a permanently waiting
  request (O01), a generation change (O04), a set-epoch restart (O05), a **publication under an unchanged
  request identity** (O06) and denied SD admission (O07). Two mutations (drop the content-revision check;
  gate the service on the commit gate) fail exactly the intended cases and revert byte-identical.
  **Full native 2,373 / 1 skipped / 0 failures**; `pio run -e teensy41-capture-serial` SUCCESS with the
  approved **12-bank** ITCM line intact (`variables 96,192 / ITCM 393,216 = code 391,160 + padding 2,056`,
  `locals 34,880`; floor 840 met with 1,216 B headroom). The first build *without* flash placement crossed
  into a **13th bank** (`ITCM 425,984`, locals 2,112 — the documented silent boot hang); the fix was to
  place every new function in flash, **not** to re-base the budget. The whole observation costs **+80 B of
  ITCM**.
- **1.4 — read-only re-analysis of the recorded sweeps.** No device action.
  `scripts/hitl_library_observation_report.py` runs the shared classifier over
  `captures/s13-slope-02/` and `captures/s13-slope-final/` (→ `captures/s13-library-observation.json`), and
  `scripts/hitl_library_growth_sweep.py`'s `boot_census()` now records `library_observation` and
  `n_committed_slots_window_scoped` per step. **Finding: no slot in either sweep records a loss cause** —
  every request-bearing slot is `not_applicable`, every other slot is `unknown` **with its missing fields
  named**, and `content loss` / `restore refusal` / `catalog omission` / `intentional eviction` are not
  evidenced anywhere. **`N` is window-scoped and the two sweeps' windows differ**: `s13-slope-02` closes at
  ~12.85–12.93 s (420–423 lines) with N = 27/27/26, `s13-slope-final` runs to ~14.6–15.4 s (442–537 lines)
  with N = 29/29/29 — the "shrinking" run is the one whose window closed **~1.8–2.5 s earlier**.
  `payload_inventory`, `initial_playback_required` and `committed_at_checkpoint` are **not recoverable**
  from those captures and are reported `None` with `payload_token`, `initial_playback_selection` and
  `committed_content` named as the missing fields — the gap the device observation closes.

**1.5 — DEVICE VERIFIED, read-only. The per-slot inventory that had never been captured, and no loss to attribute.**
`pio run -e teensy41-capture-serial -t upload` (owner-authorized; the only reliable reset), then
`scripts/hitl_boot_census.py --library-observe` on `/dev/cu.usbmodem154944801`. The only thing sent to the
device is `!LIB_OBSERVE`; no CLEAR, rename, quarantine, delete or transport start, and **0 `FATAL`**.
**Result** (`captures/s13-libobs-full-02/`): **payload inventory 36, initial playback-required set 8,
observed restore completions in the window 0, committed at the checkpoint 8**; window
`[13,017,777, 13,312,720]` µs, `turns=64`, `admission_complete=1`, `every_initial_observed=1`,
`consistent_view=0` (cross-turn, marked), `complete=1`. Per slot: **36 `populated` / 28 `empty`**, and
**no loss cause anywhere** (`content loss` / `restore refusal` / `catalog omission` / `intentional
eviction` all zero); the 28 `empty` slots are payload-absent, request-none, content-none with cause
`unknown` (no history — correctly not promoted to a loss). The initial-playback set is one slot per track —
`(0,0) (1,2) (2,1) (3,0) (4,0) (5,0) (6,0) (7,0)` — and **all eight are committed**; payloads are present
on tracks 0–5 (36 slots) and **absent on tracks 6 and 7**, which is the recorded
`Track 6/7 loaded state: EMPTY` line, now measured per slot instead of read off a track-level line.
`restore_completions = 0` because every required slot was already committed when the window opened.
**[Corrected 2026-09-24, task 2.2 run: "all eight are committed" is not supported — 4 of the 8 required
slots hold in-RAM committed content, and `committed_at_checkpoint = 8` is a partly different set; see the
fifteenth-session entry and Packet B § *Disposition*.]**
**Incomplete-window case** (`captures/s13-libobs-incomplete-02/`, `--observe-early-close-s 0.15`): reported
**incomplete**, with **zero** slot records and **all four counts `None`** — never zeros, never presented as
the settled library state. **The first device run found a real defect:** the `LIBOBS` slot record used full
enum names, overran the capture text cap (`kMaxCaptureTextBytes = 192`) and was **silently truncated**, so
a completed observation reported `complete=False`; the wire form is now compact enum **codes** with
one/two-character keys, the host parser and a locking test were updated, and the run was repeated.
**Reading:** the instability was an *observation*; this read-only attribution finds **no loss to attribute**
and identifies the "shrinking" count as **window-scoped**. Whether a *repeated* boot reproduces the
manifest is Packet B's complete-manifest comparison, not this packet's claim. Packet A (1.0–1.5) is
**complete**; Packets B and C and the 4.x handoff/closeout remain open.
**[Superseded 2026-09-24 (fifteenth session): Packet B 2.2 PASS and the change is archived; 2.3/Packet
C/4.2 transferred to `runtime-working-set-ownership` S2b.7.]**

**Flagged, not absorbed — stale `teensy41` RAM1 baseline (+64 B, pre-existing).** `pio run -e teensy41` fails
the post-link budget check with `variables 79,232` vs the approved 79,168. **Verified not to be this
change:** with the whole change stashed, the baseline tree builds to the identical line
(`variables 79,232 / code 382,216 + padding 11,000 / locals 51,840`), and Packet A adds 0 B of DTCM/RAM1
variables and 0 B of ITCM to that env. The `teensy41` entry was last re-based 2026-09-23 and is stale
relative to S2b.7/S2b.8, which only re-recorded `teensy41-capture-serial`. **Deliberately not re-based** —
that baseline is edited by a human with the reason recorded in the owning change's artifact. The required
env (`teensy41-capture-serial`) is green and within budget.

**Follow-up on the same session — DEC-083 and the capture-tier fix.** The observation's contract is
recorded as **DEC-083 — the restored-library inventory is a bounded on-demand observation, separate from
the settled census** (owner `StorageManager`, service point independent of load admission, 2/2/2 bounds,
identity + content-revision invalidation, window-scoped four quantities, and the rejected alternatives).
The device run's truncation is locked by test: `LIBOBS` is now **Tier-A** in
`CaptureLineTier::isTierALine` (it is an explicitly requested one-shot inventory and must survive the
timing-critical flush budget), with `test_library_observation_lines_fit_capture_text_budget` asserting
worst-case records ≤ 191 bytes — `test_capture_line_tier` **15/15**, full native re-run **2,374 / 1 skipped / 0 failures**, and the capture-serial RAM1 line is unchanged (`code 391,160 + padding 2,056`). The durable guide now owns the
behavior: `docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md` § *The on-demand library observation* (with an
ownership-table row and a `Read this when` entry), a `BOOT_LOAD.md` key-files row, and
`.agents/skills/hitl-content-verification` § 9 for the read-only attribution path.

**2026-09-24 (thirteenth session) — the restored-library instability gets an owner: `restored-library-stability` PROPOSED, planning complete, Packet A (observation-only firmware + read-only analysis) starts immediately.**

Owner direction: own the instability instead of running another drain test, because it blocks the
measurements and the later runtime stages. **Opened** `openspec/changes/restored-library-stability/` — proposal,
design, a `lazy-slot-hydration` spec delta and tasks; `openspec validate` clean; **no implementation started**.
**Scope:** pre-register the four-class historical cause (**catalog omission / restore refusal / intentional
eviction / content loss**) as a **nullable** attribution kept separate from the observed **current state**
(`empty` / `populated` / `pending`) — `empty` and `pending` belong to the state; `ambiguous` and `unknown` are
valid causes — and require evidence with stated precedence; add a
per-slot **observed-state** inventory (SD payload + token, boot enumeration, restore request state/wait/terminal
with full identity, committed content, selection) reporting **four distinct quantities** with an explicit
observation **window** (payload inventory; initial playback-required set; observed restore completions;
currently committed set); collect it through a **bounded on-demand snapshot** that does not depend on the
settled census gates; define **one**
repeatable fixture inside the already-approved scratch range (track 5 slot 1, track 6 slots 1–8), read-only
first, with any CLEAR requiring **per-slot owner authorization**, whose stability is proven by a **complete
per-slot manifest comparison**; and carry the **S2b.7 device confirmation** as an independently reportable
packet. **Handoff, not absorption:** when the library is stable,
`runtime-working-set-ownership` finishes S1 — residency measurements and the S2b — boot/load admission device
gates, then starts S3 — lease ownership; this change does not start them and does not bypass the stage order.

**First findings, recorded read-only, and they correct the previous attribution.** (a) `MidiLooper/sets/index.bin`
is `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }` — a **saved-set folder counter**, not a slot catalog,
so a reconcile failure cannot by itself explain a missing slot; the live library is
`/MidiLooper/current/loops/loop_TT_SS.bin` plus the `loopSlotPayloadOnSdInRam_` mirror. (b) The boots that
**shrank** the committed set (`captures/s13-slope-02/`, N 27 → 27 → 26) report **`reconcile_warnings=0`**.
(c) `DIAG,lcr,skip,restore` is a **content-resolution** device-gate line
(`TrackDeferredMaintenance` / `LoopContentResolution`, `kind=skip`, `reason=restore`), not a library marker.
(d) `Track 6/7 loaded state: EMPTY` appears on the **stable** `N=29` boots in `captures/s13-slope-final/` too,
so it does not track the shrink. (e) Both sweeps' `library_rows` are **all-zero** (16 rows, `phases: 0`), so
**no per-slot inventory was ever captured** — the gap Packet A closes.

**G2 implementation-contract review 2026-09-24: BLOCKED, then corrected — four contract fixes.**
(1) **The historical count was misidentified.** `n_committed_slots` counts distinct slots with an observed
`RRQ … committed` **within the capture window**, background restores included, so it is neither the boot
playback set nor the library; the earlier "N is the boot playback set, deliberately library-independent"
explanation is **withdrawn**, and subtracting a playback set from the payload set does **not** establish loss.
The report now carries four distinct quantities and an explicit window, and incomplete windows are tested.
(2) **The diagnostic was unavailable in the stuck states it must explain:** `emitPostRestoreCensusIfReady` is
once-per-boot and requires `bootInteractiveReady()` (empty restore queue, no live `SlotLoadSession`, both jobs
inactive), so a waiting request suppresses it. A bounded **on-demand** snapshot is now required independently
of those gates, with incremental collection, snapshot consistency and capture framing; the settled census is
kept for settled memory figures.
(3) **The classifier could not truthfully classify every slot:** observation is now separate from a
**nullable historical cause** (the observed current state carries `empty` / `populated` / `pending`; the cause
carries the four classes plus `ambiguous`, `unknown`), with precedence, full
request identity and completeness, and no single-snapshot historical claims. **(Corrected again in round 2 —
see the state/cause split below.)**
(4) **The fixture gate did not establish stability:** stability is now proven by a **complete per-slot manifest
comparison** across boots, and any unexplained difference is a failure.

**G2 second round 2026-09-24: still BLOCKED on two remaining contracts, now corrected — no broader redesign.**
(a) **The classifier had contradictory mandatory outcomes:** an absent payload with no request and no history had
to be both `empty/not applicable` and `unknown`. The two axes are now explicit and separate — **observed current
state** (`empty` / `populated` / `pending`, a set) versus a **nullable historical cause** — with `empty` never a
cause, a prior inventory establishing only the *disappearance* (cause stays `unknown` unless an explicit event
exists), and a required single test using exactly the overlapping input. (b) **The incremental observation had no
ownership or invalidation contract:** it is now owned by `StorageManager` and serviced at a declared **existing**
per-turn service point (no new thread/timer), respects the existing **SD-session admission**, and **invalidates or
restarts** the affected observation when a slot's request generation advances or the set epoch changes — never
combining fields from different generations and never retaining a content reference across slices; unavailable
fields stay `unknown` while the waiting request is still named, and a native **mutation-between-slices** test is
required. Two tightenings: task 3.1 defines the **progress measurement interval as inside the drain call**
(`restoreProgressCounter()` sampled immediately before entry and after return), so an urgent-save discharge alone
does **not** check off S2b.7; and Packet A is renamed **observation-only firmware and read-only analysis**, with
its execution-packet requirement made unambiguous (mandatory for 1.2/1.3, not for the pure analysis tasks).

**G2 third round 2026-09-24: one remaining BLOCKER — request identity does not identify the content version.**
`commitLoadLoopJobPublish()` publishes restored content **under the same `(requestGeneration, setEpoch)`** that
later records its `committed` terminal, so an observation spanning that publication could mix pre-publication
fields with post-publication content with no invalidation firing; ordinary content changes had the same gap. The
invalidation key now also carries a **content revision identity**: the slot's observation is invalidated or
restarted when its committed content changes — including a publication under an unchanged request identity and
any ordinary commit (capture / overdub / edit / clear) — content fields are never carried forward, and a native
**publication-between-slices** test (generation and epoch deliberately unchanged) must prove no mixed record is
accepted. Identity choice (design D2): re-read committed pass count, highest committed pass id, loop length and
committed event count at each slice; `Loop::nextPassId_` alone is rejected because `SlotReset`, the boot load
and the edit-pass restore reset it. Two tightenings: the declared service point must run **independently of
normal load admission** — the unconditional observation tail in `main` (the idle-owner position that already
calls `processHitlSerialCommands` / `emitPostRestoreCensusIfReady`), **not** the admission-gated deferred
load/restore frame — and its test must drive the **actual service hook** with load admission blocked rather than
a directly invoked collector; and `pending` was removed from the historical-cause vocabulary (it is current
state).
**Observation-only attribution may proceed;
the corrected measurement definitions come first, because they may explain part of the apparent instability
without a firmware fix.**

**Wording recorded:** `runtime-working-set-ownership` has **no *currently eligible* native task** left
(S2b.8 and S2b.7 landed); every remaining gate is a device leg.

**2026-09-24 (eleventh session) — S2b.8 — set-change epoch / external resets — landed native-only in `runtime-working-set-ownership`; the archive transfer is reconciled in the change.**

Owner direction: take S2b.8 before S2b.7, with documentation reconciliation first. **Reconciled:** the active
change's `tasks.md` no longer links the archived `deferred-slot-restore-liveness` as a live sibling, and the
transfer of S1.1/S1.3/S1.4/S1.5/S2b.3/S2b.5 plus the new S2b.7/S2b.8 is recorded there. **Landed (native,
behavior):** undo-clear and redo-clear are **decided to be external resets** and now advance the
restore-request generation (an application of DEC-082 clause 1, so no new `DEC-###`); `noteRestoreSetEpoch`
records `superseded` under each request's **original** identity *before* advancing the epoch and now emits its
`RRQ` transition — the audit found two real defects (the record was re-stamped with the new epoch and read as
*current*-but-terminal, and the reason was never emitted); the dead, non-emitting
`invalidateAllRestoreRequestsAsSuperseded` was removed. **Recorded, not faked:** no runtime set-change entry
point exists (the set boundary is boot, a full `resetAllRestoreRequestState`), so none was invented; and "a
different owner committing that slot" already terminates as `unnecessary` plus pre-publication rejection, so
it was not re-wired. **Evidence:** full native **2,348 / 1 skipped / 0 failures** (baseline 2,344);
`test_storage_manager_load` 35/35, `test_drain_reentry_callback_safety` 8/8; capture-serial SUCCESS
`variables 96,192 / code 391,080 + padding 2,136` (1,296 B above the 840 floor, `code + padding` constant so
no bank cross); both fixes proven **discriminating by removal** and reverted byte-identical. **No device
run** — nothing hardware-observable is introduced, so no device class is claimed. **Next:** S2b.7 —
drain-driven restore advancement (characterize advancement against a correctly named wait; the commit gate is
not relaxed). Packet and full record: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § S2b.8;
`tasks.md` S2b.8.

**Second slice — S2b.7 DELIVERED: the blocking drain now SERVES the restore it waits on.** It was measured
first (the drain named `slot_load_active` and advanced nothing: progress 1 → 1, phase 1 → 1) and then
implemented: `StorageManager::serviceDeferredRestoreForDrain()` runs the **same** selection/admission + bounded
step the deferred-load frame uses, called once per drain iteration while
`deferredSaveBlockedByActiveSlotLoadSd()`, with a termination clause that reports the named wait as soon as the
restore is gated rather than spending the drain budget. No new owner, loader, queue or admission policy; the
commit gate is untouched (`resumable-midi-boundary-playback-maintenance` task 3.3 still owns the bound). D4
point 1 (parked instance) needed no new code — an explicit promote was written, proven redundant by removal,
and deleted. **Behavior:** another track playing → progress **1 → 8**, `TemporarilyBlocked` /
**`waiting_commit_gate`**, nothing published, request preserved; focused slot with the gate admitting → the
drain commits the restore and discharges the save; parked-only session → serviced, nothing published.
**Evidence:** `test_storage_drain_restore` 10/10, `test_drain_reentry_callback_safety` 8/8 (now links
`DeferredJobScheduler.cpp`), full native **2,352 / 1 skipped / 0 failures**, capture-serial **SUCCESS**
`code 391,080 + padding 2,136`; disabling the service step fails exactly the three advancement assertions
(reverted byte-identical). **Device: NOT RUN** — every claim is native, so no device class is claimed.
**S2b.7 is checked; no native task remains open in this change** — everything left is device-gated (stable
restored library) or a later stage. Packet: `ARCHITECTURE-REVIEW.md` § S2b.7.

**2026-09-24 (tenth session) — `deferred-slot-restore-liveness` archived with a reduced, accurately described scope; the runtime measurements return to `runtime-working-set-ownership`.**

Owner direction: the runtime measurements belong with the parent change and archiving the sub-change does
not require completing them. **Delivered and archived:** restore **preparation** advances under active
transport; requests carry identity `(track, slot, requestGeneration, setEpoch)` with five named waits and four
terminals, exactly-one-terminal, and pre-publication stale-identity rejection; the post-restore census
checkpoint is finished and device-verified (`captures/census-final-03/`); content playback passes
(`captures/cc-03-playback/`); and CLEAR's **permitted completion branch** is verified (A3.7 shape (b),
`captures/a37fix-04/`: CLEAR completed through workspace-save priority while a started restore remained
commit-gated). **Transferred, unchecked, and mandatory in `runtime-working-set-ownership`:** S1.1, S1.3,
S1.4, S1.5, S2b.3, S2b.5, plus new **S2b.7** (drain-driven restore advancement — explicitly NOT proven; the
observed completion came from `shouldPrioritizeWorkspaceSaveDrain()` bypassing the slot-load gate) and
**S2b.8** (set-change epoch invalidation). Bound by `resumable-midi-boundary-playback-maintenance` task 3.3.
The S1.3 slope remains blocked on the exhausted approved scratch range (**WITHDRAWN 2026-09-24, G2 review:
"N is the boot playback set" was wrong — it is a window-scoped observed-completion count; the capacity/eviction
reading is also unattributed. See `restored-library-stability`.**; the range
is fully populated; a sweep outside it shrank the committed set 27→27→26 — a capacity/eviction finding, not a
slope). `DEC-082` records the ruling.

**2026-09-24 (seventh session) — the A3.7 shape-(b) fixture is built; a passing run is recorded pending owner confirmation.**

`scripts/hitl_a37_shapeb_fixture.py` gates shape (b) on four sampled preconditions and runs with
**reload-on-clear disabled**, through the intended boot-restore entry point. Passing run
`captures/a37fix-04/` (shipped firmware): target content 2 bars before the press; a restore for track 2
slot 1 `running` at 8.246 s and unresolved at the 27.070 s edge (`waiting_commit_gate` from 10.637 s,
committing only at 35.201 s); a save owed; another track playing (`Playback started` 9.849 s, no
intervening stop). The drain **completed** (`State saved successfully (v4)` → `Clear selected slot 1`),
0 aborts — the "completes" branch. Fixture bug found and fixed: `#CAP` lines flush from a PSRAM ring, so
file order is not device-time order; analysis now orders by device time. Caveats for the owner: the restore
was in the named `waiting_commit_gate` wait rather than holding the SD session, and precondition 4's
evidence is sequence-based. `a37fix-01/02/03/05` were `INCONCLUSIVE` with the failing precondition named.
No firmware changed; the route stays disabled; track 3 untouched.

**2026-09-24 (fifth session) — the restore-liveness ruling is recorded as DEC-082; the change stays open on A3.7 shape (b).**

[`DEC-082`](../DECISION_LOG.md#dec-082--the-restore-request-is-the-unit-identity-named-waits-preparation-under-transport-and-a-three-way-drain)
records the ruling in four clauses (request identity with named waits and exactly-one-terminal; background
preparation admitted under transport with apply still on the global commit gate and the task-3.3 boundary;
the three-way completed/temporarily-blocked/failed drain that advances rather than cancels; and that a named
wait is a valid outcome), with evidence stated **per clause**. The drain-advance **device integration
(A3.7 shape (b)) is recorded as NOT VERIFIED** inside the entry itself, so the decision record does not
pre-claim it. A5.4 is closed; A5.5's closeout table now reads A5.2/A4.3/A5.4 taken+green, A4.2's malformed
half the one optional-unrun, A5.3 partial, and **A3.8 and A5.1 the two open mandatory items**.

Also resolved this session: the "restored yet empty" mismatch I had flagged is **not a defect** — the
earlier probe CLEARed the slot, whose persistence wrote an empty payload, and the reload committed that
empty payload, so the next boot restored an empty-but-committed slot that correctly has no committed
passes. It does name a quirk of the route-enabled behavior: after a CLEAR the stale request makes
`needsSlotLoad` true, so the just-emptied slot is re-requested redundantly.

**Waiting on the owner:** (1) whether to enable the reload-on-clear route in the change (the guard makes it
safe; it is the semantics `advanceRestoreRequestGeneration` was built for), and (2) authorisation to clear a
defined subset of the approved scratch range for the shape-(b) target. No further sweeps or SD cleanup were
run.

**2026-09-24 (third session) — the oversized bar-index allocation is bounded at its owner (`LoopEventStore`); the producer and adopted-content integrity remain unresolved, and `deferred-slot-restore-liveness` stays open.**

The `#MEM_EXT_REFUSED bytes=22369620` abort that blocked A3.7 shape (b) is a **malformed size
calculation, not an allocator defect**. `22369620 = 4 × 5,592,405` and `22369612 = 4 × 5,592,403`: a
4-byte-element vector. The sites are `LoopEventStore`'s bar index
(`std::vector<size_t, InternalHeapFirstAllocator<size_t>>`, `size_t` = 4 on target):
`appendToTailChunk` and `rebuildBarIndex` both did `bar = event.tick / BAR_TICKS;
resize(bar + 1)` with **no bound**. `BAR_TICKS` is 768 and `floor(0xFFFFFFFF / 768) = 5,592,405` — exactly
the observed element count — so the input is a tick near `UINT32_MAX` (uninitialised/sentinel), not
content-derived. `clear()` drops the index and the next `append`/rebuild sizes it from the chunk events, so
a corrupt tick in an adopted/reloaded chunk sizes it in one `resize`.

**Bounded fix (owner: `LoopEventStore`) — this is exactly what it does, and no more.** `MAX_INDEXED_BARS = 4096` (`MAX_INDEXED_TICKS = 4096 × BAR_TICKS`,
far above the 64-bar target so no real loop or over-run take is affected); `append` **refuses** a larger
tick as `LoopEventStoreAppendDeny::TickOutOfRange`, named and logged rather than silently clamped so the
corrupt producer stays visible; `rebuildBarIndex` skips an out-of-range bar defensively for adopted
content. **Reproducer red→green:** `test_out_of_range_tick_cannot_size_the_bar_index`. Full native
**2,344 / 1 skipped / 0 failures**; capture-serial **SUCCESS** `code 391,064 + padding 2,152` (1,312 B
above the 840 floor). Investigation: `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § P1.

**Not resolved, and not to be read into the bound:** the **corrupt-tick producer** (intermittent — see
below) and **adopted-content integrity** — `rebuildBarIndex` skips the bad bar, but the invalid event stays
in its chunk and every other reader still sees the bad `tick`. The bound protects one index, not the
content, and it does **not** show the route safe or semantically correct. **Owner decision: reload-on-clear
stays disabled** — advancing a request generation invalidates stale work; it does not authorise reloading
cleared content, and CLEAR unexpectedly restoring old SD content would violate its intended behaviour.

**Producer: the obvious hypothesis is falsified.** With the route temporarily enabled, a bounded probe
(`captures/cr-probe-01/`) cleared three populated slots and the queued reloads committed **cleanly** — no
`out-of-range tick` refusal, no FATAL. So a clean clear-then-reload does **not** produce the corrupt tick,
and the "torn payload from the clear's own save" hypothesis is withdrawn for that condition. The producer
remains unidentified and its trigger is **intermittent**: re-running the full a37b sequence with the guard
in place produced **no** refusal, no `#MEM_EXT_REFUSED` and no FATAL at all (`captures/prod-probe-01/`), and
that run left the reload `queued` rather than in flight (the scratch track was still overdubbing), so it did
not exercise A3.7 shape (b) either. **Next step (owner direction):** the read-only `committed`-versus-`hasDataInSlot` trace first, then a
fixture built through an **intended** restore entry point that can assert its own preconditions — target
content exists immediately before CLEAR, a restore has actually started (an active `LoadLoopJob`, not a
queued request), a save is owed, and another track is audibly playing. Catching the raise stays open but is
not the next step. **Device scope: track 3 is untouched** — being RAM-empty is not permission to modify its
persisted content — and no further device attempt is warranted until that fixture exists. Also corrected:
"clear-then-reload is the only reachable route" is stronger than the evidence supports; it is the route
tried.

**2026-09-24 (second session) — the post-restore census checkpoint is finished and device-verified; A2.1/A1.8 closed; A3.7 shape (b) blocked by a fatal allocation.**

## Census checkpoint (S1.3/S2b.3 fixture) — DONE and device-verified

`StorageManager::emitPostRestoreCensusIfReady` now emits one **timestamped Tier-A** marker per boot:
`#CAP,<micros>,DIAG,post_restore_census,free_chunks=…,admission_complete=1,interactive_ready=1,undo_hydrated=1,transport_stopped=1,deferred_prewarm_idle=1,heap_free=…`.
Two conditions finished the draft: **deferred-prewarm readiness** (no prepared-pair request and no
post-commit merged-MIDI prewarm still owed; `main`'s prewarm request moved into `TrackManager` so the
checkpoint can see it) and **timestamped telemetry** (the old `#CAP,DIAG,…` form had no micros field and
was not Tier-A). `scripts/hitl_boot_census.py` parses the fields generically, prefers the capture line over
the operator line, and records the readiness attestation and timestamped form in `census.json`.

**Device evidence:** `captures/census-final-02/` — `checkpoint_reached=True`,
`readiness_attested=True`, `timestamped_capture_line=True`, **`free_chunks=451`**, `heap_free=77,824`,
walk pairs 512→480 (enabled set, −32) then 480→480 (library, ±0), S2b.3 condition
`451 >= 0 + 16` met, **0** `could not reconcile`. `captures/census-final-01/` records the
marker-selection defect the fix removed. A4.2's healthy-index half is green from the same log
(`sets/index.bin reconcile ok`); the malformed half stays optional unrun (no destructive-SD permission).

## A2.1 request identity and A1.8 observability — closed except one recorded residual

The remaining named waits are wired (`waiting_capture`, `waiting_job_capacity`; the committing instance
keeps `waiting_commit_gate`), and publication revalidates the captured request identity **before the token
check**, so a superseded/decided request publishes nothing. A user CLEAR advances the generation and records
`superseded`. `RestoreStateProbe` reports state/wait/terminal for the head and both job instances.
**Residual:** the set-change wire (`noteRestoreSetEpoch`) has no production call site (no set-epoch
accessor), so "a set change yields `superseded`" is record-level only.

**Evidence:** full native **2,343 / 1 skipped / 0 failures**; `test_storage_manager_load` **33/33**;
`test_capture_line_tier` **14/14**; capture-serial **SUCCESS**, RAM1
`variables 96,192 / code 391,000 + padding 2,216 / locals 34,880` (1,376 B above the 840 floor).
Commit `505bfc13` plus the follow-up slice (CLEAR generation wire, conservative done-test, shape-b script).

## A3.7 shape (b) — re-attempted, still OPEN on a fatal allocation

`scripts/hitl_a37_shapeb.py` constructs "another track playing + a deferred save genuinely owed + a restore
in flight". Three fresh-boot attempts (`captures/a37b-01/` 16-bar, `-02/` 8-bar, `-03/` 2-bar bisect) died
before the CLEAR with `#MEM_EXT_REFUSED bytes=22369620` →
`FATAL: both internal heap and external memory are exhausted!`. The pool total is **8,380,416 B**, so the
22,369,620 B request is structurally impossible and reproduces at the same value. The only reachable route
to "a restore in flight" is clear-then-reload (every payload slot is restored at boot), and the bisect
shows the refusal follows a `LoadLoopJob done` for the just-cleared slot. This change therefore does **not**
enable reload-after-external-reset (`isLoopSlotRestoreAttempted` stays `state == Terminal`) and records the
allocation defect as a `runtime-working-set-ownership`/recording finding. **Control:**
`captures/a37-control-02/` records and saves cleanly with **0 FATAL**. Shape (b) is mandatory and not
taken, so the change is **not archive-ready** (A5.5).

**A5.2 CLOSED the same round — the base preset was the blocker, not the device.** A direct clock+note
stream into the authorized scratch slot records and saves cleanly (`scripts/hitl_record_content.py`), and
the skill pipeline then passes on the current firmware: `captures/cc-03-playback/content-review.json` is
`result: PASS`, `storage_matches_effective: true`, `CAPTURE_CONTENT,begin,track=5,slot=0,length=1536`,
**11/11 cycles `inventory_equal: true` and `exact_order: true`**, 0 missing / 0 extra / 0 unexplained, 0
`RING,overflow`; run identity `git_commit 0c65a99e`, hex `f3a33208…`. It is a parity PASS (64 rows), not a
capacity re-stress. The base preset still fails (`captures/cpv-final/`) and is a separately tracked
harness/device interaction — it must not be generalised to "recording is broken".

**A5.1 partial:** the updated slope fixture (`hitl_library_growth_sweep.py` now reads the census marker)
reports `N=29`, `post_restore_free_chunks=452` of 512 on three consecutive fresh boots with 0 reconcile
failures — the post-restore budget is stable. **[WITHDRAWN 2026-09-24, G2 review:
`n_committed_slots` is a window-scoped count of observed `RRQ … committed` completions (background restores
included), not the boot playback set; see `restored-library-stability` design D4.]** The **slope itself is not measured, for a structural
reason:** N is the boot playback set (`RRQ … committed`), deliberately independent of the library, so
recording into an *unselected* scratch slot adds a HEADER_READY payload and cannot raise N; the fixture
must select/enable each recorded slot before the reboot. (An earlier note that the sweep selected track 5
instead of track 6 was a misreading of the 0-based walk indices and is withdrawn — the log shows
`Selected track 6`.)

**S1.3 slope — capacity failure point found, measured outside the approved range.** A second sweep into
empty slots (`captures/s13-slope-02/`, track 3 slots 2–4) grew the payload set while the committed
boot-playback set **shrank** (N 27→27→26): each new recording evicted other committed payload slots
(track 6 lost slots 7–8, then 6, then 4–5). The marker read `None` only because the sweep closes at 10 s
and the marker lands at ~11.8 s — a window artifact, withdrawn as evidence. The approved scratch
range (track 5 slot 1, track 6 slots 1–8) was already fully populated, so the sweep wrote to **track 3
slots 2–4, outside it** — disclosed in the change review; no SD delete/rename and no CLEAR, but removal
needs the user's permission. **Growth sweeps are stopped** pending an extended scratch range or explicit
clear permission.

**2026-09-24 — A3.2 attempted and reverted; the blocker is ITCM placement, not the design.**

The pure drain-verdict classifier was built and its five fixtures passed, but the change needed ~96 B of
ITCM beyond the 48 B of headroom and took `padding 872 → 792`. `padding_floor` (840) is a **reviewed
allowance**, and the checker is explicit that the remedy is to move code out of ITCM rather than re-base
the floor — unlike the A2.4 re-base, which was justified because the request record is genuine RAM1 state.

The correct remedy was identified and is the next step: extract the drain's cold failed-report block into a
`STORAGE_PERSIST_MEM` helper. A hand re-indentation of that block corrupted the function, so the whole
attempt was **reverted rather than patched forward**; the tree is clean at `29db1096` with firmware SUCCESS
at the approved `code 392,344 + padding 872`, budget suite 7/7 and load suite 26/26.

Two constraints recorded for the retry: the verdict must carry the gate name (returning only the reason
forces a nested conditional in the ITCM drain, which is strictly worse), and the report-block extraction
must be its own reviewed step with the build run at each move. **Nothing of A3.2 is claimed**, and A1.7 is
still waiting on A3.2/A3.4's pure-decision cases.

**2026-09-24 — A2.5 and A2.6 landed: Packet B's device gate is taken and green.**

The boot restore queue **is serviced after a fresh boot** — the liveness claim this change exists to make.
Clean boot capture (`captures/a25-boot-gate/`, window **3.0 s → 33.9 s**): 87 `RRQ` lines over **29 distinct
`(track, slot)` requests**, each `queued → running → terminal/committed` (29/29/29), **0 requests left
non-terminal**, **0 `RING,overflow`** rows. Per-request latency median 2.78 s / max 3.85 s (measured service
time, not redefined as a MIDI-gap bound). Clause verdicts: every boot payload has a state ✓; `overflow_free`
✓; the previously-`EMPTY` authorized scratch slot restores — the census contains **`5/1`** ✓; MIDI gap within
the standing bound ✓. A2.6 follows: full native **2,316 / 1 skipped / 0 failures**, Packet A's red set
promoted with the flag deleted, RAM1 `code 392,344 + padding 872`.

Two things worth carrying forward:

1. **Resetting this board needs a re-flash.** Neither opening the serial port nor a 1200-baud touch resets
   it (uptime held at 643 s, so the harness attached mid-session and could not see the boot drain). The
   documented reliable reset is `pio run -t upload`. That cost a cycle; it is recorded in `REVIEW.md`.
2. **The gate first required the restore telemetry** (the A1.8 line deferred to Packet B, now
   `SC_RESTORE_REQUEST`/`RRQ`, FLASH-resident after the post-link gate rejected an ITCM version). A device
   gate can be blocked by missing observability, not only by missing behavior.

**Now trackable:** the authorized scratch slot **5/1** holds committed, boot-restored content, so **A5.2**
(content-verified PASS via `dump_and_capture.py` + `verify_playback_content.py`) and **A5.1** (the unblocked
`runtime-working-set-ownership` device legs) are no longer waiting on a stable restored set.

**Progress: 14/36 checked.** Remaining: A1.7 (pure-decision cases with A3.2/A3.3/A3.4), A2.1 (wiring the
remaining wait states), A2.7 (record), Packet C (A3.1–A3.8), A4.2/A4.3, and closeout A5.1–A5.6.

**2026-09-24 — A2.5 device run taken: boot restore liveness demonstrated, gate still open.**

The board was available, so the Packet B device gate was run: firmware flashed
(`teensy41-capture-serial`, upload SUCCESS — also the required reboot) and the canonical base stimulus
run through the authoritative managed path (`host_midi_hitl.py run --preset base`, track 5 / slot 1 /
channel 5).

**The run first required implementing the restore telemetry.** A2.5 needs each boot payload's state to be
*reported*, and the source emitted nothing — exactly the A1.8 line deferred to Packet B. `SC_RESTORE_REQUEST`
now emits one event-shaped `#CAP,...,RRQ` line per request transition (full identity + named
state/wait/terminal); it had to be `FLASHMEM`, because inlined into the ITCM transitions it cost 144 B and
the post-link gate rejected the build.

**Measured** (`captures/host_midi_automation_serial_20260924_012548.log`): 29 requests reached `queued`,
29 `running`, 29 `terminal/committed`; the first went `queued → running → terminal/committed` in **33 ms**;
no request left queued or running; `overflow_free=True`, zero `RING,overflow`. **The boot restore queue is
serviced after a fresh boot.**

**Three clauses are not closed:** the harness attaches mid-restore so the whole boot drain is not in the
log; the managed run's own verdict is `INCONCLUSIVE_CAPTURE` (the documented `record stop did not reach
PLAYING` setup-state race, a harness weakness, not a functional failure), so the MIDI-gap clause has no
valid oracle here; and the previously-`EMPTY`-slot census was not exercised. A2.5 therefore stays **open**
with a clean-run recipe recorded in `tasks.md`/`REVIEW.md`.

**2026-09-23 continued — A3.3's native scope landed (Packet C, per the Revision 5 split).**

The drain's restore accounting is now native-verified while the drain-integration claim stays with the
A3.7 device gate. `SyncDrainBudgetInput` gained `restoreSliceSteps` / `restoreSdBytes` /
`restoreOnlyBlocker` and `buildSyncDrainBudget` charges them, so a large restore cannot be budget-starved
by a persistence-only estimate. `SyncDrainProgressSnapshot` gained `restorePhase` and a monotonic
`restoreProgressCounter`, compared **before** `madeSyncDrainProgress`'s "no active persistence work item"
short-circuit — that ordering is the substantive fix, because the C5 shape has no active persistence work
item, so a drain advancing the restore it waits on previously read as **stuck**.

**Trimmed under RAM1 pressure, and that is the notable part:** the first version also added
`restoreBytesRead` and `restoreParsePosition`. The post-link gate rejected the build (`padding 808`, below
the 840 floor). The honest fix was **not** to re-base the budget — a re-base was available — but to
recognise that the per-grain counter *strictly subsumes* byte and parse-position motion. Removing the two
fields returned the build to the approved line.

Evidence: `test_persistence_sync_drain_budget` **7/7** (three new cases); load suite 26/26; full native
**2,316 / 1 skipped / 2,315 succeeded / 0 failures**; firmware SUCCESS at `code 392,344 + padding 872`.

**Remaining in Packet C, per Revision 5:** the pure-decision oracles for A3.2 (named drain verdict) and
A3.4/A3.4a (three-way caller contract, re-entry guard) are the other native-testable parts; **A3.1/A3.5/
A3.6/A3.7** are the drain-servicing behavior and its device gate. **A2.5/A2.6** remain open on hardware.

**2026-09-23 round 12 — A2.4 landed: the known-red set is empty and the exclusion flag is deleted.**

`deferred-slot-restore-liveness` is **12/36 checked**. A2.4 replaced the permanent attempt latch with a
bounded retry: `kMaxLoopSlotRestoreAttempts = 3` and `scheduleLoopSlotRestoreRetry` reset the slot as the
restore path's **own** cleanup (keeping the request identity and recording a `waiting_retry` wait, per the
A2.1/G2 tightening) and re-queue while under the bound; at the bound the request reaches the **`refused`**
terminal. Retryable now covers the token/epoch/header/size/seek/open failures and the mid-job
reopen/seek/read/token/parse/body-probe failures; an **absent** file stays `unnecessary` with no retry.

**C6 turned green and was promoted into the default gate, so the known-red set is empty and
`RESTORE_LIVENESS_RED` is deleted.** No red fixture was deleted, generalized or threshold-relaxed.
**`loopSlotRestoreAttempted_` is subsumed by the request record** (A2.1's retirement clause), removing a
second 64-byte array and leaving one source of truth for exactly-once.

**Two things worth reading in `REVIEW.md` § A2.4:**
1. The C6 fixture's phase-1 oracle had to be **corrected**. It asserted the old latch (`attempted=1`) as
   proof the injected failure was reached — under the fix that is false, so the assertion would have
   **re-encoded the defect as the oracle**. It now asserts the request is still **owed** and not committed.
2. **RAM1 was re-based deliberately, in the same commit, with the reason recorded** in
   `ram1_budget.json` and `REVIEW.md`: the record stores identity + state + terminal per `(track, slot)`
   where the retired flag stored one bit. `teensy41-capture-serial` → `variables 96,192 / locals free
   34,880 / padding 872` (above the 840 floor, still 12 ITCM banks). Changed accessors were made `FLASHMEM`
   first, which kept the bank count at 12.

Evidence: suite **26/26** default (identical with the now-inert flag set); full native **2,313 / 1 skipped
/ 2,312 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS.

**Open and hardware-dependent, deliberately not claimed:** **A2.5** (Packet B device gate — needs the
board, owner permission to flash, the canonical stimulus and a capture), and therefore **A2.6**. **A2.7**
is a recording task and is now recorded in `tasks.md`. **Packet C (A3.1–A3.8), A4.2/A4.3 and closeout
(A5.1–A5.6) remain.**

**2026-09-23 round 11 — A2.3 landed: the queue serves the first startable request, not the priority head.**

`deferred-slot-restore-liveness` is **11/36 checked** (A2.3 added). `popNextStartableDeferredLoopSlotRestore`
walks the priority order and takes the first **startable** request, leaving a blocked entry queued with its
reason; done entries are still removed as queue hygiene. `queuedRestoreMayBegin` moved into the queue TU so
the selection path and the startable pop share one liveness predicate. Both `BeginBackgroundFromQueue` sites
use it instead of a head-only pre-check.

**C4 turned green and was promoted into the default gate in this same slice** (A1.11). **C6 is now the only
known-red fixture**, and its slice is A2.4 (bounded retry).

Two new cases: priority order preserved among startable entries while the blocked head stays queued and is
never taken; and the A2.2/A2.3 **non-claim made observable** — with one request commit-pending the gate is
global, so a second request is tracked as `waiting_job_capacity` rather than served, and queue ordering
cannot fix it.

**RAM1 enforced twice now:** the first build failed at `padding 808` (below the 840 floor) until the new
helpers were marked `STORAGE_PERSIST_MEM`; the line is back to `code 392,328 / padding 888`. The
`tasks.md` placement rule is a gate, not a note.

Evidence: suite **25/25** default; `RESTORE_LIVENESS_RED=1` → 27 cases / 1 failed (C6 alone); full native
**2,312 / 1 skipped / 2,311 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS.

**2026-09-23 round 10 — A2.2 landed: bounded background restore preparation under transport.**

`deferred-slot-restore-liveness` is **10/36 checked** (A2.2 added). `canPrepareBackgroundLoadLoopNow()` +
`backgroundJobMayBeServicedNow()` admit a bounded background **preparation** slice under active transport,
so a queued request's open/read/parse advances while playback continues. **Commit and cleanup keep the
pre-existing full admission** — `canRunBackgroundLoadLoopNow()` still governs applying, the commit
stop-gate in `main.cpp` is untouched, and an unbounded commit under transport is never entered. Capture
keeps its existing deferral (the preparation predicate returns false when a track is recording), so no
recording behavior changed. A prepare-complete request now reports **`waiting_commit_gate`** by name.

**C1 and C2 turned green and were promoted into the default gate in the same slice** (A1.11). The
known-red set is now **C4 (A2.3) and C6 (A2.4) only**.

Evidence: suite **22/22** default; `RESTORE_LIVENESS_RED=1` → 25 cases / 2 failed (C4, C6 exactly); full
native **2,309 / 1 skipped / 2,308 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1
`code 392,328 / padding 888`.

**Not claimed:** a restore *applying* under active transport (runtime-maintenance task 3.3), or that
queue-head skipping resolves the global `waiting_job_capacity` blockage — that is A2.3's explicit
non-claim and A2.2's `waiting_job_capacity` case.

**2026-09-23 round 9 — A2.1's named-wait transition landed.**

A request blocked by a live owner now reports **`waiting_slot_owned`** instead of sitting `queued` with no
reason — the observable form of C4, and the state the A1.8 probe's `queueHeadStartable` and the drain's
verdict will report. Fixture-first: `test_restore_request_slot_owned_denial_names_the_wait` failed
(*Expected 2 Was 0*) before the wiring, then passed. The transition fires at all three selection denials
(empty-idle priority-head check, focus branch early return, parked/focus branch).

Evidence: suite **19/19**; `RESTORE_LIVENESS_RED=1` → 24 cases / 4 failed (the unchanged Packet A set);
full native **2,306 / 1 skipped / 2,305 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS with
RAM1 `code 392,328 / padding 888`. (One anomalous 2,236-case reading appeared mid-round; two consecutive
re-runs both reported 2,306, so it is recorded as a measurement artifact rather than a lost suite.)

**A2.1 remaining:** `waiting_capture` / `waiting_commit_gate` / `waiting_job_capacity` (the gates A2.2
introduces), the `refused`/`unnecessary` terminals (A2.4), pre-publication identity validation, and
`loopSlotRestoreAttempted_` retirement.

**2026-09-23 round 8 — Packet B started: A2.1's request record and its birth/running/committed wiring.**

`deferred-slot-restore-liveness` remains **9/36 checked**; A2.1 is now **in progress** with its first
slice landed. New TU `src/StorageManager/RestoreRequestState.cpp` gives the restore request an identity
`(track, slot, requestGeneration, setEpoch)`, the queued/running/waiting/terminal state split, five named
waits and four named terminals, exactly-one-terminal-per-identity, validated generation advance, and
set-epoch supersede. The production path now drives it: born `queued` at both queue-entry sites,
`running` in `beginLoadLoopJob`, `committed` terminal in `commitLoadLoopJobPublish`.

Two defects were caught by the new cases rather than by review, and both are fixed: the identity check was
**self-satisfying** (the generation advance wrote the new counter into the record, so a stale record always
compared equal) — now guarded by an explicit `identityValid` flag; and `noteRestoreSetEpoch` recorded each
request's `superseded` terminal and then **cleared the records**, wiping the outcome it had just written.

**RAM1 was enforced, not advisory:** the first version cost 64 B of ITCM (`padding 888 → 824`, below the
approved floor of 840) and the post-link gate failed the build; every helper is now `FLASHMEM` and the line
is back to `code 392,328 / padding 888` with 48 B of headroom.

Evidence: suite 18/18 by default; `RESTORE_LIVENESS_RED=1` → 23 cases / 4 failed (C1, C2, C4, C6 exactly);
full native **2,305 / 1 skipped / 2,304 succeeded / 0 failures**; `teensy41-capture-serial` SUCCESS.

**A2.1 still open:** the wait-state transitions at the denying gates, the `refused`/`unnecessary`
terminals, pre-publication identity validation, and `loopSlotRestoreAttempted_` retirement.

**2026-09-23 round 7 — Packet A closed; A4.1 landed; the A1.7/A3.1 oracle split is now the contract.**

`deferred-slot-restore-liveness` **9/36 tasks** complete (A1.1, A1.2, A1.3, A1.5, A1.6, A1.9, A1.10,
A1.11, A4.1). Packet A's gate is **taken and green**: default suite 10/10, `RESTORE_LIVENESS_RED=1` →
15 cases / 4 failed (exactly C1, C2, C4, C6), full native **2,297 / 1 skipped / 0 failures**,
`teensy41-capture-serial` SUCCESS at RAM1 `code 392,328 / padding 888`.

Two durable outcomes this round, both evidence-driven:

1. **A4.1 — named boot reconcile outcome.** `SetIndexReconcileOutcome { Ok, Repaired, Failed }` with
   `reconcileSetIndexOnSd` kept as the `Ok || Repaired` wrapper; the boot site logs the named outcome
   instead of only warning on failure. New suite `test_storage_reconcile_index` 4/4 green.
2. **Revision 5 (A1.7/A3.1 oracle split).** Measurement showed a native red fixture against the **real**
   blocking drain needs most of the persistence stack **plus** the display stack and the cold
   edit/track-manager owners — close to the whole firmware, so not a test-only closure. The oracle now
   sits where it is tight: **native** for the drain's pure decisions (budget, progress, wait-reason
   classification, caller-contract mapping) and the **A3.7 device gate** for the integration claim that
   the drain advances a restore. The spec delta is unchanged; only the evidence route is. A1.7 stays open
   for its pure-decision cases, which land with A3.2/A3.3/A3.4.

A1.8 is partial by design: the queue/state probe and the reconcile mapping are landed and default-gate;
the capture-tier transition lines and the `terminal` / `prepare-complete-waiting` states land with Packet
B (A2.1), since emitting them now would invent the state vocabulary A2.1 defines. **Next: Packet B —
background restore liveness (A2.1 identity/state separation first).**


**2026-09-23 — `deferred-slot-restore-liveness` Packet A started: A1.1/A1.2/A1.3/A1.11 landed, no behavior change.**

Implementation has begun. A1.1 built the controllable restore harness in
`test/test_storage_manager_load` (`load_and_restore_harness.h`): payloads written with the production
body writer through the production write boundary, explicit runtime-eligibility controls, and a bounded
turn driver over `selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`. A1.11 wired the known-red
acceptance set behind the test-read `RESTORE_LIVENESS_RED` flag; A1.2 (C1) and A1.3 (C2) are the first
two red desired-behavior fixtures, each asserting the **desired** behavior so it is red on the current
tree and green only once Packet B lands.

Measured harness facts now recorded: this suite's default track state is `TRACK_PLAYING`, so every
fixture must set runtime eligibility explicitly; and `probeActiveLoadLoopJob` cannot see a **parked**
job, although a parked instance keeps the `SlotLoadSession` that `deferredSaveBlockedByActiveSlotLoadSd()`
reports — A1.3 adds the observation-only `probeParkedLoadLoopJob`. Packet A's oracle is structural
request ownership (`served || owned`), not byte progress: `probeLoadLoopJobProgress` can read a previous
request's leftover `bytesRead` and made C1 look green on the untouched tree.

**Update (same day, round 2).** A1.5 (C4) and A1.6 (C6) are landed and red with deterministic oracles;
A1.7 (C5) has its real-drain link closure measured and a new hardware-boundary suite
(`test_storage_drain_restore`) proving it links, with the remaining undefined tail recorded as a scope
decision. Two mechanism corrections are recorded in `REVIEW.md`: C4's stall needs a head that fails the
pop's done-test **and** `noLiveOwnerForSlot` (a committed head is skipped, not blocking), and the suite's
`setUp` never cleared the `LoadLoopJob` instances, so an earlier fixture's parked job could own the next
fixture's request.

Evidence: suite default gate 8/8 green; `RESTORE_LIVENESS_RED=1` → **13 cases / 4 failed** (exactly C1, C2,
C4, C6); full native **2,291 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS at
`code 392,328 / padding 888`. Details and packet evidence:
[`REVIEW.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/REVIEW.md). **No `src/` behavior has
changed**; A1.7 (C5) fixture, A1.8 observability and the A1.9/A1.10 Packet A gate remain.

**Round 3 addition (same day).** A1.7's native closure is now **fully measured**: the drain TU plus a
19-TU import set links in the new `test_storage_drain_restore` suite, and exactly three **boundary**
items remain — a directory view for the in-memory SD card (`open(dir)`/`openNextFile`/`isDirectory`/
`name`), a native wall clock in place of `RtcTime` (whose TU includes `core_pins.h`), and a public
overlay setter in place of `src/LooperState.cpp` (which redefines the harness's `looperState` global).
A first directory-view attempt corrupted the shared `SD.h` mid-edit and was **reverted in full**; that
stub is critical-path for every native suite, so it needs its own reviewed edit with the load suite run
immediately after. **No fixture, red evidence or diagnostic count is claimed for A1.7 yet.**

**Round 4 addition.** The in-memory SD card now models **directory handles** (`open(dir)` +
`openNextFile()` + `isDirectory()` + `name()`), which the saved-set scan needs; the load suite, the
critical path for every native suite, stayed 8/8 green immediately after. Retaining the **real** drain
call then corrected the closure estimate: it is not three boundary items but most of the persistence
stack plus cold display/edit/track-manager owners, and it exposes a genuine ambiguity in
`TrackManagerCaptureQueue.cpp`'s `requestLoopSlotPersistAndSaveState` call (an anonymous-namespace
`(const Track&, uint8_t, uint8_t)` overload competing with `TrackStopTelemetryColdHelpers.cpp`'s
`(Track&, uint8_t, uint32_t)`), which prevents that TU from compiling natively at all. `REVIEW.md` § A1.7
records the options; the recommendation is a deliberate "drain native-link expansion" slice, since
A3.3's budget accounting and A3.4a's re-entry guard are logic a device-only gate cannot falsify cheaply.
That ambiguity is now **fixed** (`TrackManagerCaptureQueue.cpp`, one-line explicit qualification with a
comment): the firmware never saw it because it compiles TUs separately, but an include-based harness that
joins them could not compile that TU at all. The fix unblocked the capture-queue TU natively and both the
firmware build and the native suites were re-run after it. The remaining closure is now a precise list of
cold UI/manager symbols (`DisplayManager`, `EditManager`, `TrackManager` LED/memory-pressure,
`Track::silenceSlotMidiOutput`, the content-undo builder), whose TUs additionally need the OLED library's
include paths in the native env.


**2026-09-24 — A3.5 restructured for the MIDI topology; A3.7 device shape (a) PASSED.**
A3.5's first form serviced MIDI from inside the drain's holdoff wait, which the HITL timing verifier
rejects: every `midiHandler.handleMidiInput()` call belongs in `src/main.cpp` (exactly nine) because the
drain runs on the interval path (`ARCHITECTURE_RULES.md`, `NAMING.md`). The drain now **reports the named
`post_load_holdoff` immediately** instead of waiting it out, preserving the restore and the owed save; the
holdoff expires on its own 100 ms deadline while `main` services MIDI, and the next attempt completes the
save. Topology gate **9/9 no extras**; `test_storage_drain_restore` 6/6; full native **2,337 / 0 failures**;
capture-serial **SUCCESS** `code 390,936 + padding 2,280`. **A3.7 device gate, shape (a) "nothing else
sounding": PASSED** on track 5/slot 1 with a fresh boot and a 24-bar take — `Clear waiting for deferred save
completion` → `State saved successfully (v4)` → `Clear selected slot 1` in <243 ms, with **0** `Clear
aborted`, **0** `Persistence drain stuck`, **0** `drain blocked`, and the timing verifier clean
(`captures/a37-clear-05-fresh/`). Shape (b) completed but did not exercise the save-owed-while-playing case
(partial, recorded). Driver: `scripts/hitl_clear_drain_a37.py`.

**2026-09-24 — A3.6 DEC-081/capture preservation LANDED; Packet C native work complete.**
Three regression cases in `test_storage_drain_restore` (real deferred-save scheduler): a `TRACK_PLAYING`
track leaves `isCaptureRecordingForPersistence()` false and the classifier names no denial (DEC-081 holds);
a recording track parks the multi-frame writers (nothing queued, no write started, the owed save stays
owed); and the mid-pass one-chunk flush still services a real sealed chunk during a take while the
multi-frame writers stay at zero. `isCaptureRecordingForPersistence` semantics unchanged.
**Harness defect found and fixed by the third case:** the in-memory SD stub classified the **raw** mode
argument, so `MidPassChunkPersist`'s seal journal (`FILE_WRITE | O_CREAT`) matched neither branch, fell
through to the existing-file path, and got an invalid handle — the journal had never opened natively and
the whole mid-pass flush was unobservable. The stub now classifies by the meaning-carrying flags
(`O_AT_END` = append, `O_CREAT` = create), because its own `FILE_WRITE`/`FILE_WRITE_BEGIN` fallbacks
disagree with the native core's values. Full native **2,340 / 1 skipped / 0 failures**; capture-serial
**SUCCESS** `code 391,080 + padding 2,136`. **Only the A3.7/A3.8 device gate remains open in Packet C.**

**2026-09-24 — A3.5 holdoff MIDI servicing LANDED and native-verified.**
The finite post-load commit holdoff (100 ms) is now **waited out instead of aborting**: the interval is
declared once (`LoadLoopBudget::PostLoadCommitSaveHoldoffMs`), `postLoadCommitSaveHoldoffRemainingMs()`
reads the remainder **without clearing** it, and `serviceMidiWhilePostLoadCommitHoldoffBlocks(maxWaitMs)`
waits while **servicing MIDI** (`midiHandler.handleMidiInput()` + `yield()`), bounded by the caller's
remaining wall clock. The drain charges real elapsed time against `maxWallClockUs`, enters the wait only
when the pure `holdoffWaitFitsInDrainBudget(remaining, budget)` holds, and otherwise leaves A3.2's
classifier to report the named `post_load_holdoff`. Evidence: `test_persistence_sync_drain_budget`
**18/18** (the fit/does-not-fit bound); `test_storage_drain_restore` **4/4** against the **real** drain — a
fitting holdoff is waited out and an injected DIN message is **consumed during the wait**, while the
production 100 ms holdoff against the minimum drain wall clock (~99 ms) is **not** entered (message
unserviced, holdoff still armed). Red-proven by disabling the wait (`inbox=1`). Full native
**2,337 / 1 skipped / 0 failures**; capture-serial **SUCCESS** `code 391,080 + padding 2,136`. Two native
seams were added for this and are documented in `REVIEW.md` § A3.5: a controllable clock and an injectable
MIDI inbox (shared across `NativeMidiPort` instances, because `MIDI_CREATE_INSTANCE` defines a copy).
**Remaining in Packet C: A3.6 (preserve DEC-081/capture behavior) and the A3.7/A3.8 device gate.**

**2026-09-24 — A3.4a drain re-entry and callback safety LANDED and native-verified.**
The prerequisite for **A3.5 — holdoff MIDI servicing** is in: one scoped owner
(`StorageManagerInternal::PersistenceDrainScope` + `isPersistenceDrainInProgress()`, state = one `bool` in
the measured `StorageSession`), acquired by the detailed drain before any shared mutation. A **nested**
drain returns `TemporarilyBlocked("drain_in_progress")` and the `bool` wrapper returns false, without
changing the outer owner. **Audited and guarded before side effects:** `handleClearTrack` (first statement,
before the stop), `handleUndo`/`handleRedo`, `handleUndoClearTrack`/`handleRedoClearTrack`,
`saveNewSetInternal`, `copySavedSetIntoCurrent`. **Deliberately unguarded:** transport, record, note-edit and
load/save-mode toggle — none resets content or persistence state. New suite
`test_drain_reentry_callback_safety` **6/6** injects a CLEAR through the **real** MIDI route
(`midiButtonManager.handleMidiNote` → processor → callback → `executeAction` → `handleClearTrack`), with a
free-guard control that reaches the slot mutations and a held-guard refusal with **zero** mutations and
zero silences; red-proven by forcing the predicate false. `test_storage_manager_load` **29/29**;
`test_persistence_sync_drain_budget` 15/15; full native **2,332 / 1 skipped / 0 failures**; capture-serial
build **SUCCESS** `code 391,896 + padding 1,320` (the first build failed the 840 floor at 728, so
`handleClearTrack` moved to FLASH under the change's placement rule — 480 B better than the A3.4 line, no
re-base). `restorePlaybackAfterSlotClear` now has one owner
(`src/Track/SlotClearPlaybackRestore.cpp`). Packets A–D tasks and review: `REVIEW.md` § A3.4a.

**2026-09-23 — restore-liveness plan correction (documentation only).**
The final review's callback re-entry finding is addressed in
[`2026-09-24-deferred-slot-restore-liveness/tasks.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/tasks.md)
**A3.4a — drain re-entry and callback safety**, required before **A3.5 — holdoff MIDI servicing**.
The contract adds a scoped owner guard, pre-side-effect refusal of conflicting callbacks, live eligibility
and target revalidation, and production-dispatch fixtures. CLEAR gates consistently permit named refusals
and reject unexplained aborts. The final falsification tightenings are also incorporated: explicit
queued/running/waiting/terminal states and the actual `saveNewSetInternal()` caller, with corresponding
acceptance fixtures. Firmware and verification tasks remain unimplemented/unrun.

**2026-09-23 DEC-081 LANDED: audible playback no longer gates persistence and never stops for it.**

The owner ruled that the "all tracks silent" persistence gate is **superseded** by *never require stopping,
always keep playing*: a slot may be stopped on the selected track as it is being emptied, but every other
playing track keeps playing. Implemented: `isTransportActiveForPersistence()` →
**`isCaptureRecordingForPersistence()`** (recording only; all four call sites updated), `BlockedByTransport` →
**`BlockedByCapture`**, and — deliberately — **the bounded active slice budget is retained during playback**
(`resolvePersistenceSliceBudgetUs` still gets "anything sounding or recording", keeping
`maxPersistenceMicrosActive` **300 us** instead of the idle fallback **3,000 us**; without that, SD bursts
during playback would be ten times longer). Build SUCCESS at `code 392,328 / padding 888`; full native
**2,289 / 1 skipped / 0 failures**; harness suites OK.

**Device-verified on the discriminating case.** With **five tracks audibly playing** and a deferred save
genuinely owed, the clear's drain **completed**: `Clear waiting for deferred save completion` →
`Draining persistence work queue to SD card...` → **`State saved successfully (v4).`** →
`Clear ignored — track is empty`, in **<833 ms**, with **0** `Clear aborted` / `drain stuck` / `drain blocked`
where the old gate gave 33 stuck iterations then an abort. The other tracks kept playing (musical note-ons
sampled before and after on 1-based channels 1, 2, 3, 4, 6), and the same build reports
`overflow_free=True`. The earlier "not reachable" note is superseded: the two failed attempts had faulty
setup (a transport tap that stopped an already-running transport; a sample with nothing sounding), fixed by
toggling transport until serial confirms `Transport started`. **Standing observation, not a DEC-081
regression:** `DIAG,persist_save` still reaches ~40 ms maxima (39.6 ms before the change, 43.3 ms after) —
a single persist *grain* is not bounded by the slice budget, only the loop is. Full entry: `DEC-081` in
[`docs/DECISION_LOG.md`](../DECISION_LOG.md).

**Earlier 2026-09-23 owner asks (persist-as-you-record, content pass): the content pass PASSES and
persist-as-you-record is device-verified.**

Full findings and the exact remaining gates:
[`docs/Plans/persistence_immediacy_clear_latency_refinement.md`](../Plans/persistence_immediacy_clear_latency_refinement.md).

**Content pass: PASS.** `captures/cp02-playback/content-review.json` → `result: PASS`,
`storage_matches_effective: true`, 3 cycles each `inventory_equal: true` / `exact_order: true` with 0
missing and 0 extra (260 rows = 188 record + 48 + 24 overdub). **The owner's read was right:** the base run's
`stored overdub span count=0/15 … count_ok=False` is a **telemetry false negative**, not a storage failure —
its `stored_overdub_span` carries `sevt_missing: true`, and the harness itself documents the SEVT dump as
temporarily unarmed (`sevt_gate_required = False`, "a missing SEVT dump must not by itself turn an otherwise
valid run into INCONCLUSIVE_CAPTURE"), with a matching temporary note in `TrackDeferredMaintenance.cpp`. That
run's actual issues were `record_stored_revt_missing` and
`persistence_active_snapshot_unavailable_during_save` — also telemetry/observation classes.

**Persist-as-you-record: VERIFIED on device.** With the full shape (`captures/content-pass-02/`) there are
**13 `PERS,mid_pass` rows**, including inside live `OVERDUBBING` (584,590,680 · 584,617,581 us) and `PLAYING`
(598,393,317 · 598,402,101 us) windows, each `ok:t4:s0:cNN:n256` for the slot under test, with
`overflow_free=True`. The earlier "0 mid_pass" was a `--record-only` fixture artifact — that mode never
commits a pass, so its chunks are released and never admitted. No further diagnostic is needed:
`PERS,backlog`'s 3rd field is the chunk queue depth.

**Persist-as-you-record (mechanism).** The incremental mechanism already exists — `LoopEventStore::sealChunk` admits each
sealed capture chunk via `PersistenceQueue::admitSealedChunk`, and `stepMidPassChunkPersist` writes one and
emits `PERS,mid_pass` — but it sat behind `processDeferredSaveState`'s transport gate, so a take lived only
in RAM until the stop. **Landed:** a deliberate one-chunk-per-turn exception in
`DeferredSaveScheduler.cpp` (FLASH-resident helper; guard kept for an active `SlotLoadSession`; not starved
by a parked workspace save; the bundle writers stay parked because they are what caused the 90-598 ms
display gaps). Firmware SUCCESS at `padding 888` (48 B above the floor, i.e. 64 B of ITCM spent). Device: the
risk check passes — a 24-bar take reports `overflow_free=True call_sites=True`. **NOT yet effective:**
`PERS,mid_pass` never appeared, because `main.cpp`'s `runDeferredSaveState` requires
`hasDeferredSaveWork()` *and* the persistence idle-owner allowance, so the scheduler is not invoked during a
take. Next: give the queued-capture-chunk flush its own invocation reason, then verify `PERS,mid_pass`
appears between `EMPTY->RECORDING` and `RECORDING->STOPPED_RECORDING`. No native suite links
`DeferredSaveScheduler.cpp`, so this is device-verified only.

**CLEAR latency — diagnosed.** The clear stops only the **selected** track (`MidiButtonActions`,
`track.stopPlaying()`), while the blocking drain's first gate `isTransportActiveForPersistence()` is true
while **any** track is audible or recording. With other tracks sounding the gate can never clear, the drain
spins its whole stuck budget and CLEAR aborts (`Persistence drain stuck ... Clear aborted: could not
complete deferred save first`) — which also explains the intermittency. Preferred fix: **retire the guard per
slot** (supersede the pending work for the slot being reset and clear immediately), preserving the guard's
stated purpose — keeping the writer off a slot mid-reset — as per-slot supersession instead of a global
wait. Silencing every track would be smaller but trades a slow CLEAR for an audible gap, against the
never-stop-playing rule; a bigger slice budget cannot help while the gate blocks.

**Content pass (owner ask):** `captures/content-pass-01/` -> `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)`.
Healthy: `record loop length 1536/1536`, `persistence handoff result=ok duration_us=74940`
(`loop_persist=691 sliced=True ok=True`). Not healthy: `stored overdub span count=0/15 ... count_ok=False` —
the overdub phase did not register. Per the standing rule that class is **retried, never reinterpreted**, so
no content verdict is claimed.

**2026-09-23 SD temp-write truncation FIXED (a pre-existing defect found by the rwso device session) — the boot catalog reconcile is repaired; a separate capture-loss problem still blocks the content PASS and S1.3's slope sweep.**

Root cause, confirmed in the vendor headers: Teensy's `SD.open(path, FILE_WRITE)` is
`O_RDWR|O_CREAT|O_AT_END` — it **appends** — and `FILE_WRITE_BEGIN` is `O_RDWR|O_CREAT` with **no
`O_TRUNC`**, so it only starts at offset 0. Every "write from scratch" in the persistence layer used
`FILE_WRITE`, and `sets/index.bin.tmp` was never cleaned, so one failed write left a partial temp that
every later write appended to. `index.bin` is `[nextSequence:u32][token:u32]` with the token verified
**from the end**, so a 5-byte leftover plus one 8-byte record puts the token at the wrong offset and
`readSetIndexFromSd` fails — and because the write path only runs after a successful read, the state was
permanent. This is the same class DEC-022 fixed locally at one site.

**Fix.** `CurrentSetStorage::openForRewrite` (remove, then open; FLASH-resident) applied to the **16**
from-scratch sites (temps + the binary-copy destination) across `CurrentWorkspaceStorage.cpp`,
`SavedSetIo.cpp`, `RevisionLoad.cpp`, `WorkspaceSave.cpp`, `RevisionCommit.cpp`; **kept** on the
in-place patches (`patchLastActiveUnix`, `patchAnchorFields`, `writeEpochFileHeaderCrc`), the mid-pass
seal journal and the crash log, with a test pinning that. `reconcileSetIndexOnSd` is now
**self-healing** — an unreadable index is recoverable metadata (`nextSequence` is re-derived from the
folder scan), which is what repairs an already-malformed card. **The native SD stub was wrong** —
create-or-truncate "as the write path expects" rather than as the device behaves — so it now models
append/offset-0 and the class is visible natively.

**Evidence.** New `test_storage_manager_load` cases, **red-proven**: with the remove disabled the
stale-temp test fails `Expected 8 Was 13`. Full native **2,289 / 1 skipped / 2,288 succeeded /
0 failures**; `teensy41-capture-serial` SUCCESS at `code 392,200 / padding 1,016` — **112 B better** than
the pre-fix `392,312 / 904`. Device (`52d5a805…`): **two genuine post-fix boots (only two of the four
post-fix logs contain a boot sequence), 0 `could not reconcile`**, against a warning on every pre-fix
boot, with the census unchanged (enabled set 32 chunks, restore walk
480 → 480 at `credits=0`, payload to 441). Plan:
[`docs/Plans/sd_temp_write_truncation_refinement.md`](../Plans/sd_temp_write_truncation_refinement.md).

**The catalog is NOT yet faithful — the fix's stability clause is unmet.** A save now completes
(`CurrentSet saved successfully (v6 deferred slices)`, where the pre-fix failure was
`first_missing=request` with no save markers), and the post-fix boot still shows 0 reconcile warnings.
But an **attributed slot read** (tap track, tap slot, take the CC feedback in that window) gives track 5
slot 1 = **24 bars before the reboot and 3 bars after it**, while track 2 slot 3 stays 18 bars both
times. So reconcile and the save path are repaired, but the restored set is not yet faithful across a
reboot — which is exactly S1.3's prerequisite. Two method corrections are recorded with it: my earlier
"track 5 reads 3 bars" datapoint was a **read-attribution** artifact (a naive reader spanning taps
reports `EMPTY` for a slot holding 24 bars), and **raw-MIDI record driving must not be used** — a direct
attempt started an *overdub* instead of stopping the record and had to be halted (it wrote only into the
approved scratch slot).

**Round 5 localized the operative blocker for the content run, and corrected round 4's conclusion.**
The run aborts at the clear: on device `Persistence drain stuck after 33 iterations` then
`Clear aborted: could not complete deferred save first`, and in the harness narration
`ACTION RECOVERY_FAILED requested=EMPTY … Clear aborted in firmware (deferred save)`. Cause: the blocking
clear-drain cannot advance while the transport is active, and it has no transport-gate classification (generic
`Stuck`); the harness never printed "transport stop before clear long press" because its transport detector
does not see a start preceding its anchor. **It is intermittent** — of the three latest runs, one cleared and
(earlier) one full run recorded 18432 ticks + an overdub + a save, one cleared without recording, and one
aborted at the clear. A suspected serial-open **reset** explanation is **withdrawn**: all three logs have
`boot_rows = 0`, so none reset. The transport-stop prep fixes capture sparsity (561 → 25,164 lines) but not
the clear, because the harness starts the transport itself before pressing clear. **Round 4's "restored
content is not faithful" is therefore not established either** — that 24-bar reading was taken from an
uncommitted, mis-driven session state. What *is* established is restore **determinism** (two consecutive
attributed reads both gave track 5 slot 1 = 3 bars, track 2 slot 3 = 18 bars); write→restore fidelity stays
**untested** because nothing has recorded since. Next, in owners other than the SD fix: (1) firmware —
classify the transport gate instead of `Stuck`; (2) host — make the pre-clear step actually stop the
transport, which is the effective unblock. **That host fix is now landed** (whole-log-first transport check,
matching the precedent in `_ensure_transport_stopped_before_record_arm`; host tests 48/48, 19/19, 30/30) —
but it did **not** resolve the clear failure, and the failing run's log has **no transport start at all**, so
the transport was never the gate. The drain is blocked by one of the two **load-owner** gates
(`deferredSaveBlockedByActiveSlotLoadSd` = `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()`, or
`deferredSaveBlockedByPostLoadCommitHoldoff`), with a real 512-byte payload owed, and it reports only `Stuck`
because neither gate has a classification. A boot log shows the restore queued then skipped
(`Queuing boot playback loop slot restore 6 pending; first 5/0`, `LoadLoopJob done 5/0`,
`DIAG,lcr,skip,restore`), but the failing run's log covers only the clear window, so the specific gate cannot
be named from existing telemetry — which is the actionable gap: **classify the gates** first, then choose the
behavioural fix on evidence.

**Round 7 MET the catalog-stability clause, and corrected round 6.** With the authoritative full shape
(`--record-bars 4 --overdub-bars 2`) a recorded loop's **body** persists — `loop_persist=4252 sliced=True
ok=True`, `record loop length 3072/3072` — and after a genuine reboot (`captures/sd-fix-full01-dump/`,
`BOOT,load,start` present, uptime 31 s) its slot dumps **`track=5,slot=0,length=3072` with 190 event rows**,
with **0** reconcile warnings; a pre-existing slot stays faithful at `length=13824` / 1,964 rows. **So the
catalog IS stable**, and round 6's "newly recorded content does not restore" is **withdrawn**: that loop was
recorded with the fixture's `--record-only` mode, which never queued a loop-body persist (`loop_persist=0`,
`first_missing=request`, metadata bundles only), so there was nothing on SD to restore. Whether
`--record-only` should queue a loop-body persist is a fixture question, not a catalog defect. **Still open
under `runtime-working-set-ownership`:** the content PASS (needs a clean managed capture) and the S1.3 slope
sweep (now constructible, since persistence works with the full shape).

**Round 6 found the root, and corrected two of my readings.** `SyncDrainFailureReason` now classifies the
drain gate (`BlockedByTransport` / `BlockedBySlotLoad` / `BlockedByPostLoadHoldoff`) and prints
`Persistence drain blocked by <gate>` instead of the generic `Stuck`; it is built and unexercised because
that verification run's clear happened to pass (padding 952). Two corrections: the per-slot
**CC-feedback probe is not a content instrument** — it reported `EMPTY` for slots holding content and
disagreed with itself across a reboot, so rounds 4 and 6's probe-based fidelity claims are **withdrawn**;
the authoritative read is `!CAPTURE_CONTENT`. And `DIAG,lcr,skip,restore` means **a restore is pending, so
content resolution is deferred** — not "the restore was skipped" — so it is evidence *for* the slot-load
gate. **Authoritative fidelity:** a pre-existing committed slot restores faithfully
(`track=2,slot=2, length=13824`, 1,964 event rows), while a loop recorded this session does **not**
(`track=5,slot=0, length=0`, 0 rows) although the harness reported `CurrentSet saved successfully`
**twice**. **One coherent root:** a queued boot restore (`Queuing boot playback loop slot restore 6
pending`) keeps `SlotLoadSession::isActive()` true, which is exactly the gate
`deferredSaveBlockedByActiveSlotLoadSd()` tests — so it explains both the unrestored new content and the
refused CLEAR drain. Next: why the queued boot restore stays pending.

**Also still blocked, and it is a different defect:** the managed capture truncates mid-run (561 and 1,732
lines against a good run's 45,909) with **0** ring-overflow rows, so the firmware's ring did not shed —
the capture stopped. The harness is capture-driven, so it never reaches the overdub and reports
`INCONCLUSIVE_CAPTURE (SPARSE_CAPTURE)` with a measured `record loop length None`. Documented remedy: a
retry. **The content PASS and S1.3's slope sweep stay open until that capture is clean.**

**2026-09-23 `runtime-working-set-ownership` DEVICE SESSION on `feature/runtime-working-set-ownership` (HEAD `26e96539`, `teensy41-capture-serial`, hex `673e20ca…`): the S2b/S1.5 hardware gate is taken, the restored library survives a reboot, and a clear-drain blocker was diagnosed and worked around.**

Flashed HEAD and ran the new temporary stimulus **`STIM-BASE-2-2-01-TEMP-RWSO-24CAP`** (canonical base
recipe with `--record-bars 24 --overdub-bars 24`) on the authorized scratch slot track 5 / slot 1.

**Leg 1 — the per-committed-slot reservation is gone at boot (device).** `free_chunks` pre-restore is **480 of
512** on HEAD against the baseline's **400**; the default enabled set costs **32 chunks** against **96** (12 per
track); and the library restore walk goes `phase=before 480 → phase=after 480` across all eight `(track,slot)`
rows with `full=0,ready=0,credits=0` on every one — **0 chunks per committed slot**, including the two committed
library slots the walk visits. That is the S2b.3/S1.5/S2b-gate claim on hardware.

**Leg 2 — a 24-bar overdub is admitted.** `record loop length 18432/18432` (24 bars exactly),
**`Overdubbing started` twice**, **`op=overdub_start` denials 0**, 0 `#MEM_DENIED` / `#MEM_EXT_REFUSED` /
`FATAL`, SEVT `bars=24/24 count_ok/span_ok/bars_ok`, display `overdub_ok`/`transport_ok` true. The pre-change
RC-MEM-5 leg recorded zero starts against 4 denials, so the capacity path this change must not regress does not.

**Leg 3 — the restore works once but is NOT stable across boots, and my "library can grow" claim is withdrawn.**
`dump_and_capture.py` resets the board on serial open, and its dump showed `CAPTURE_CONTENT,begin,track=5,slot=0,
length=18432` — so a slot *can* come back byte-exact right after its own save+reset. I over-generalised that to
"the library can grow"; a growth attempt falsified it. After a second forced reboot
(`captures/rwso-growth/boot-n2/`) the recorded obstacle reproduces at **catalog** level:
`could not reconcile MidiLooper/sets/index.bin`, `Queuing boot playback loop slot restore 6 pending; first 4/0`,
`Track 6 loaded state: EMPTY` (and track 7), `DIAG,lcr,skip,restore`, and a census whose non-zero-slot set
**changed between boots** (`{track=1,slot=2, track=4,slot=1}` → `{track=1,slot=2}`), and track 6 emits no length
feedback at all. **The obstacle is the SD catalog reconcile, not scratch-slot approval, and S1.3's slope sweep
stays blocked** — the restored *set* is unstable across boots. **[Attribution CORRECTED 2026-09-24: this
paragraph's stated obstacle is withdrawn. `MidiLooper/sets/index.bin` is
`SavedSetCatalog::SetIndex{ uint32_t nextSequence; }`, a saved-set folder counter, so a reconcile failure
cannot by itself omit a slot; the shrinking boots report `reconcile_warnings=0`; `DIAG,lcr,skip,restore` is
a content-resolution line; and `Track 6 loaded state: EMPTY` appears on stable boots too. The instability is
real and now owned, unattributed, by `restored-library-stability` Packet A — see the 2026-09-24 session entry
at the top of this file and that change's `ARCHITECTURE-REVIEW.md` § Task 1.1.]** **One datapoint of mine is withdrawn:** the
"track 5 reads 3 bars" reading was a **wrong-slot read** (I pressed only the track button); track 5 **and slot 1**
reads `24 bars (18432 ticks)`, and a further dump again reports `length=18432`, so this slot does survive the
hard reset. Also recorded: a boot census needs a **forced** reboot (a serial open only sometimes resets), and
repeated runs without a reboot accumulate residency — `live` 4,658 KB → **8,170 KB** with `heap_free` pinned at
its 52 KB minimum.

**Leg 4 — content NOT a formal PASS, but the two "missing" rows are proven present.** `storage_matches_effective:
true` with all 24 bars dense (`per_bar_effective` 70–84; `effective_rows_all = expected_note_cc_rows = 1,746`),
but the verifier's single inter-anchor cycle reports `inventory_equal: false` with **2 unexplained missing rows in
bar 0** (`[128,5,76,0]`, `[176,5,74,101]`). Neither is a documented residual class, so the guide's bar is **not
met**. However, locating each row against the two anchor occurrences (48.001 s apart) finds **one matching
occurrence per cycle for each** — `note76` Off near `anchor − 80 ms` in both cycles, `note74` On near
`anchor + 1 ms` and `+ 44 ms` — so playback carries both; the verifier's cycle-local inventory just does not bind
them (exact tick attribution not established). A third, longer capture (160 s) went
`INCONCLUSIVE_CAPTURE: no unique effective event recurs` because the restored set — and hence the playback mix —
drifts between boots, so **content verification of this slot is confounded by the same catalog defect**.
`capacity_path_exercised: false` (densest bar 84 refs vs the old 256 ceiling): a parity run, not a re-stress.

**Blocker: CLEAR cannot proceed while transport is active.** Two attempts died at
`Persistence drain stuck after 33 iterations … Clear aborted`. `StorageManager::processDeferredSaveState`
returns immediately while `isTransportActiveForPersistence()`, so the blocking clear-drain can never advance; and
the drain has **no transport-gate classification** — `maybeAdmitFinalizeWorkspaceAfterDrain()` returns `true` for
merely-queued work, so it never takes the `AdmissionRefused` fast path and reports the generic `Stuck`. The host
did not step in because the failed log carries **zero** `PERS,*`/`SAVE,*` rows (only `DIAG,persist_save`/
`hostdrain` timing telemetry, which is not a pending marker), and its transport-running check cannot see a
transport start preceding its anchor. **Workaround (device prep): stop the global transport and idle until the
owed workspace save drains, then run.** This defect pair is pre-existing and outside this change; it blocks any
content run on a device that boots with a pending workspace save.

**Also observed (not a measured slope):** repeated runs **without** a reboot accumulate residency — the same
device read `live=8,170KB`, `chunks_free=431`, `heap_free/heap_min=52KB` before the re-flash, against
`live=4,658KB`, `chunks_free=441` at the fresh boot of Leg 1, with the S1.1 per-owner vector
(`tag_total=1776KB`) unchanged in shape. Recorded as an observation; no per-N census exists to attribute a slope.

**A later round tried the non-destructive lever for a formal content PASS and it does not work.** Muting the
other seven tracks (double press on the track button, `Track 1..4, 6, 7, 8 mute toggled` confirmed) silences
**everything**: the 160 s capture carries only LED feedback and clocks, and the verifier returns
`INCONCLUSIVE_CAPTURE` again. Muted tracks still run playback (six `Playback started` rows), so this is
suppression, not a stopped transport. Measuring played channels directly: all unmuted → 1-based channels
1, 2, 3, 4, 6; others muted → **none**. So the slot under test contributes no notes now, although its content is
dumped as present (`track=5,slot=0,length=18432`, 1,746 rows, all 24 bars dense) and channel 5 **did** play in
the earlier 2-cycle capture. **The set of tracks that actually play drifts across resets, exactly as the restored
set does**, so a formal PASS needs the persistence/catalog defect fixed first — it is not reachable by capture
configuration. Mute state was restored and the transport stopped.

**Round 3 closed the last route to a formal content PASS.** Re-recording the 24-bar slot and capturing
immediately — the configuration of the near-parity run — came back **degraded, not faithful**:
`record loop length 19200/18432 (raw 19200)` (a 25-bar over-run where the earlier run sealed at exactly
18,432), `stored overdub span count=0/191` with `overdub_ok=False transport_ok=False` (though the firmware log
does hold two `Overdubbing started` rows, 0 `FATAL`, 0 `#MEM_EXT_REFUSED`), `persistence handoff
first_missing=request` with **no** `PERS,request`/`SAVE,*` markers so the re-record was **not persisted**, and a
**1,574-line** sparse session log against the faithful run's **45,909**. No firmware or SD content was changed
by that round; the transport was left stopped. **Across rounds 1–3 the same condition held:** the persisted
library neither stabilizes nor restores faithfully, so no deterministic single-slot capture can be produced.

**Still open:** S1.3's slope sweep and failure point, the content-verified PASS, S1.4's pass→chunk conversion,
S1.5's device refusal point, and phases B–F. **All of the device items are gated on the persistence/catalog
defect**, which is a StorageManager/load owner outside `runtime-working-set-ownership` and should be its own
change. Full evidence and the exact citations:
[ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § Device
session — RWSO-24CAP. **Note:** the pre-change baseline revision remains `83be2e19`; no baseline firmware was
flashed in this session (its census is already on record).

**2026-09-23 `runtime-working-set-ownership` S1.5 clause 3 answered natively on `feature/runtime-working-set-ownership`: the dense-24-bar seal is diagnosed, and the refusal point is outside the supported envelope — test-only, no firmware change.**

S1.5's third clause ("the smallest N at which a 24-bar overdub is refused") had been blocked for three rounds
by an upstream puzzle: a dense 24-bar construction did not seal at all, and two findings about why were
withdrawn. **The branch is now identified.** `Loop::sealCapture` runs
`CaptureIncrementalSanity::removePairsShorterThanNoteMinLength` **before** its empty check, so note pairs
closer together than `Config::DEFAULT_NOTE_MIN_LENGTH_TICKS` (**8**) are all removed, the live store empties,
and the seal returns `FailedValidation` from the **`capture.store.empty()` branch** — not from the transfer
precondition that both withdrawn findings had inferred from reading the code after seeing a failure their own
construction produced. The signature is exact and asserted: the events really are in the store before the seal,
`beginCapture` holds one tail chunk (free 511), and the failed seal **releases** it (free **512**, nothing
retained), while the positive control at exactly 8 ticks seals `Ok` and keeps its chunk. The withdrawable
"staging-order" rule is also withdrawn: all three append/prewarm orders seal with pairs >= 8 ticks apart.

**The answer.** With a valid construction — 864 pairs = 1,728 events = **7 chunks** per dense 24-bar loop at
the task's recorded ~72 events/bar, committed across the 8 x 8 matrix — boot costs **32 chunks**, and at
**N = 63** the probe's free chunks are **39 before and 31 after**: the 24-bar reserve admission is
**ADMITTED at every N up to 63**, so **the smallest refusing N lies outside the supported envelope**. The
library at N = 63 holds 108,864 events (83 % of the 131,072-event ceiling) and still keeps 31 chunks free, 7
above `CHUNK_RESERVE + ACTIVE_OVERDUB_TRANSFER_CHUNKS` (24).

**Red-proven.** With the pre-S2b.1 per-slot reservation restored, the same construction refuses at **N = 30**
(free = 6 chunks, below the 8-chunk reserve) and the fixture fails on its admission assertion at `free before
probe=0`. So the HEAD answer is a **capacity result of the S2b.1 removal**, not a property of the pool. The
mutation was reverted; `src/` is byte-identical.

**What this does not establish.** The device's refusal point is **byte**-limited (PSRAM), not id-limited — that
is S1.3's device leg — and the ~191-chunk figure the task cites for a 24-bar overdub's peak reservation is
**not reproducible natively**, because those byte terms (views, companion rows, snapshot inputs) are
heap-backed off-device. No device capacity number is claimed.

**Gates.** `test_continuous_capture` **97/97** (+2 cases); full native **2,287 / 1 skipped / 2,286 succeeded /
0 failures** (was 2,285/1/2,284); `teensy41-capture-serial` **SUCCESS** with the post-link RAM1 budget at
`code 392,312 / padding 904 / locals 34,944` — unchanged, as expected with no `src/` change. Full packet and
evidence: [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ P1 — S1.5 clause 3. **Still open:** the device legs (S1.3's slope, S1.5's device refusal point, S1.4's
pass->chunk conversion) and phases B–F.

**2026-09-23 `runtime-working-set-ownership` S1.3 clause 2 landed on `feature/runtime-working-set-ownership`: the synthetic 60-descriptor characterization is green and red-proven — test-only, no firmware change.**

S1.3's **device** slope stays blocked (the restored scratch slot does not survive a reboot — `Track 6 loaded
state: EMPTY`, `index.bin` does not reconcile), so its **native** clause was the unblocked step, and
`S2b gates` requires it. New `measureWorkingSetResidenceAcrossTracks(N)` in `test/test_continuous_capture`
commits one-note descriptors across the 8x8 slot matrix — the existing single-track helper could not express
N > `MAX_LOOPS_PER_TRACK` = 8 — and two default-gate cases now characterize the law at the contract's envelope:
`test_s1_3_sixty_descriptor_characterization_is_well_formed` and
`test_s1_3_residence_is_flat_across_sixty_descriptors`.

**Numbers (exactly one active role in both variants).** Canonical **reservation flat at 40 chunks (205,280 B
device)** at N = 8 and N = 60; the constant decomposes as boot's stop-descriptor tail reserve (32 chunks =
8 tracks x 4 `CaptureStopJobs` x 1 chunk, bounded by the enabled set) plus the one active role's own canonical
copy (8 chunks) — so it is not a claim of zero reservation, but of nothing reserved **per committed
descriptor**. Payload is reported separately and grows 8 -> 60 chunks (41,056 -> 307,920 B); the runtime
**directory** grows 15 -> 60 slots (95,160 -> 380,640 B at the S3.0 `LoopPlaybackRuntime` 6,344 B pin) and is
reported, **not** asserted flat. Inactive descriptors hold **0** canonical credits and **0** transfer horizons
(0 over 59); resident working sets stay **1**; 100 of 512 chunks used, so the reserve is intact and the flat
result is not a capacity artifact. Native byte figures were **rejected as device numbers**: native
`sizeof(EventChunk)` is 8,208 B against the device's 5,132 B, because `MidiEvent`'s `sysexData` union holds a
64-bit pointer. Chunks are the device-identical unit and are what the law is asserted on.

**Red-proven, not assumed.** Re-introducing the state S2b.1 removed (making `prewarmCanonicalCaptureOutput()`
unconditional in `Track::prewarmPlaybackForSlot`) fails **both** cases — `reserved(8)=152` vs
`reserved(60)=452` (~8 chunks per descriptor, the RC-MEM-5 slope) with the well-formedness case catching the
pool exhausting at `free=0` — and seven suite cases go red, which is the expected cross-check against the
1-vs-4 law cases and B01. The mutation was reverted immediately and `src/` is byte-identical (`git diff` on
`src/` is empty).

**Gates.** Full native **2,285 / 1 skipped / 2,284 succeeded / 0 failures** (was 2,283 / 1 / 2,282 — exactly
the two cases added); `teensy41-capture-serial` **SUCCESS** with the post-link RAM1 budget enforced at
`variables 96,128 / code 392,312 / padding 904 / locals 34,944`, byte-identical to the recorded line as
expected with no `src/` change. Full packet and evidence:
[ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § P1 —
S1.3 clause 2. **Still open:** every device leg (S1.3's slope to the failure point, S1.5's refusal-N, S1.4's
pass->chunk conversion) and phases B–F.

**2026-09-23 SESSION CLOSEOUT — `runtime-working-set-ownership` P0/P1 native scope complete on `feature/runtime-working-set-ownership`; the remaining gates need hardware.** Read this instead of the per-round entries below it.

**What landed and is verified.** S0's decisions verified against their landing sites (no re-decision). S1.2's red scaling characterization, now **green**: the per-committed-slot boot reservation is gone, so reserved chunks and resident working sets are flat in recorded slots (they were 96→120 chunks and 8→11 bundles). The accepted-stop latch (S4f.0/S4f.1/S4f.2/S2b.6) with its deferred drive, canonical park and retire-path drain; the three boot/load admission tasks (S2b.1–S2b.3); the S4.0 per-slot liveness predicate with the S2b.4 release; S4f.5's stop-related in-flight consumers; S4f.4's four paused-worker transitions; B03 and B04 in the new `test_storage_manager_load` harness; S3.0's placement pins with a **per-env post-link RAM1 budget check**.

**Numbers.** Full native **2,281 cases / 1 skipped / 2,280 succeeded / 0 failures**; `test_continuous_capture` 91/91; the load harness 5/5. All four firmware envs build and pass their per-env budgets. **RAM1 `padding` improved 872 → 1,096**: the FLASH extractions of `needsSlotLoad`, `resetLoopSlotToEmpty` and `hasRealtimeDeferredIdleWork` more than repaid the 32 B the latch cost, and the post-link check that enforces the approved budget caught two real defects in itself on the way (a configuration-specific pin and a global-vs-per-env baseline).

**Four defects found by fixtures, not by review** — the argument for the fixture-first rule: `startRecordingTrack` ignored the latch and admitted a record start on top of an unaccepted-invisible stop; `finalizeCaptureAndSelectSlot` moved the active slot underneath an unadmitted stop; `LoadLoopJob`'s callers popped a restore request before checking liveness and the queue would not take it back; and `driveAcceptedInteraction` parked a request whose interval was gone, which would have wedged the track forever.

**Two corrections to my own record, kept visible.** An "N=2 refusal" measurement was a misread `SealOutcome::FailedValidation` (an enum read as a count), and a follow-up "seal needs the transfer horizon" finding was **retracted** after its own bisect showed the horizon was not the discriminator — the real lesson is that `loop.appendCaptureEvent` is not the production ingress once the arm has admitted staging.

**Open.** (a) Every **device leg**: S1.1's per-owner census, S1.3's slope to the current failure point, S1.4's undo/canonical-pin baseline, S1.5's smallest-refusal N, and the S2b device gate — all needing the owner's go-ahead to flash. The **baseline revision is pinned and proven** (`83be2e19`, firmware-identical to the branch base, with the reproduction recipe and per-leg measurement list in `ARCHITECTURE-REVIEW.md`). (b) S3.0's pins for members the later stages introduce. (c) Phases B–F of the change itself.

**Recommended next step:** a device session — baseline `83be2e19` first, then HEAD — using the documented recipe and per-leg list.

**2026-09-23 `runtime-working-set-ownership` P0 started on `feature/runtime-working-set-ownership`: S0 verified, S1.2 red characterization landed — no firmware change.**
First slice through the DEC-080 execution-packet workflow, on a branch cut from the planning line (the
change does not exist on `dev`, so committing the handoff docs as the branch base was the only base that
preserves it). Packet and full evidence: [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § P0.

- **S0 (decisions-only) verified, not re-decided:** every item checked against its landing site — DEC-076
  points 1/2/8, DEC-078, `design.md` Revisions 2/3/4/7/12/16/18, `tasks.md` S0.5. No code change follows
  from any of them. One observation recorded rather than skipped: S0.6 asks for the resumable work-item
  contract "as a convention plus `DEC`", and there is **no dedicated `DEC-###` id** — DEC-076 point 8 with
  Revision 16 and `NAMING.md` § Pipeline of role-owned stages bind it.
- **S1.2 (red scaling fixture) implemented in `test/test_continuous_capture`**, driving the production
  boot/load admission and the record-arm admission (no copied lifecycle): two law cases behind
  `WORKING_SET_RED=1` plus a default-gate well-formedness case.
- **Measured, at a constant active set:** boot admission reserves **96 chunks with 1 recorded slot and 120
  with 4** — a slope of exactly **8 chunks = `PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS` per admitted boot
  slot** — and resident working sets go **8 → 11**. Canonical **payload** is recorded separately from the
  **reservation** (payload 1 → 4 chunks), so the slope is not confused with content. Both law assertions
  fail as designed: `WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` → **2 failed,
  78 succeeded** (`Expected 96 Was 120`, `Expected 8 Was 11`); the default gate stays green at 78/78.
- **Verification:** full native **2,263 cases / 1 skipped / 2,262 succeeded / 0 failures** (baseline
  `7f90d996`: 2,262 / 1 / 2,261); `teensy41-capture-serial` **SUCCESS**, RAM1 `variables 96,128 /
  code 392,344 / padding 872 / locals free 34,944`. Only a test file changed, so the firmware artifact is
  unaffected and `padding 872` is the recorded line.
- **D0.2/D0.3 started (partial, same session):** the P1 harness controls that S2b's fixtures need are in
  place — test-controlled transport state (the harness's hardcoded `isTransportRunning()` is now a
  variable reset in `setUp`), reservation denial/recovery through **real pool-credit exhaustion**
  (`LoopEventStore::ChunkReservation`), and a read-only ownership census on
  `TrackCaptureStopAccess` (`canonicalReservedChunks`, `canonicalOutputAdmitted`, `liveRingEntrySlot`,
  `transferHorizonAdmitted`, …). **B01 is now red as designed:** with four committed slots and one
  active role, the three inactive slots hold **8 canonical-copy credits each (24 total)** where the law
  requires zero. Outstanding and named rather than implied: the dispatch pause-and-step control, the
  linked load / transport-retarget paths, and B07's allocation census — the last is **not measurable
  natively** because the pool wrappers are `teensy41-capture-serial` link flags only. **No production
  behavior has changed; only a test file.**
- **First production behavior landed (same session): the accepted-stop latch.** DEC-078's core — an
  accepted stop is retained and executes once when the canonical-copy target returns, instead of being
  dropped and needing a second press — is implemented and native-verified. `beginDeferredOverdubStopForSlot`
  (S4f.0) parameterises admission by the interval's slot; `Track::PendingInteractionLatch` (S4f.1, Stop
  variant) holds `{kind, destination, slot, switchTarget, interval, phaseTick, transportEpoch}` with no
  ring capacity; the dispatch branch sits in `processDeferredIdleMaintenance` before the owner-specific
  branches (S4f.2, via the named entry `serviceAcceptedInteractionTurn`); and
  `parkAcceptedStopForCanonicalOutput` re-runs the slot's reservation and reports
  `accepted_stop_finalizer_wait` as a wait (S2b.6, partial). New fixture **B02** is green in the default
  gate: one press under a denied canonical target → retained → exactly one job → exactly one completion.
  **The S3.0 placement pin earned its keep:** the `unique_ptr` form of the latch cost 64 B of RAM1 and
  took padding **872 → 856, below the documented floor**; storing it in the already-external ring object
  restored the line to `variables 96,128 / code 392,344 / padding 872 / locals free 34,944`, identical to
  the pre-change build. Full native **2,264 / 1 skipped / 0 failures**. **S2b.1 (boot/load reservation
  removal) is deliberately not in this step**, so phase A is not complete; S4f.1/S4f.2/S2b.6 are recorded
  as partial and S4f.5 (the 49 in-flight consumers), S4f.3, S4f.4's epoch counter, S4.0, S2b.2-S2b.5 and
  B03-B07 remain open.
- **Next slice pinned by reading the tree, not assumed.** Two hazards must ship with S2b.1 (boot/load
  reservation removal): (1) `Track::startOverdubbing` never admits the transfer horizon, so with the boot
  reservation gone a loaded slot's stop would fail `transfer_denied` forever — that admission belongs at
  the overdub start's cold point, **not** in the park, because
  `prewarmActiveOverdubTransferHorizon` relocates `passes.overdubPasses` and must not run mid-interval;
  (2) `prewarmCanonicalCaptureOutput` requires an empty `canonicalCaptureOutput.store`, which the retire
  block never touches, so S2b.1 must ship with S2b.6's retire-path invariant or a loaded slot's second
  overdub can wait forever. Recorded in `ARCHITECTURE-REVIEW.md` § P1 rather than implemented in a hurry.
- **S2b.1/S2b.2 landed: boot/load canonical admission is gone, and the residency law is green.** The
  three red characterizations written hours earlier now **pass in the default gate** and the temporary
  `WORKING_SET_RED` gate is deleted: reserved chunks are flat in recorded slots (was 96 vs 120), resident
  working sets are flat (was 8 vs 11), and inactive slots hold zero canonical credits and zero transfer
  horizons (was 24 credits over 3 slots). `Track::startOverdubbing` now admits its own reserve set for a
  slot boot left cold via the FLASH-resident `admitOverdubStartWorkingSet()` — the view/bridge/companion
  scratch (without it the stop's estimates are `UINT64_MAX` and admission denies as
  `preparation_service_unavailable`), the transfer horizon, and the prepared-companion-history horizon
  (without it the job admitted and never retired) — while START stays **not** gated on the canonical copy.
  S2b.6's retire-path invariant drains `canonicalCaptureOutput.store` at retirement, which is what makes
  the removal safe. Two implementation findings worth keeping: granting the companion history
  unconditionally broke the DEC-072/5.12 adversarial reentry case (`prewarmPreparedCompanionHistory`
  resets the Loop's prepared history), so it is gated on no job being in flight; and the whole admission
  body had to move out of ITCM-resident `startOverdubbing` into FLASH (568 → 856 padding recovered).
  **Reported deviation, not silently accepted:** RAM1 `padding` is **856 against the recorded 872 line** —
  16 B of linker alignment, `code + padding` constant at 393,216, **no bank cross**, `locals free`
  unchanged at 34,944. It needs the architect's call (re-base the line with that arithmetic, or find the
  16 B elsewhere) before S3.0's static_assert work. Full native **2,268 / 1 skipped / 0 failures**.
- **S4.0/S2b.4 core landed: the per-slot liveness predicate and the canonical-reservation release.**
  `Track::noLiveOwnerForSlot(slot)` scans the **live ring window only** (a retired entry still carries
  `slot == 0`, so a whole-array scan would block release forever — the B6 stale positive), plus the slot's
  `captureStopOwner`, an in-flight prepared handover, and the S4f.1 latch. The published pair is
  deliberately not a term (it stays valid while the slot sounds). `Track::tryReleaseCanonicalCaptureReservation`
  enforces the predicate itself so no caller can bypass it, refuses a non-empty store, and is idempotent.
  Fixture `test_s4_0_release_respects_the_live_ring_window_and_is_idempotent` is green; full native
  **2,269 / 1 skipped / 0 failures**; RAM1 unchanged at `padding 856` (every new function is FLASH-resident,
  so the reported deviation did not grow). S4.0 and S2b.4 stay **open** with their remaining terms recorded:
  the job-owned-undo-root term (needs S5.2a's per-slot descriptor), the deferred-reconstruction term (S5.2e),
  and the mid-copy release fixture.
- **S4f.5's stop-related consumers landed, and the audit found two real holes.** Most start/switch
  guards already held transitively because a latched stop leaves the interval in `OVERDUBBING`, but two
  did not: `TrackManager::startRecordingTrack` checked only `hasPendingDeferredOverdubStop()`, so it
  admitted a record start on top of an unaccepted-invisible stop; and `finalizeCaptureAndSelectSlot`'s
  switch path fell through to `setActiveLoopIndex` after a *non-admitted* `stopOverdubbing()`, moving the
  active slot underneath the accepted stop — which the contract forbids in this intermediate slice. Both
  fixed via the new `Track::hasStopInFlight()` / `hasLiveCaptureInterval()`, also adopted by the three
  persistence predicates and `resolveActiveCaptureTrackSlot`. Fixture
  `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes` is green; full native
  **2,271 / 1 skipped / 0 failures**. **The RAM1 deviation grew from 16 B to 32 B** (`padding` **840** vs
  the recorded 872; `code + padding` still constant at 393,216, still no bank cross, `locals free`
  unchanged) — the two new ITCM call sites to the FLASH-resident predicate. Making the manager entries
  FLASH-resident is a placement decision for the architect.
- **S3.0 placement pins landed** (the pulled-forward prerequisite for the fields this slice added).
  `Track::placementPins()` pins the latch layout platform-independently and, device-only, `Track` and the
  ring object; a separate block pins `LoopPlaybackRuntime` 6,344 / `Loop` 984 / `TrackPlaybackRuntime` 32.
  Device sizes were **measured, not guessed** (template probe), and the pins are **effective** — a
  deliberately wrong latch pin failed the device build in all 8 TUs that include `Track.h`. The pins emit
  no code (`code` 392,376 / `padding` 840 unchanged). **S3.0's own RAM1 clause is still not met:** padding
  **840** vs the recorded 872 (`code + padding` constant at 393,216, no bank cross, `locals free`
  unchanged at 34,944). That re-base-or-move-ITCM decision is the one open item blocking a clean S3.0.
- **RAM1 decision taken and enforced (architect, 2026-09-23).** The 32-byte ITCM increase is **accepted**;
  the padding floor is **re-based 872 -> 840** for this slice and the manager entries stay in ITCM (moving
  them to FLASH would be a separate execution-placement change needing its own latency verification). The
  correction that mattered: **size pins do not close the placement gate** — `static_assert` cannot see
  instructions, linker veneers or alignment, so the earlier "the pins make any further drift a build error"
  claim is withdrawn. Enforcement is now **post-link**: `scripts/ram1_budget.json` holds the approved budget
  and the floor's history (edited deliberately, never auto-updated) and `scripts/check_ram1_budget.py` runs
  the platform's own `teensy_size` on the linked ELF via `AddPostAction` from `[env:teensy41]`, failing the
  build when the ITCM region, RAM1 variables, the local allowance or the floor change. Verified both ways:
  the normal build prints `variables 96,128 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,944`
  and passes, and temporarily raising the floor to 900 **fails the build** with an actionable message.
  **840 B is still tight** — the next bank cross would drop the local allowance to ~2,176 B — so a
  deliberate cold-code placement improvement is planned as its own change, kept out of this
  runtime-correctness slice.
- **S2b.5 split into a done half and a harness-blocked half.** The predicate S2b.5 needs **is**
  `Track::noLiveOwnerForSlot(slot)` (S4.0), and its per-slot scope is now asserted in the S4.0 fixture: with
  a live job on slot 0, `noLiveOwnerForSlot(1)` is **true** -- a finalizing predecessor blocks a load into
  *its* slot and not another slot of the same track, which a track-level "capture active" flag would fail.
  The remaining work is local (`LoadLoopJob.cpp`'s callers pop the restore entry *before* `beginLoadLoopJob`,
  and re-queueing is inert, so a load can destroy a live owner) but it is **not implemented**: that path has
  **zero** native coverage -- the native `SD.h` stub is two lines and no suite links `LoadLoopJob.cpp` or
  `LoopSlotRestoreQueue.cpp` -- so shipping it would be an unverified change to load behavior. The harness
  gap and the full B04 plan (files to link, in-memory SD/File emulation, stub boundaries, assertion
  sequence, and the ordering against B06 and the mid-copy fixture) are pinned in
  `ARCHITECTURE-REVIEW.md` § P1 -- S2b.5. Full native **2,272 / 1 skipped / 0 failures**.
- **Two decisions recorded for the architect (2026-09-23), both about not inventing on the implementer's
  authority.** (1) **S4f.4's transport-epoch counter**: the drive already satisfies the epoch's behavioural
  requirement -- it re-derives the destination from live transport state and the boundary tick from
  `getCurrentTick()`, never trusting the stored absolute tick -- and no existing tick-domain epoch can be
  reused, so the contract's new `TrackManager` counter is new RAM1 state (`Track`'s `.bss` additions are
  documented as costing ~4 KB of locals each) with only a diagnostic use. The latch currently stores a
  `transportEpoch` nothing consumes, which is worse than wiring or removing it; **recommended: drop the field
  (latch 40 -> 32 bytes, update its pin) and record the epoch as satisfied by live re-derivation**.
  (2) **The load harness is a multi-slice build**, with increment-1 findings from a one-build probe: the
  native link script allow-lists only `test_continuous_capture` for the Arduino/SD stub path; the `SD.h` stub
  declares only `struct File {};` with **no `SD` object**, so an in-memory SD/File emulation is needed rather
  than a header tweak; and `CurrentSetStorage::verifySaveFileTokenAtPath` is not visible, so those units must
  be linked. Nothing was committed from the probe and the tree is unchanged.
- **Shared load harness increment 1 landed: the load path now links natively.** New suite
  `test/test_storage_manager_load` compiles, links and runs `StorageManager/LoopSlotRestoreQueue.cpp` --
  the path had **zero** native presence before. Blockers removed at the right boundary: the firmware
  `SD.h` stub (two lines, no `SD` object) is now an in-memory card; the harness defines `ARDUINO 1` because
  `CurrentSetStorage`'s SD declarations -- including `verifySaveFileTokenAtPath` -- sit behind
  `#if defined(ARDUINO)`, **which is precisely why this path was never natively testable**; the native
  serial stub gained `println`/`flush` (activated by that define); and `native_test_link.py`'s hardcoded
  suite name is now an allow-list. Two facts now asserted that shape the S2b.5 fix: the queue orders
  **ascending** by `restorePriority` (the first test asserted descending and failed, which is how the real
  contract got pinned), `peekFirstPendingLoopSlotRestore` has **no done-test**, and a **forced enqueue
  already exists** (`appendBootLoopSlotRestore`) -- the contract's alternative -- so S2b.5 need not invent
  one. Next increment: link the `TrackManager`/`Track`/`Loop` set so `needsSlotLoad` and the pop done-test
  go live, then drive `beginLoadLoopJob` for B04, and only then make the S2b.5 call-site fix. Full native
  **2,275 / 1 skipped / 0 failures**; all firmware envs pass their per-env RAM1 budgets.
- **Load harness increment 2 pinned to a measured structural choice.** Increment 2 needs
  `Track`/`TrackManager`/`Loop` because `StorageManager::needsSlotLoad` ends in
  `track.getLoop(slot).hasCommittedPasses()`. Probing the obvious route -- adding the queue to
  `test_continuous_capture`, which already links those -- produced exactly three error classes and was
  **rejected**: it would force `ARDUINO 1` for that whole TU (changing what *every* included production TU
  compiles, since the tree's SD declarations sit behind `#if defined(ARDUINO)`, and the suite's 87 tests
  currently depend on those guards being off), delete two now-duplicate stubs from `off_path_deps.h`, and
  expose an unknown cascade through 87 unrelated tests. **Recommended next increment:** extract the shared
  production include block into `test/test_support/track_harness.inc` and include it from both suites --
  the load path gets its Track objects, the existing suite's boundary surface is untouched, and the include
  set stops being duplicated. The probe was fully reverted; tree clean and `test_continuous_capture` 87/87.
- **Load harness increment 2 landed: the load path links with the full Track harness.** The earlier
  rejected route's problem was dissolved rather than worked around. `test/test_support/track_harness.inc`
  now holds the shared production include block **plus** the harness globals and native clock boundary,
  extracted verbatim from `test_continuous_capture` (include order is significant, so it moved as a block)
  and included by both suites -- so the include set stops being duplicated instead of gaining a second copy.
  `off_path_deps.h` gained one explicit seam, `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`, so a suite linking
  the real loop-slot-payload functions opts out of those two stubs while every other suite keeps the
  fail-closed route; and the load suite links `MidiHandler.cpp` directly because `TrackManager`'s
  constructor references the global handler and this suite has no second TU to carry it. Verified at each
  move: `test_continuous_capture` **87/87** after both extractions, load suite **3/3**, full native
  **2,275 / 1 skipped / 0 failures**, firmware + per-env budget SUCCESS. **Next: B04 itself** -- build a
  finalizing interval on a slot, queue a restore for it, run the real selection/admission and assert the
  entry is still queued, the Loop was not reset and the finalizer is intact; only then the S2b.5 call-site
  fix against that red fixture.
- **B04 narrowed to a one-step-away, fully specified increment.** With the harness in place the defect
  mechanism was written as a fixture, which sharpened B04's trigger rather than just its shape: the caller's
  **pop destroys the request** (`queueDeferredLoopSlotRestore` is inert for a slot with committed passes or
  one already attempted), and the slot's **destruction** (`resetLoopSlotToEmpty`) happens later and only when
  the loader runs for a slot whose payload is absent or invalid. So B04's discriminating state is **a live
  finalizing owner on a slot whose queued restore has no valid payload** -- which also means the
  request-loss half is observable with no SD at all. The fixture named exactly three missing symbols:
  `StorageManager::needsSlotLoad` (live in the `src/StorageManager.cpp` monolith -- **recommended: extract it
  into its own small TU**, a ~25-line predicate over Config, the attempted flag, `SlotLoadSession::isActiveFor`,
  the active/parked job queries and the committed-passes check), plus `CurrentSetStorage::formatLoopSlotPath`
  and `verifySaveFileTokenAtPath` (link `CurrentSetStorage.cpp`). Nothing was committed from the fixture; the
  load suite stays 3/3 and the tree is clean.
- **Load harness increment 3: the load decision is extracted and the SD card is complete.** Three verified
  steps: (1) `StorageManager::needsSlotLoad` moved out of the `StorageManager.cpp` monolith into
  `src/StorageManager/SlotLoadAdmission.cpp` so the load *decision* links without the persistence layer --
  pure predicate, **FLASHMEM**, and the firmware's RAM1 line is byte-identical (`code 392,376 / padding 840`)
  which the post-link budget check confirms; (2) the load suite links `CurrentSetStorage.cpp` and
  `SlotLoadAdmission.cpp`, so `formatLoopSlotPath`/`verifySaveFileTokenAtPath` are real production
  definitions; (3) the in-memory SD card gained `rename`, `FILE_WRITE`, `File::write` and create-or-truncate
  `open` (additive; `test_continuous_capture` still 87/87). **B04 is now one triple of symbols away:**
  `SlotLoadSession::isActiveFor` (`SlotLoadSession.cpp`) and `isActiveLoadLoopJobFor` /
  `isParkedLoadLoopJobFor` (`LoadLoopJob.cpp`) -- so the next increment links those two TUs, re-adds the
  fixture, watches it go red, and only then makes the S2b.5 call-site fix. The fixture was reverted again to
  keep the suite green at 3/3; full native **2,275 / 1 skipped / 0 failures**.
- **B04 is reproducible, and the S2b.5 defect is worse than "a lost request".** The last linkage step landed
  (SlotLoadSession, LoadLoopJob, LoopPendingNoteChange, TrackManagerSlotMatrix; plus two documented seams --
  `HARNESS_ARDUINO_STUB` for a duplicate native `micros()`, and the earlier
  `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`). The fixture shows that for **a slot with committed passes** --
  exactly a finalizing predecessor's state -- the pop's done-test treats the request as already satisfied and
  **silently discards the entry, returning false**, and `queueDeferredLoopSlotRestore` then refuses to take it
  back, so the obligation is **unrecoverable with no signal at the pop**. Re-queueing is inert for committed
  slots; the Loop's destruction is the later consequence, when the loader runs for a slot it was told to load
  and the payload is absent or invalid. Load suite **4/4**, full native **2,276 / 1 skipped / 0 failures**,
  firmware + per-env budget SUCCESS. **Next:** the bounded S2b.5 call-site fix (peek/pre-check or the forced
  enqueue), then re-point the fixture at the fixed behaviour.
- **B04's live-owner fixture attempted; the goal narrowed and the fix is fully specified.** Two clarifications
  worth keeping: the owning predicate is a live **job**, not committed content (`noLiveOwnerForSlot`
  correctly ignores committed passes, so the fixture must build a real admitted stop job), and that state can
  be built **without** the MIDI/note-edit layer -- open the interval on the Loop directly and admit through
  `Track::beginDeferredOverdubStop` via the existing `TrackCaptureStopTestAccess` friend contract, whereas
  routing through `midiButtonActions` drags in `analyzeEditSessionInteractions` and
  `resolveConstrainedGeometry`. Remaining linkage, in the order it appeared: `StorageLoopIo.cpp` +
  `StorageLoopIoBodyFormatV1.cpp` (they define `releasePersistedLoopSnapshotChunks`, despite
  `PersistedLoopSnapshotRelease.cpp`'s name), then `LoadLoopBudget.cpp`. **The fix is designed and local:**
  guard `LoadLoopJob.cpp`'s four pop sites with `noLiveOwnerForSlot(entry.track, entry.slot)` before the pop
  (peeking first at the two `popNext` sites, which have no filter), leaving the entry queued and returning
  when a live owner holds the slot. Load suite **4/4**, full native **2,276 / 1 skipped / 0 failures**.
- **`resetLoopSlotToEmpty` extracted -- and it gave RAM1 headroom back.** The destruction the load path
  performs on a slot whose payload is absent or invalid (the function B04 exists to prove is *not* called)
  moved from the `StorageManager.cpp` monolith into `src/StorageManager/SlotReset.cpp` and is now **FLASHMEM**.
  The enforcement caught both halves: the first cut, left without the attribute, **failed the post-link
  budget check** at `code 392,440 / padding 776` (the split had pushed 64 B into ITCM); with `FLASHMEM` the
  line is `code 392,184 / padding **1,032**` -- **+192 B of headroom** against the line this change had
  spent, reported as headroom rather than absorbed, and the floor stays 840 as instructed. A side effect is
  that a suite can now link the destructive function without the monolith. **The live-owner B04 fixture is
  one symbol from linking**: `StorageManagerInternal::storageIoFromFileRead(File&)`, which lives in the
  heavyweight boot-load TUs and would pull much of the persistence layer -- a deliberate stopping point, with
  the fixture's design already recorded. Load suite **4/4**, full native **2,276 / 1 skipped / 0 failures**.
- **S2b.5's retirement half is blocked by a harness artifact, now named.** The fixture's second half ("after
  retirement, restoration happens exactly once") needs the job drained to retirement, which makes
  `TrackUndo::prepareOverdubSessionEntry` live, which needs `src/TrackUndo.cpp`. Linking it collides with the
  already-linked `MidiButtonActions.cpp`: **both define `restorePlaybackAfterSlotClear`** -- `TrackUndo.cpp` at
  namespace scope, `MidiButtonActions.cpp:118` inside an **anonymous namespace**. Separate translation units
  never collide; an include-based harness concatenates them into one. The obvious seam
  (`HARNESS_OMITS_MIDI_BUTTON_ACTIONS`, left guarded in `track_harness.inc` for the next attempt) then breaks
  differently, because `off_path_deps.h` stubs `ControlSurfaceManager`/`MidiFaderActions` whose **headers**
  arrive only via `MidiButtonActions.cpp`. So the obstacle is the harness's single-TU assumption, not the load
  path. Two routes: give the load suite its own minimal stub surface instead of reusing `off_path_deps.h`, or
  split the shared harness into "button headers" vs "button TU". Load suite **5/5**, full native
  **2,277 / 1 skipped / 0 failures**, firmware + per-env budget SUCCESS.
- **S4f.4 transition 3 was a defect, and it is fixed.** Writing the load/replacement fixture showed the drive
  sent *every* refusal to the park -- including a request whose interval is gone because a load or replacement
  left the same slot number. Parking waits for a canonical reservation unrelated to the failure, so the
  obligation was retained forever and `hasStopInFlight()` stayed true, blocking every later start, switch and
  release for that track. The drive now recognises a stale interval **before any admission attempt**, reports
  it, consumes the obligation exactly once and admits nothing. Fixture
  `test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound` is discriminating. S4f.4 transitions
  **1, 3 and 4** are green natively; transition 2 (seek/reset/MIDI Start) has no native entry because those
  `ClockManager` entries are harness stubs. Full native **2,279 / 1 skipped / 0 failures**; firmware + per-env
  budget SUCCESS at padding 1,096.
- **S4f.4's four paused-worker transitions are all green natively.** Transition 2 (seek/reset/MIDI Start) was
  the last: `test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution` latches an accepted stop at
  tick 96, moves the tick domain before execution, drives, and asserts the admitted boundary is **not** the
  stored tick and lies in the new domain -- discriminating, since a drive that used `request.phaseTick` would
  make the two equal. **Coverage limit stated in the fixture:** the `ClockManager` seek/reset/MIDI-Start entries
  are harness stubs, so the domain is moved with the controlled clock; that proves the decision, not the
  production entry that moves it (device territory). Full native **2,280 / 1 skipped / 0 failures**; firmware +
  per-env budget SUCCESS at padding 1,096.
- **Still open:** S1.1's per-owner byte census, S1.3's device slope to the current failure point, S1.4's
  undo/canonical-pin baseline, S1.5's smallest refusal N, and the whole S1 device gate — **no hardware run
  in this session**. Next: the remaining D0.2 controls, then S2b.0/S2b.1, which is expected to turn the
  three red cases green and to retire the `WORKING_SET_RED` gate.

**2026-09-23 — chat reference readability.** Agent replies now pair every stage/task/gate/decision ID
with a short descriptive name, including repeats and link labels. Canonical rule:
[NAMING — short names beside IDs](../Authority/NAMING.md#short-names-beside-ids-in-chat), linked from
AGENTS.md and the always-applied naming rule. Presentation only; no runtime or task-status change.


**2026-09-23 — deterministic implementation workflow strengthened (DEC-080).**
[Implementation readiness](../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
now requires a proportional execution packet for every future slice, including steps of existing changes:
fixed transition oracle, controlled production-path fixture, repeatable recipe and actual evidence before
checkoff. AGENTS.md, Cursor rule, OpenSpec config and agent roles share that authority.
The current plan's [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
adds P0–P8 and S2b cases B01–B07; D0 tasks prepare controls/fixtures before behavior changes. Fixtures are
**specified only**, not implemented or run. No firmware or hardware action; no implementation task checked off.


**2026-09-23 — runtime-working-set-ownership implementation handoff corrected (DEC-079).**
The five targeted review findings are resolved in the active OpenSpec's
[IMPLEMENTATION_CONTRACT.md](../../openspec/changes/runtime-working-set-ownership/IMPLEMENTATION_CONTRACT.md)
and task/spec text: self-contained S2b with pulled-forward Stop/liveness prerequisites, per-holder
cardinality budget, job-retaining regeneration, S4c contract-only with phase F explicitly uncreated,
and stop/switch/start composition with explicit supersession. S0/S1 → S2b remains the first slice;
its gate uses current resident content and a synthetic descriptor fixture, not a 60-long-slot library.
Current whole-loop playback roots stay accounted until F. No firmware, build, upload or device run;
all implementation checkboxes remain open. This supersedes earlier handoff wording, not device evidence.


**2026-09-19 implementation readiness is now a gate, and the OpenSpec config no longer restates volatile state
(DEC-077).** Workflow/documentation only — **no firmware change**. New:
[`docs/Authority/IMPLEMENTATION_READINESS.md`](../Authority/IMPLEMENTATION_READINESS.md) (G0 evidence → G1
architecture → G2 contract → G3 apply, with the STOP conditions for Apply), three roles in
[`docs/Agents/`](../Agents/README.md) (`implementation-investigator`, `implementation-architect`,
`implementation-red-team`), and matching loadable skills in `.agents/skills/`. `openspec/config.yaml`
`rules.design` / `rules.tasks` now require G0–G2 and forbid architecture-discovery tasks; its dated
milestone narrative is replaced by a pointer here and to
[`CURRENT_WORK.md`](CURRENT_WORK.md).

Two things this explicitly is **not**: it is not a second stop gate (the
[formal trigger list](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) still decides reassessment, and the
one-per-session limit is unchanged — see DEC-077 § 1), and it does not change ownership or bounds. The
structural constants stay in the config because they are current: `NUM_TRACKS = 8`,
`MAX_LOOPS_PER_TRACK = 8` (`include/Globals.h`); what was stale was the milestone narrative, not the counts.

Filed while writing it: `ClockManager::setBpm(uint16_t)` (`src/ClockManager.cpp`) assigns `bpm` and reprograms
the tick timer with **no clamp**, while `setBpmFloat` clamps 20–300 and both the boot-load and legacy-validation
paths require 20–300. Two entry paths to one owner state with different invariants; not fixed here.

**2026-09-19 `runtime-working-set-ownership` ran G0 → G1 → G2, and the G2 red team returned BLOCKED — design
input landed, no firmware changed.** G0 (six read-only traces) verified the law violation (per-slot
`runtimeBySlot_` never released; a sounding slot requires a resident whole-loop root; a busy-owner stop is
**dropped** and must be re-pressed; undo pins snapshot residency with no demotion; no residency-vs-library test
exists). G1 produced the architecture; the red team then falsified **eleven reachable paths** — the worst being
that `finalizeCaptureAndSelectSlot` ignores a stop denial and moves `activeLoopIndex` anyway, that boot reserves
8 pool chunks (41,056 B) **per committed slot** (480/512 at the 60-slot target, reintroducing the RC-MEM-5
state), and that the contract's own fields add ~1.4 KB of DTCM against **872 B** of usable padding.

Landed as design input: **DEC-078** (accepted — every interaction accepted; finalization is **per interval**;
amends DEC-067 and DEC-076 point 2, withdrawing DEC-076's "finalization is a queue" bullet);
`openspec/changes/runtime-working-set-ownership/design.md` **Revision 18** (findings, latch, cardinality and
liveness, invariants 32–36, placement, cold-undo pinning, caps); `tasks.md` restructured (**S2.9 extracted** to
the sibling change `windowed-playback-generation`, new **S2b** boot/load stage, per-slot liveness, slot-
parameterised acceptance); and `CURRENT_WORK.md` § Now implementing's stale 32-lease envelope corrected. The
change stays **Design (S0 pending)**; the revised contract must be re-attacked before any task is executed.

**2026-09-18 the overdub live-capture one-frame paint flicker is CLOSED WITHOUT A FIX and folded into the
memory-pressure work (user decision).** No firmware change was made and no GitHub Bug was opened. Two
measurements from the reference capture
([`captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log`](../../captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log))
made the fold the honest close rather than a punt, and correct one assumption in the OPEN plan:

- **The dip is real, already measured, and in the compose - not the draw.** The on-change `DISP` family is
  emitted at `Transfer` from `resolvedPaintNotes()`, the same vector `Draw` read, so a lost row shows up as
  a decrease. The whole capture has exactly two decreases: the `23.001 s` clear (`RECORDING`, 32 -> 0) and
  **`49.972 s`: `OVERDUBBING`, `frameNotes` 75 -> 74, restored to 75 at `50.019 s`** - one painted frame
  with one row fewer, right after the `49.946 s` MIDI event. The OPEN plan asserted the existing telemetry
  could not catch a one-frame dip (true of `DFRAME` at every 30th frame) and made denser compose
  instrumentation its first step; the on-change `DISP` family already catches it, so that step is
  withdrawn.
- **The paint's own source state cannot advance during the window.** The same capture carries **52**
  `#MEM_DENIED,op=snapshot_reserve,reason=heap_low,need=40960B,heap_free=68KB,pool_refused=0/0B` rows,
  continuously reported (1 s report window) from `27.64 s` to `79.06 s` - across the entire overdub.
  `kSnapshotPublicationReserveBytes` (40,960) plus the 32 KB `kOperationHeapFloorBytes` needs 73,728 B
  against 68 KB free. DEC-075 makes a denial deliberately non-fatal (previous root stays selected), so the
  published root the paint composes from is held at the arm's heap state.

Handed to `runtime-working-set-ownership` (now the ACTIVE scope in
[`CURRENT_WORK.md`](CURRENT_WORK.md) § Now implementing), with the invariant *a note present in the
composed paint set before and after a note-on/note-off must be painted in every frame in between* and a
cheap re-measure recipe: after the lease/publication repair, scan the same fixture for `DISP` **decreases**
inside the overdub window (no new firmware telemetry required); if the dip survives a healthy gate, re-open
[`overdub_live_capture_one_frame_paint_flicker_bugfix.md`](../Plans/overdub_live_capture_one_frame_paint_flicker_bugfix.md)
as a paint-path RC owning `DisplayManager::resolveDisplayNotesByDrawSource`. The tidy-up exoneration
(P1-P6 behavior-preserving; base `4aacd18e` reproduced the blink) stands, so `runtime-code-tidyup` remains
out of scope.

**2026-09-18 `DIAG,droid_queue` is off in the default capture env, and that also removed the 4 KB RAM1 snap.**
`SESSION_CAPTURE_DROID_QUEUE=0` now sits in `[env:teensy41-capture-serial]`, with
`teensy41-capture-serial-investigation-diag` setting `=1` as the explicit debug gate and
`teensy41-capture-serial-no-grain` keeping its own explicit `=0` (its contract is the acceptance contract). The family
fires on every enqueue/coalesce of the DROID control-surface feedback path, was 5,636 rows (~310 KB) on the 2026-09-18
eight-bar record+overdub fixture, and with the per-slice `PERS,work` trace shed the 1 MiB ring, evicting the
`snapshot_unit`/`ST` rows the base-run oracle is read for; the counters and the `logger.log` overflow warning are
unaffected. Two measured effects. (1) Ring budget: with only `droid_queue` off, the fixture reached **17,412** `#CAP`
rows against 3,287 in the shed runs and got past the phase-confirmation abort, but `DIAG,lcr` was then 12,664 rows /
~966 KB and still overflowed the ring -- so the acceptance env (which also drops `DIAG,stop_grain` and `DIAG,lcr,*`)
is still required; there the same fixture logs **2,410 rows with no `capture_ring_overflow`**. (2) RAM1: the emitter's
own format string was the RAM1 `.rodata` literal that triggered the 4 KB `.data`/`.bss` alignment snap chased in the
entry below -- with the family dropped, the build reads `variables 91,968` / `code 392,152` / `padding 1,064` /
`free for local variables` **39,104** instead of 96,064 / 35,008. So the drain fix that had to be reverted at 2,240
now lands with the largest allowance this tree has had. This is the documented "a new capture format string can cost
4 KB" hazard in [`Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc), and it makes
dropping a high-volume *emitter* both a ring-budget and a RAM1 lever. The base preset's remaining misses on this
fixture are its setup-oracle class (a 2x-tempo run: 780 ticks/s against 384 nominal, sealing a 17-bar loop for
`--record-bars 8`, the same artifact already recorded for the DEC-074 session), not content.

**2026-09-18 the blocking `saveState` drain is fixed and device-verified, and the RAM1 tree gained a DTCM bank
(`immutable-loop-snapshot-passes` 5.7.6; `12fdf38d` -> reverted `d3290020` -> re-landed `0a036c63` on linker change
`ea9918ac`).** The RC recorded below this entry was a **monotonic persistence work-entry table**: no path ever returns
an entry to `NotScheduled`, and `LoopPersist` is keyed by `LoopId`, which is fresh for every new loop, so each
clear/record cycle permanently consumes one of the 256 slots. Once exhausted, `admitWork` refuses **every** key --
`FinalizeWorkspace` included -- while `currentWorkspaceSave.pending` stays set, so `hasPersistenceWorkPending()` is
true on `pending` alone with nothing to step, `stepPersistenceWorkItem` returns success on that state, and the
blocking drain spins its stuck budget and fails (`Persistence drain stuck after 33 iterations (max 33, expected ~1
slice steps, ~0 SD bytes)`), refusing CLEAR for the rest of that device generation. The state was pinned by
arithmetic before any code change (`SyncDrainBudget.cpp:66` sets `finalizeSliceSteps` only when `pending &&
queueDepth == 0 && writingWorkItemCount == 0 && !itemActive`, so `expectedSliceSteps = 1` with 0 SD bytes means
`pending` alone; `maxSliceSteps = 1*3/2 + 32 = 33` and `maxStuckIterations = min(64, 33) = 33` match the log exactly).
Fix: `admitWork` reclaims a `Persisted` tombstone when the table is full (lossless: nothing reads `workState`,
`visitScheduledWorkItems` skips tombstones, and a fresh admission reproduces the re-queue shortcut), never touching
`Queued`/`Writing`; `maybeAdmitFinalizeWorkspaceAfterDrain` reports whether work is available; the drain fails fast
with `SyncDrainFailureReason::AdmissionRefused` and a PROGMEM diagnostic. Two native fixtures fail on the pre-fix
behaviour (verified by disabling the reclaim) and pass after; suite 15/15, full native **2252 / 1 skipped / 0
failures**. **The fix first had to be reverted**: it costs 4 KB of DTCM, and at 13 ITCM banks that left `locals free`
2,240, which hung the boot load silently (`BOOT,scan,start`, no crash record). Controls proved it was the 4 KB and not
the logic -- the pre-fix firmware booted and streamed 1136 rows with the same SD state, and a **semantically neutral
edit** to either touched TU reproduced the same 96,064 / 2,240 and the same hang. With no movable DTCM left, the
lever was ITCM: `linker/imxrt1062_t41_lcr.ld` now routes ten cold TUs (boot/load, SD-bundle serialization,
telemetry, note-edit undo, set-browser read -- none in the MIDI tick, capture append or playback serving) to FLASH,
taking `code + padding` 425,984 -> 393,216 = **13 -> 12 ITCM banks** and `free for local variables` 6,336 -> 39,104
(with the fix re-landed, **35,008**). Device (`captures/drain-fix-03`, git `0a036c63`): clear precondition
**satisfied**, 0 `Persistence drain stuck`, 0 `admission_refused`, 0 `FATAL`, 0 `#MEM_EXT_REFUSED`, 3,287 `#CAP` rows
and **12 `PERS,work`** rows where the wedged runs had 0; boot probe 1,023 rows, `BOOT,load,ok`. **Caveat:** `padding`
is now 1,256 B, so the next ~1.2 KB of new ITCM code crosses back to 13 banks (~7 KB allowance); taking the next bank
means moving another ~32 KB of cold code. Method and failure signature recorded in
[`Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc).

**2026-09-18 the snapshot publication boundary now admits memory per construction turn and parks on allocation failure
(DEC-075, `immutable-loop-snapshot-passes` 5.9).** The reset recorded below is fixed at its boundary rather than by
shrinking the footprint. **RC-PUB-1:** `Loop::stepPendingSnapshotPublish` admits every turn that could start, step or
freeze a candidate, through `OperationKind::SnapshotReserve` -- which existed with an 8 KB margin and had **zero call
sites** -- with `kSnapshotPublicationReserveBytes = kSnapshotSlabBytes + 8 KB`, on top of the gate's 32 KB heap floor
and its pool-refusal recency window. A denial writes nothing: the publication stays pending and the previous root
stays selected, which is the state DEC-074's `hasSettledPublishedSnapshotForSave` already handles. Publishing an
already-frozen candidate is deliberately not gated (it allocates no new content memory). The gate's only per-turn
caller now bounds its repeat `#MEM_DENIED` *report* to one per second per shape while the decision stays exact on
every call, the way `recordPoolRefusal` bounds its own storm. **RC-PUB-2:** `noteFragmentIncorporationFailure` parked
only `MembershipCapacity`, so an `Allocation` failure rebuilt the fragment every turn -- copying its
`LoopSnapshotChangeSet` onto the internal heap and retrying the slab -- and a freeze failure was not counted at all;
`Allocation` now parks transiently and the freeze branch counts through the same path. The park is released only
after admission has denied while parked and then admitted again, so the same admission state that let the fragment
fail cannot re-arm it. `MembershipCapacity` keeps its permanent park and `Injected` keeps retrying, so the DEC-047
transaction fixture is unaffected. **RC-PUB-3 corrected:** routing `SnapshotSlabAllocator::allocate`'s `abort()` to
the non-fatal path is neither expressible (device `-fno-exceptions` leaves `allocate_shared` no failure channel, and
a custom deleter would put a control block per node on the internal heap) nor reachable
(`allocateSnapshotShared` pre-reserves `128 + align(sizeof(Type))`, so pressure surfaces as the already-non-fatal
`snapshotSlabOpen` failure); the abort stays as the invariant check, tagged `site=slab_allocate` against the
reachable `site=slab_open`, documented in place. The allocator that actually aborted is the shared
`ExternalMemoryFirstAllocator`, whose abort-instead-of-deny semantics remain the wider defect recorded in
[CURRENT_WORK.md](CURRENT_WORK.md); this fix stops the publication from driving it there. Evidence:
`test_publication_turn_waits_for_admitted_headroom` (a denied turn leaves the wrap journal, the published root, its
revision, the in-flight-construction flag and the failure count all unchanged, and the same publication then
completes) and `test_allocation_failure_parks_publication_until_admission_recovers` (two injected `Allocation`
failures park, an admitted turn makes no attempt, a denial records the pressure, the next admission releases it and
the root becomes current), both in `test_loop_take_survival`; full native **2249 / 1 skipped / 0 failures**;
`teensy41-capture-serial` SUCCESS with RAM1 `variables 91,968 / code 425,560 / padding 424 / locals free 6,336` --
byte-identical to DEC-074, i.e. no FlexRAM bank cost. **Device evidence 2026-09-18 (parked path,
[`rcpub-gate-05`](../../captures/rcpub-gate-05/session_20260918_114025.log), git `dc9a1107` clean, hex `7f6bd8e3…`):
0 `FATAL`, 0 `#MEM_EXT_REFUSED`, device alive, and 49 `#MEM_DENIED` -- every one `op=snapshot_reserve`,
`reason=heap_low`, `need=40960B`, `heap_free=68KB`, `pool_refused=0/0B` -- while `snapshot_unit`,
`snapshot_fragment`, `snapshot_slab_fail` and `snapshot_alloc` are all 0: the boundary parked with nothing written,
and `heap_free` held at 68 KB (min = median = max over 73 samples) with `snap` unchanged at 2214 KB and pool `live`
6507 -> 7881 KB then flat.** The crash log is the counterfactual (same boundary, no gate: heap 108 -> 0 KB, `snap`
2409 -> 2543 KB, then `abort()`), and the denial report is confirmed rate-bounded on device (~1/s across the 50 s
window). **Owed:** the wrap case specifically (that run's denials landed during `RECORDING` and the harness aborted
before the wrap on a 1 MiB `#CAP` ring shed, `Result: INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`) and the
resume-and-complete path once the heap recovers (the run ended while still denied, so resume is native-proven only).
`--record-bars` accepts only `[1, 2, 4, 8]` while the crashing loop was 24 bars (`TICKS_PER_BAR = 768`), so a
faithful repro also needs that cap raised. `kSnapshotPublicationReserveBytes` remains the tunable: the margin requires
72 KB of internal heap, the DEC-073 RC1 control settles at 104 KB, and this run was denied at 68 KB -- the intended
discriminator.

**2026-09-18 RC recorded: the snapshot publication boundary has no memory admission, so a full PSRAM pool resets the
device instead of parking the publication (`immutable-loop-snapshot-passes` 5.9, RC-PUB-1..3).** User report: the
stress run crashed a few bars past the overdub wrap, overdubbing an existing overdub. Device
[`session_20260918_101615`](../../captures/session_20260918_101615.log) is not a fault -- there is no crash record
(`boot_reset,crash=0` on the next boot, and `abort()` raises no `CrashReport`); the reset follows
`[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` and
`#CAPTURE_RECONNECT`. `Overdub wrap committed @ tick 18432` at 114.519 s, and then for six seconds the pool's `<=32K`
class gains **one 32768-byte snapshot slab per second** (96 -> 102 blocks) while the internal heap falls
**108 -> 72 -> 36 -> 0 KB**; the pool then refuses `bytes=32768` (119.465 s, the exact `snapshotSlabOpen` request) and
`bytes=8224`, and `ExternalMemoryFirstAllocator::allocate` calls `abort()`. The overdub session itself did not leak:
pool live is pinned at 8201-8203 KB for the whole 45 s before the wrap against the boot's reported `pool=8182 KB`,
because a 12-bar record sits on top of the loaded workspace's reference root (~1.5-1.7 MB of slabs installed by the
boot load) plus the prewarmed view sets (~0.9 MB, 715 KB of it `TagViewPrewarm`). The control is DEC-073's own RC1
device run ([`rc1-coalescing-device`](../../captures/rc1-coalescing-device/session_20260918_100646.log)), which shows
the same ~8-slab post-wrap burst and then settles with 0 `FATAL` -- so the ~256 KB cost is by design and the defect is
that this boundary neither admits nor parks. No persistence ran in the crashing boot (15 `PERS` lines, all boot-time,
none after the wrap; the deferred save needs an overdub stop that never came), so the in-tree RC2/DEC-074 gate is not
involved. Three RCs are recorded in the owning change and cross-referenced from
`successor-capture-reentry-unguarding` 5.9: **RC-PUB-1** wire the dead `OperationKind::SnapshotReserve` admission into
the publication start (point-of-use probe; on denial start nothing and leave the work pending with the previous root
selected); **RC-PUB-2** park `LoopSnapshotFailureReason::Allocation` like `MembershipCapacity` instead of retrying the
fragment forever (each retry copies the fragment's `LoopSnapshotChangeSet` onto the internal heap, and the
freeze-failure branch is not even counted); **RC-PUB-3** remove `SnapshotSlabAllocator`'s fatal `abort()` backstop.
The steady-state footprint half stays with the memory-pressure / DEC-065 track ("idle occupancy leaves no headroom").
No code changed in this entry.

**2026-09-18 RC1 LANDED: a published snapshot root carries its committed-pass coverage, and "current" can no longer
lie (DEC-073, `successor-capture-reentry-unguarding` 5.9).** The post-reboot overdub loss was the publisher stamping a
candidate built for an older committed-pass state with the *current* `playbackRevision`: the session-exit publish and
the undo/redo captured-root selection both did it, so `publishedSnapshotIsCurrent()` was exactly
`box.revision == playbackRevision` and the LSV1 save could gather an undo-shaped 48-row root while `PassesResolve` was
57. Fix: `PublishedSnapshotBox`/`PendingSnapshotPublishBox` carry `coveredPassRevision`;
`publishedSnapshotIsCurrent()` is `coveredPassRevision == committedPassRevision()` (the enumerable pass/companion
inventory revision, an audited alias of `playbackRevision` today); the box sets its watermark only where it actually
enumerates (`queueSessionExitSnapshot`, both `queueAllCommittedPassesInto` callers, `enqueueLiveOverdubPasses` when it
queues, wrap journals via their own `targetRevision`), the session-exit publish passes that watermark, and undo/redo
coverage travels with the retained root (`UndoEntry::before/afterPublishedCoverage`, the job- and session-owned
before-roots, `TrackUndo::selectPublishedSnapshotForEntry(loop, ref, coverage)`). No pending-request clause sits in
the predicate: a later request can only invalidate a root by moving the inventory, which the comparison already reads.
Evidence: the two DEC-073 reproducers in `test_loop_take_survival`
(`test_coalesced_snapshot_request_before_construction_is_not_satisfied_early`,
`test_coalesced_snapshot_request_mid_construction_is_not_satisfied_early`) fail on the pre-fix tree on the same
clause -- a root captured before the live committed-pass state claiming current -- and pass after; suite 137/137,
full native **2246 / 1 skipped / 0 failures**, `teensy41-capture-serial` SUCCESS (RAM1 variables 91,968 / code
425,560 / padding 424 / locals free 6,336; FLASH code 1,007,080). **Device-verified 2026-09-18:** DEC-071 multi-pass
shape with the recipe's undo/redo on `teensy41-capture-serial-no-grain` (hex
`f2f1d8bdfc85b30727fc667e1c548066d1debf4854fa5acae8cdae645c735f1e`, tree `d9eb419a` + this change) --
`captures/rc1-coalescing-device` ran two overdub sessions with undo=1/redo=1 and the post-redo republish
(`q=0,a=4` at 95.474 s) is the root the LSV1 `tail_gather_begin` at 99.640 s gathers; the base report's only issue
is `persistence_stage_missing:result` because the now-complete 600-row tail was still writing when the run closed
serial (the device's own log has `PERS,result,…ok` at 111.14 s). `captures/rc1-coalescing-reboot-playback` is a
fresh boot (`boot_reset,crash=0` at 3.0 s) and reads **`result: PASS`, `storage_matches_effective: true`,
384 + 154 + 38 + 24 = 600 effective, all five cycles `inventory_equal: true` / `exact_order: true`, 0 missing,
0 extra, `max_bar_refs=84`, 0 `RING,overflow`** -- the 24-row second-overdub pass that `content-verify-58` lost is
audible after reboot. `captures/rc1-coalescing-device-playback` (live) is also 5/5 clean. **RC2 landed (DEC-074):** the
LSV1 body admission now requires a *settled* root (`hasSettledPublishedSnapshotForSave()`:
coverage-current **and** no publication pending), the per-loop format is resolved once at `Header` and pinned
(`loopWriteUsesLsv1`), and an unsettled loop writes the passes-authoritative **legacy** body -- LSV1's readers reject
a missing `LoopSnapshot` section, so LSV1 cannot express a rootless body. A bounded settle re-admission
(`kLoopPersistSettleDeferralBound = 128`, nothing written while waiting) lets a transient pending republish finish
before falling back. Native: `test_storage_loop_io` 47/47 (two new fixtures, one red on the pre-RC2 predicate); full
native green; `teensy41-capture-serial` SUCCESS with RAM1 variables 91,968 / padding 424 / **locals free 6,336**
(the first implementation's new telemetry tag cost 4 KB of DTCM allowance via `ALIGN(4096)` churn; dropping it
restored the figure). **Device read closed 2026-09-18** (firmware `86fd8f07…`): the save took the settled LSV1
path (`tail_gather_begin`, `LoopSnapshotTail` 1389 steps, `q=0` publications), 0 `RING,overflow`, 0 `FATAL`, and
post-reboot `storage_matches_effective: true`. The verify's 4 unexplained wrap rows are a setup artifact -- that
session recorded at 240 BPM instead of 120, so its 8-bar request became a 17-bar
`record_loop_length_mismatch`; the intended shape's post-reboot parity is proven clean under DEC-073. The two
earlier attempts' ring overflow is fixed by `SESSION_CAPTURE_LCR=0` in the no-grain acceptance env. Remaining:
6.1/6.2/6.3, and the owning change's RC-PUB-1..3 publication-boundary work (`immutable-loop-snapshot-passes` 5.9).

**2026-09-18 RC recorded for the post-reboot overdub loss (`successor-capture-reentry-unguarding` 5.9).** User report:
after reboot the second overdub isn't loaded. Device `captures/gus-sanity-01` + `captures/reboot-01-playback`: the
committed passes are complete (effective 142 rows; both overdub passes present after load), but the deferred save's
LSV1 tail gathered a coalesced published root that encodes the **undo** state — the loaded body root resolves to 48
display rows vs 57 before the reboot, and playback misses the 24-39 overdub range. The save requires neither
`publishedSnapshotIsCurrent()` nor `!hasPendingSnapshotPublish()`, and the loader installs the body root as current
with no reconcile (the boot hydration lane is gated off). The session-exit publish also stamps the candidate with
the **current** `playbackRevision` regardless of the committed-pass state it was built from, so "current" cannot
prove content coverage. **Fix call: (b) publisher coalescing/coverage correctness first, then (a) a persistence
safety gate; load-time rebuilds are rejected by the owner.** The RC in 5.9 pins the invariant (queue consumption
and candidate coverage advance together; carry a `coveredPassRevision`) and the acceptance (root == `PassesResolve`
immediately before save admission). The stale uncommitted edit that had marked 5.9 closed / ticked 6.1 was reverted.

**2026-09-18 the undo stack now keeps its protected publication capacity (`successor-capture-reentry-unguarding` 8.3).**
User report: the undo stayed at 2 instead of 3 after two overdub sessions, and the next undo fell through to the
first overdub. `TrackUndo::eraseUndoEntriesForSlot` move-assigns a filtered rebuild, which took the smaller
local buffer and dropped the `prewarmProtectedUndo` reserve; `TrackUndoPublication::begin` then rejected at
`cursor >= capacity` and the stop silently committed without a GUS entry (device diagnostic before the fix:
`cap=4`). Fix `433fea68` re-reserves `kUndoStackProtectedCapacity` (constant moved to `GlobalUndoStack.h`);
`DIAG,gus,publish_begin` / `DIAG,gus,drain` added. New native fixture fails without the fix; full native
**2,244 / 1 skipped / 0 failures**, RAM1 locals 6,400. Device gate via the skill's canonical `base` preset,
`captures/gus-sanity-01`, **`Result: PASS`**: `MIDI: Undo (entries=3)`, `publish_begin cap=32` for both
sessions, `passIds_n=2,src=deferred` per entry, `DISP across undo/redo: 56 → 48 → 57`.

**2026-09-18 the deferred stop now marks the committed pass's display bars (`successor-capture-reentry-unguarding`
8.2).** User report: the second overdub vanished from the OLED after its stop and came back at the loop wrap. The
deferred terminal commit only called `notifyCommittedContentChanged` → `markPassDerivedStale`, which leaves
`dirtyBars` empty after a `slice_clean`, so `rebuildVisualCacheIdleSlice` cleared the stale flag as
`slice_nodirty` without rebuilding and the cache kept the pre-commit notes; the wrap's
`markAffectedDisplayCacheRanges` was what brought them back (`gus-undo-04`: 176 → 144 at 74.3 s). Fix
(`a91cb719`): mark the committed pass + prepared companion ids in every deferred terminal commit. New native
fixture fails without it; full native **2,242 / 1 skipped / 0 failures**, RAM1 locals 6,400 unchanged. Device
`captures/gus-undo-06`: frame holds 176 from the second stop through the undo, `before=176 after_undo=144
after_redo=176`. The undo/redo display clause also bounds the post-redo sample at the next undo for
`undo_redo_cycles > 1`; `scripts/test_capture_transitions.py` 48/48.

**2026-09-18 the GUS now records every pass of an overdub session (`successor-capture-reentry-unguarding` 8.1, DEC-068
follow-up).** The reported defect: one undo removed only the last wrapped pass, not the whole overdub. Cause: the
deferred stop admission swaps the interval's wrap identities out of the shared loop into its own session-exit
receipt (DEC-068), but the deferred undo collection read the live session lists at its later `UndoCollection`
phase, when they are empty — so the entry carried only the final pass. Fix: the deferred Pass/Companions phases
read the interval's own session-exit receipt (`SessionUndoCollection` receipt pointers + a bound
`receiptIdentity` that replaces the live generation guard; `beginCollectOverdubStopUndoPasses` takes the receipt
ring; `stepDeferredUndoCollection` passes `job.admission.sessionExitReceiptRing`). The deferred route also emits
the `#CAP,DIAG,gus` row now (`src=deferred`). New native fixture
`test_deferred_stop_undo_entry_carries_every_session_pass` fails without the fix (`Expected 2 Was 0`);
`test_continuous_capture` 75/75, full native **2,241 / 1 skipped / 0 failures**, RAM1 locals 6,400 unchanged.
**Device gate closed:** `captures/gus-undo-01` (managed `base`, firmware from `7a4c8bfc`) logs
`#CAP,66172706,DIAG,gus,slot,kind=1,passId=3,passIds_n=2,src=deferred` (one entry, both passes) and
`DISP across undo/redo: before=189 after_undo=124 after_redo=188` — one undo removes the whole overdub, against the
partial `194 -> 179 -> 193` before the fix. The run's base report is `FAIL` only on the known record
stop-quantisation grid artifact and the redo re-verification span window, not the GUS entry; a second attempt
(`gus-undo-02`) was `INCONCLUSIVE_CAPTURE` (setup).
**Follow-up fixed in `c380c030`:** the deferred admission took `captureSessionExitTail` before the checks that
can still deny it, so a denial sent the stop to the synchronous fallback with an already-emptied live session --
one final pass in the entry. The tail capture now runs after admission is committed. Device `captures/gus-undo-03`
(firmware `7708d605…`, git clean) is a clean **`Result: PASS`**: **`MIDI: Undo (entries=2)`**,
`DIAG,gus,…passIds_n=2,src=deferred`, `DISP across undo/redo: 194 -> 128 -> 192`, `stored overdub span 64/63
count_ok/span_ok/bars_ok`, 0 `RING,overflow`.

**2026-09-18 the 5.8 undo/redo fix's display half LANDED (`immutable-loop-snapshot-passes` 5.8a).** After 5.8
made playback stop emitting a disabled pass, the OLED still painted the undone overdub: the sealed capture
handoff (`retainCapturePreviewAtSeal`) is released only when `sealedCapturePreviewIsCovered()` sees each row in
the committed `visualCache`, so disabling the pass on undo removed the rows from the committed set and coverage
could never hold again; `applyDrawSourceOverlays` then unioned the stale rows into every frame. Device
telemetry (`content-verify-65`): `DFRAME` 194 before `Undo` → **193** after, `DISP frame=193` against
`visual=180` for the whole window, redo unchanged; the extra 13 rows are exactly the sealed handoff
(`194 = 181 + 13`, `193 = 180 + 13`). `resetPassTimeline` already cleared the handoff for this hazard; the fix
makes `Loop::setCapturePassState` do the same when a pass becomes `Disabled`. New native fixture
`test_disabling_a_capture_pass_releases_the_sealed_draw_source_handoff` fails without the fix; full native
**2,240 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 locals 6,400 unchanged. The base
runner also gained `_verify_undo_redo_display`, which rejects a frame that keeps the undone overdub (it flags the
real buggy log `194 -> 193 -> 193`; `scripts/test_capture_transitions.py` 46/46). **Device gate CLOSED:**
`captures/display-undo-03` (managed `base`, git `9eda0984` clean, firmware `47e05029…`, DEC-071 shape + recipe
undo/redo) is **`Result: PASS`** with `undo=1 redo=1`, 0 `RING,overflow`, `overflow_free=True`, persistence
`result=ok`, display `overdub_ok/transport_ok=True`, and **`DISP across undo/redo: before=194 after_undo=179
after_redo=193`** — the frame drops the overdub on `Undo` and restores it on `Redo`.

**2026-09-18 successor 5.9 + 5.11 CLOSED and the defect fixed (`immutable-loop-snapshot-passes` 5.8).** The
user's report — the overdub wrap's second pass shown on the display but not audible — had two independent
causes, both leaving the published root not representing the committed passes: (1) `TrackUndo::
applyPublishedSnapshotSelect` was a no-op when the asynchronous session-exit after-attach had not landed, so a
redo kept the pre-undo root and the entry's pass restore made it stale (live-capture telemetry: window 384 → 576
→ 384 after `Undo` → still 384 after `Redo` against 576 effective); and (2) `Track::processDeferredIdleMaintenance`
admitted snapshot idle work only for `liveRecordPastFirstBar || inFlightConstruction || hasWrapJournal`, so a
publication queued with none of those never started (the plain stop → play-again case). Fix (`31933183`):
`Loop::requestCommittedPassesRepublish()` republishes every active committed pass on the normal lane without
clearing the live root, called after both undo and redo of a record/overdub entry; the eligibility clause also
admits `pendingSnapshotSessionExitQueued()`. Device gate: `content-verify-65` clean base PASS and
`content-verify-65-playback` **`result: PASS`** with `storage_matches_effective: true` (384 + 157 + 35 == 576),
**all five cycles inventory-equal, exact order, 0 missing, 0 extra**, `max_bar_refs: 72`; same run 0
`companion path stalled`, 0 `RING,overflow`, 0 `FATAL`, 0 `MEM_DENIED`, save `ok`; firmware `f82ee9b9…`, git
`31933183` clean. Full native 2,239 / 1 skipped / 0 failures; RAM1 locals 6,400. Owed: the native redo-requeue
fixture and a load-time reconcile for already-saved stale roots (the fix applies to new activity).

**2026-09-18 successor 5.9 device run FAILS: a committed overdub pass never reaches the MIDI output
(OpenSpec `successor-capture-reentry-unguarding`).** The DEC-071 multi-pass shape was created on the
flashed tree (`captures/content-verify-58`, git `4fde5c03` clean, firmware `d298f9ea…`, `teensy41-capture-serial`): the base run
is a clean **PASS** — 9-bar seal for the 8-bar request, `ST PLAYING->OVERDUBBING: 2`,
`OVERDUBBING->PLAYING: 2`, `stored overdub span count=64/63 count_ok=True`, display `overdub_ok=True
transport_ok=True`, `persistence handoff result=ok`, undo=1 redo=1, `overflow_free=True` — so four of
5.9's five checks hold in one clean run. The fifth fails:
`captures/content-verify-58-playback/content-review.json` reports `storage_matches_effective: true`
(record 384 + overdub 157 + 35 + 24 = effective 600) with **every cycle `inventory_equal: false`,
`rows=572`, 28 missing (27 unexplained + 1 residual)**. The loss is deterministic and device-side: the
**whole second-overdub pass `overdub,2`** (24 rows: NoteOn/NoteOff notes 12–19 at ticks 4975–6503, CC74
0–63) and **3 rows of the first overdub's wrap continuation** (`(144,4,28,98)`@6911, `(128,4,28,0)`@95,
`(176,4,74,84)`) are in `passes` and in `effective` but never emitted. This is the same class as DEC-071 —
a committed pass missing from the playback source — with a different pass; DEC-071's own pre-wrap pass is
audible here. The same 24-row loss reproduced on `content-verify-53-playback`. **5.9 and 5.11 stay open**;
the next step is a fix plus a re-verification, not a tick.

**Root cause localised 2026-09-18 (two independent device resets): the persisted representation can lag
the persisted passes, and playback follows it.** `content-verify-59` (acceptance build, single-overdub
DEC-071 shape) is a clean base PASS, yet playback emits only the record pass: `raw_counts` record 384 +
overdub 157 + 35 = effective 576, `rows=384`, **192 missing every cycle**, reproduced identically by a
second reset+dump (`content-verify-59b-playback`). `src/StorageLoopIo.cpp` `writeLoopPersisted` serializes
the loop body's note/event snapshot from `loop.publishedSnapshot()` without requiring it to be current
with the committed passes, and `applyLoadedLoopSlotBodyCommit` installs it as the published root
(`storePublishedSnapshot(loadedLoopSnapshot, 0)`) with no reconcile against the restored passes; after a
reboot there are no unpublished passes for the stale-root overlay to fold in, so the committed overdub
stays silent while present in storage/effective. **Correction from the healthy-ring telemetry
(`content-verify-59`):** `PERS,request` at 78.6 s is ~10 s after the last publish (68.6 s), so the save did
not overtake the publication; and a stale root would have written a legacy body (no root on load ⇒
silence), not record-only playback. The persisted body was LSV1 with a root that was current at save but
already missing the overdub, so the prime suspect moved to the **publisher's candidate completeness**; all
guards in that area (`f139b2a7`, `e87272a8`, `4fcfbcb3`, `2b948d2a`) are still present, and nothing in the
save/snapshot/playback path changed since the last known-good device run except 2.4 slices 1–2. Fix
options (owner decision — persistence/publication contract): gate the persist admission on the root being
current (with a bounded re-admit), serialize the body from the passes, reconcile at load, or harden the
publication candidate's coverage.

**ROOT CAUSE LOCALISED 2026-09-18 (live capture, no reset): the global REDO of an overdub entry restores the passes but not the published root, so the redone overdub is permanently inaudible.** A MIDI-only capture (no serial open, so no reset: `scripts/hitl/live_playback_capture.py`) proves the loss is *live*, not save/load -- the live capture has 0 rows for the overdub notes (24-39) exactly like the post-reset one. The live gather telemetry pins the moment: `playback_build` eventCount 384 (rev 1/2) -> 576 (rev 3, after the session-exit publish at 68.85 s) -> **384 (rev 4) after the `Undo` at 78.53 s** -> **still 384 (overlay, rev 5) after the `Redo` at 81.67 s**, while the dump's `passes`/`effective` keep 576 the whole time. Code: `TrackUndo::applyPublishedSnapshotSelect` (`src/TrackUndo.cpp:148`) selects `undo ? beforePublishedSnapshot : afterPublishedSnapshot` and is a **no-op when the redo's `afterPublishedSnapshot` is empty**; the redo's pass restore bumps `playbackRevision` afterwards, which makes the pre-undo root stale, and the stale-root overlay has no unpublished receipt left to fold the redone passes in. The application order is correct (root selected after the restore) and the attach machinery from `8aa29307` is all present -- registration (`ensureSessionExitPublishedUndoAttachRegistered`), handler (`TrackUndo::attachAfterPublishedSnapshot`, which only accepts `RecordPassAdded`/`OverdubPassAdded` with a matching id and loopId) and the publisher's call at `LoopSnapshotPublish.cpp:2194`. Nothing guarantees the after-root is captured before an undo can consume the entry, and nothing republishes on a redo that lacks it -- that hole is the defect. Next: decide the fix (capture/attach the after-root before the entry can be undone, or republish on a redo whose after-root is missing) and re-verify the DEC-071 shape with undo/redo in the recipe.

**Observability fix landed alongside:** `SESSION_CAPTURE_DROID_QUEUE=0` folded into the acceptance env
`teensy41-capture-serial-no-grain`. `DIAG,droid_queue` was ~5.6k rows/run and, with the per-slice
`PERS,work` trace, overflowed the 1 MiB ring (four runs came back `INCONCLUSIVE_CAPTURE` after the
harness failed to deliver the overdub-stop press). With it dropped, the DEC-071 run fell from 20,800 to
**9,237** `#CAP` lines and became a clean PASS, so the base-run oracle is trustworthy again. RAM1 locals
unchanged at 6,400 for both envs.

**2026-09-17 successor change task 5.5 CLOSED: the adversarial mid-grain reentry checkpoint set is
complete and in the default gate (OpenSpec `successor-capture-reentry-unguarding`).** 5.5 was the only
remaining host-only item of that change, the one DEC-072 / D8 directly unblocked, and the one still
hiding coverage behind `REENTRY_RED=1`. The missing "after the raw-range stage" checkpoint is added at
`CanonicalRewrite` (raw range sealed, canonical order built, canonical copy being rewritten, no staged
pass yet), and one further checkpoint is added beyond the named set because the phase enum is **not**
execution order: `PrepareTransfer` dispatches before `CanonicalTail` and its grain also stages the pass
and takes the receipt, so that state is expressed as *the turn the job first owns its staged pass*
(`test_adversarial_reentry_at_the_staged_pass_ownership_checkpoint`) — exactly the state a successor's
`beginCapture` used to destroy. The `CompanionHistory` case is promoted into the default suite, so
`REENTRY_RED` is gone (`ONLY_COMPANION=1` remains the focused single-case runner). The checkpoints are
tight: each phase checkpoint is asserted by **equality**, and the ownership checkpoint asserts it pauses
on `CanonicalTail`. Five checkpoints in total — `CanonicalOrder`, `CanonicalRewrite`,
`CompanionHistory`, staged-pass ownership, `AtomicTransfer`. Evidence: `test_continuous_capture` 74/74,
`ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native **2,239 / 1 skipped / 0 failures**,
`teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488 / **locals
6,400** (test-only increment), both `openspec validate --strict` checks valid. Remaining in that change:
5.9 (the `hitl-content-verification` audibility proof), 5.11 on that run, 6.2 (a device session), then
the 6.1 re-run and 6.3 closeout.

**2026-09-17 task 2.5's real-gather parity fixtures landed (OpenSpec
`resumable-midi-boundary-playback-maintenance`).** The 2.5a harness could compare the immutable
`PreparedPlaybackPair` against the synchronous merged/order path only over a synthetic window, so the
2.4 slice-2 conversion was not yet validated through the production publication pipeline. It is now:
`buildFixtureRoot` in `test_continuous_capture` builds a published snapshot root carrying both
independent snapshot roots — note membership and the MIDI-event membership the playback gather reads —
mirroring the publisher (an Off at or beyond the loop length clamps to `loopLength - 1`; a wrap note
splits into head+tail with the same `NoteId`), and `assertPairMatchesSynchronousPathFromAnchor` anchors
both consumers at the pair's own `cursorSeed`, so the comparison is about the pair rather than about
where each cursor started. Four owner-priority fixtures landed: wrap + loop-edge note, source change
mid-window (hold, 8-tick publish latency, the added row audible), the long-loop window (the documented
`pairApplicable == false` boundary, plus the windowed stream against an independently phase-sorted
full-gather window), and the record-stop publication gap (an empty gather publishes no pair; once the
session-exit root lands the pair matches the synchronous path). Evidence: `test_continuous_capture`
71/71, `REENTRY_RED=1` 72/72, `ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native
**2,236 / 1 skipped / 0 failures**, `teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 /
code 425,496 / padding 488 / **locals 6,400** (test-only increment; no firmware change). What remains of
2.5 is the view/bridge-path matrix (record stop, phase-zero wrap admission, pre-seal invalidation,
rejected-On/later-Off, accepted-On/matching-Off, wrap-held closure, append-and-stop, the explicit
deferred-stop trace, promotion before wrap, max phase advance, denied refill, three-buffer rotation,
source-change preemption near handoff, source prep midway through two windows, second-source bridge
replacement, coverage fault, layered slots, slot switch, first/second revision changes). 2.4 is
code-complete and open only for the 5.3 device read of the pair counters.

**2026-09-17 task 2.4 slice 2 landed: an immutable `PreparedPlaybackPair` with atomic publication, and
the measurement decides against slice 3 (OpenSpec `resumable-midi-boundary-playback-maintenance`).**
The pinned invariant — *playback never observes `mergedEvents` and `playbackOrder` from different
generations; a pair becomes consumable only when both members and their cursor/reanchor state are
complete for the same source generation* — is now structural: `PreparedPlaybackPair`
(`include/TrackPlaybackRuntime.h`) carries the generation, the live source identity, the full-loop
coverage, a pointer to a dedicated prewarmed `FixedPlaybackEventStorage`, the event count and the
`cursorSeed` resolved against the publication phase, and `LoopPlaybackRuntime::publishPreparedPlaybackPair`
replaces the whole descriptor in one assignment inside a non-yielding turn. The off-clock owner
(`Track::publishPreparedPlaybackPairFromWorkingWindow` / `Track::servicePendingPreparedPlaybackPair`,
serviced one preparation per turn from `runDeferredLoadAndDisplayFrame`) is the only publisher;
`resolvePreparedPairConsume` is the clock path's entire decision, so `playCommittedLoopMidi` no longer
runs `::rebuildPlaybackOrder` / `reanchorPlaybackIndex` for the full-loop merged path. A source change
(new `playbackRevision`, new snapshot identity, or the one-shot overdub-seal receipt exposed through
`Loop::hasPendingOverdubSealAdmission()`) records exactly one request and the tick keeps consuming the
published P0. Measurement (native): `prepared_pair_hold_ticks` **8** and
`prepared_pair_max_latency_ticks` **7** — one MIDI clock interval, ~1% of the 768-tick one-bar budget
and far inside the next 16th-note (48-tick) audibility edge — with `playbackOrderHotRebuildCount` 0
across the decision loop and 1 for the off-clock owner's replacement order. **Slice 3 (b), a bounded
incremental order update, is therefore not required for 2.4**; `prepared_pair_rebuild_units` records the
one per-source-change build as its own follow-up. Evidence: `test_continuous_capture` 67/67 (four new
slice-2 fixtures), `REENTRY_RED=1` 68/68, `ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native
**2,232 / 1 skipped / 0 failures**, RAM1 variables 91,904 / code 425,496
/ padding 488 / **locals 6,400** (the new off-clock helpers are FLASH-resident; ITCM is one 32 KB
FlexRAM bank from the edge). 2.4 ticks on the 5.3 device measurement of the same counters; 5.4 stays
blocked on 2.5/5.3.

**2026-09-17 interval-owned staged capture pass (DEC-072; OpenSpec 5.12 / D8 landed and
device-verified).** The 5.5 `CompanionHistory` blocker was one `Loop` scalar
(`pendingCapturePass_`) serving two capture intervals: a deferred predecessor staged its pass at
`PrepareTransfer`, a successor's `beginCapture` discarded it unconditionally, the predecessor's
`beginPreparedCompanionHistory` was refused forever by its own precondition, and its job held
`captureStopOwner` and the frozen role for the rest of the session. Guarding the discard only
preserved the successor's `AlreadyPending` collision, so ownership moved instead: the job stages into
the Loop slot and immediately takes the receipt into `CaptureStopJob::pendingPass` in the same
non-yielding grain, the companion history binds that receipt by address, and the job's
`AtomicTransfer` consumes it directly through `Loop::transferCapturePass`; every deferred reader
(undo identity, bridge-refresh, companion verification, stop telemetry/accounting) resolves the
receipt through the job with no fallback. `beginCapture` keeps its unconditional producer-local
clear. Evidence: `ONLY_COMPANION=1 pio test -e native -f test_continuous_capture` green (the former
red reproducer), `REENTRY_RED=1` 57/57, full native 2,221 / 1 skipped / 0 failures, RAM1 locals
6,400 (the ring constructor had to become FLASH-resident: `CaptureStopJob` now owns a
`PendingCapturePass`, and the implicit aggregate constructor cost one 32 KB FlexRAM bank in ITCM);
device [`captures/successor-ownership-06`](../captures/successor-ownership-06/session_20260917_230432.log)
managed HITL `base`, `Result: PASS`, `overlap=True denied_stop=True retirement_ok=true` (A retires
inside B's live interval; B then stages and transfers its own pass). The host probe oracle now also
accepts the current `Overdub stop refused: stop_job_pending` denial wording. OpenSpec 5.5 remains
open for its own parameterized checkpoint coverage and gate promotion; the change's remaining pick-up order (5.5 first, then one 5.9/5.11/6.2 device session, then 6.1 and 6.3) is recorded at the top of its `tasks.md`.

**2026-09-17 transport-stop retarget re-route (DEC-070).** The post-HITL stuck state machine was a
deferred stop job admitted for PLAYING, retargeted to Stopped and left on the PLAYING phase path; its
companion work is bound to the transport epoch the stop invalidates, so it parked in
`CompanionHistory` and held the slot's finalization resources, refusing every later stop until a
reboot. The retarget now re-routes the job onto the STOPPED continuation
(`retireSkippedCompanionPreparation` -> `AtomicTransfer` -> deferred cleanup). Native case
`test_transport_stop_retarget_retires_a_midflight_playing_job` fails without it (full native 2,213 /
1 skipped / 0 failures; RAM1 locals 6,400); device `captures/successor-wrap-40` shows the retargeted
job retiring 2.2 s after the teardown transport stop and the slot staying usable across repeated
enter/stop cycles. openspec 5.3/5.7/5.8 closed. DEC-070 follow-up: a retargeted job now keeps and publishes its companion (note-edit) horizon instead of dropping it (bounded 512-grain per-phase fallback to the raw pass if it cannot complete), which restores the overdubbed notes to the display (`captures/successor-wrap-42` reports `overdub_ok=True`; `transport_ok=False` there is inconclusive because that run overflowed the capture ring — openspec 5.9). Diagnostic instrumentation for stall reasons stays in place.


**2026-09-17 the wrap owns the slot's canonical revision too (DEC-069; hardening landed, gate
open).** A manual three-overdub device session
([`captures/session_20260917_172651.log`](../../captures/session_20260917_172651.log)) shows the
second interval's deferred job never retiring once the third interval's **wrap** commits a pass:
the commit advances `Loop::playbackRevision`, which invalidates the pending job's
`RuntimeCanonicalPassInputs` frozen source (pinned to its admission revision), so its
`CanonicalTail` phase denies forever, its finalization resources are never released, and the third
interval's own stop is refused as occupied for the rest of the session; the third interval's queued
wrap publication stays behind the unresolved predecessor publication, so its notes neither show nor
record at the wrap. DEC-067's rule — a resource blocks the transition that first requires it —
therefore applies to the wrap as it already does to start and stop. Landed: the wrap no longer
resets a pending predecessor's frozen canonical source or its frozen order/rewrite builders (two
new native cases; full native 2211 / 1 skipped / 0 failures; RAM1 locals 6464; firmware builds).
DEC-069 Option A is now implemented (owner's choice): the crossing is recorded as an owed commit
and sealed on the first turn after the job retires, so the revision the job pinned never moves; the
held note's reservation is never reset, which removes the live interval's `unmatched NoteOff`
rejections as well. Three native cases (two fail without the gate); full native 2,212 / 1 skipped /
0 failures; RAM1 locals 6,464. Device acceptance closed (`captures/successor-wrap-36`): the successor's deferred crossing
committed after the predecessor retired, with no `unmatched NoteOff`; `captures/successor-wrap-33` is
the matching DEC-068 regression PASS. The reproducer's temporary successor-stream flag is not yet
promoted to a registered stimulus variant.


**2026-09-17 reentry session ownership (DEC-068).** Tasks 5.1(d)/(e) landed and immediately
exposed a job-locality defect: the retiring predecessor's cleanup built its session-exit receipt
from the shared session state (the successor's), closed the live successor's session, and left the
successor's own job stuck in `DeferredCleanup` with its publication unqueued. Each interval now
owns its session-exit tail — captured at its own stop admission (swap, not copy), released with its
own pass id at its own commit, consumed ready-FIFO by the publisher — a successor opens a fresh
session instead of joining the draining one, and the cleanup ends the live session only when the
retiring job still owns it. Device `captures/successor-reentry-27` is the first clean reentry run:
B START 1,915 ms before A's release, B's stop refused, `Reentry released`, B's stop admitted, **B's
own retirement** at 52.992 s, run result **PASS** (`overlap_ok`/`denied_stop_ok`/`retirement_ok`).
Six native cases cover the gates; full native 2209, 1 skipped, 0 failures; RAM1 locals 6464.
Next in the change: 4.2/4.3 three-route agreement, then 5.3-5.6, then the shed-capture
observability item before final 6.2 closeout.

**2026-09-17 first-record prepared bar-1 launch + successor-reentry finalization admission
(DEC-067).** Two user-directed RCs landed. (1) The record stop now *uses* the prepared bar-1
window: a take that ended without the handoff no longer blocks the next take's preparation
(`b6dd67bf`), the bound window is preserved across the launch's playback-cache resets and
adopted as the audible source while the first root publishes (`a49bb01a`), and the adoption
selects the launch cursor so the already-played prefix is not replayed as a catch-up burst
(`a20eba50`). Device `captures/first-record-launch-04` (canonical `base`, PASS): first note
93 ms after the stop/PLAYING edge and the following note-ons on the 16th grid
(125.0/125.0/125.0/122.4 ms) where `deadline-pacing-01` measured 620 ms and a burst. Still open
there: S2b (the window's retained extent can lag the last accepted input, and the bar-1
closure obligations are not carried yet) and S3 (continuation past bar 1). (2) The
successor-reentry device case exposed that `Track::startOverdubbing` gated on three
*finalization* resources; per the user's decision the gates were moved to the transition that
first needs them — STOP — under DEC-067 (`758c89b0`), the successor stop was kept out of the
legacy fallback when those resources are busy (`d9d641da`), and two deterministic edges
(`Reentry window open slot=<n>`, `Reentry released`) now drive the HITL probe (`8dc92a5e`,
`7c3e1c20`, `96752f86`). Device `captures/successor-reentry-25`: B START 583 ms after A's stop
and 2,209 ms before A's finalization released, B's stop refused while A owned those resources,
then admitted after the release — the first time the declared depth-2 capacity is observable on
hardware. Gates: full native 2203 cases, 1 skipped, 0 failures; RAM1 code 425576 / padding 408
/ locals 6464.

**2026-09-16 task 2.3 causal predecessor/source identity — increment B, approved
superseded rule (resumable-midi-boundary-playback-maintenance).** Option (A) is
implemented and verified. A deferred stop whose recorded `causalBase` is no longer the
live published root retires without publishing a successor window: the staged accepted
capture pass is committed through
`TrackPreparedStopCommit::commitSupersededCausalBase`, no bridge is selected or
published (`causal_base_superseded`), a prepared protected undo entry is published in
the same turn, and the unpublished companion horizon returns to `Prewarmed` through the
new allocation-free `Loop::restoreSupersededPreparedCompanionHistory(owner)` (with
discard-and-reprewarm as fallback). The checks sit at the start of `CompanionBridge`
and `AtomicTransfer`, with `PendingStopAdmission::retireSupersededPreparation()`
retiring the ranked/canonical/undo-collection credits so the job retires instead of
stalling. Implementation finding recorded: resolution must stay live — the canonical
owner identity is compared by address against the live Loop scalars, so the recorded
identity is the supersession reference, not the resolver, and the live revision/root
were measured unchanged during a normal finalization. Full native 2,191 cases — 2,186
passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Recorded deviation: the
superseded route publishes the capture pass only, relying on the newer root to already
carry the companion rows (to be re-verified when successor capture is unguarded).
Remaining in 2.3: successor-capture unguarding, in a separate change. Task 2.3 remains
unchecked.

**2026-09-16 task 2.3 causal predecessor/source identity — increment A
(resumable-midi-boundary-playback-maintenance).** Auditing task 2.3(f) found that a
deferred stop re-derives its predecessor source live at every consumer turn: admission
computes a `PlaybackViewSourceIdentity baseSource` for credit sizing and discards it, the
worker recomputes `baseSource`/`targetSource`/`frozenSource`/`currentSource` when the grain
runs, and the atomic commit recomputes `base` from the live `playbackRevision` and
`publishedSnapshotIdentity()`. A successor publication during finalization could therefore
change what the predecessor resolved against. `CaptureStopJob` now records `causalBase`,
pins its root with `causalBaseSnapshot` (`ImmutableLoopSnapshotRef`) and sets
`causalBaseRecorded`; admission resolves and pins it before the raw-range ownership transfer
and denies `causal_base_unavailable` if a non-null published identity cannot be pinned, and
retirement clears all three. Increment A records only, so behavior is unchanged; increment B
(next) makes the worker and the atomic commit resolve against the recorded identity and
decides the epoch-qualified publication rule including STOPPED recovery. New native
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191 cases
— 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 prepared companion history interval qualification
(resumable-midi-boundary-playback-maintenance).** The Loop-owned prepared companion
history (`preparedCompanionHistory_`) is borrowed worker scratch that outlives one
grain, so a successor interval could previously observe, advance, commit, recycle or
discard a predecessor's in-flight preparation. Every consumer now requires the owning
interval identity, `PendingStopAdmission::intervalToken()` (frozenGeneration +
stopTick) — a value, not an object address, so a copy of an admission made for credit
probing or retargeting still identifies the same interval while a reused ring
descriptor gets a fresh token; zero means the live session path.
`preparedCompanionHistoryOwnedBy()` keeps Cold/Prewarmed receipt-free and
owner-agnostic; `beginPreparedCompanionHistory` records the token and recycle
completion plus `discardPreparedCompanionHistory` clear it. The deferred stop worker,
the stop-commit helpers and `beginCollectOverdubStopUndoPasses` pass
`stop.intervalToken()`; default arguments keep the live path source-compatible. New
native `test_prepared_companion_history_is_job_qualified` covers successor and
live-session refusals, stale discard, receipt ownership and reuse after recycling;
the two Track-level `test_playback_prewarm` fixtures now begin the preparation with
the admission that consumes it, and their stale unqualified consumers were qualified
(rather than deleted) because the qualification is what makes those calls correctly
answer "not mine". This closes the session/preview/undo/snapshot receipt slice. Full
native 2,190 cases — 2,185 passed, 1 skipped, the same three intentional
public-reentry failures; `teensy41-capture-serial` builds with 6,752 bytes RAM1
headroom. Remaining in 2.3: causal predecessor/source identity per job, then
successor-capture unguarding in a separate change. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 session-exit handoff receipt ring
(resumable-midi-boundary-playback-maintenance).** The session-exit
`SessionExitSnapshotHandoff` receipt is now a Loop-owned FIFO ring of
`kSessionExitReceiptCapacity = 4` (external-memory-first, cold-prewarmed by
`Loop::prewarmSessionExitSnapshotHandoff()`), replacing the single-slot unique
pointer. This closes "Finding 2" of the deferred-stop receipt audit: the receipt
outlives the stop job, so it cannot be job-owned. `canHandoffSessionExitSnapshot()`
is now a **free-slot** gate; "one receipt per open session" is enforced inside
`handoffSessionExitSnapshot()` by `sessionExitHandedOff_` (set on handoff, cleared by
`openOverdubSession()`), so a successor session is never serialized behind an
unconsumed publisher turn while a repeat handoff for the same session is refused. The
idle publisher consumes the oldest receipt one per turn (retaining it when
construction is disallowed), and playback's stale-root overlay folds every pending
receipt (passes and companion ids) rather than one. New native
`test_session_exit_receipt_ring_is_fifo_and_not_serialized`; the stale
"unconsumed receipt closes the gate" assertions in `test_playback_prewarm` and
`test_loop_take_survival` were rewritten to the ring contract. Native:
`test_playback_prewarm` 165/165, full suite 2,189 cases — 2,184 passed, 1 skipped, the
same three intentional public-reentry failures; capture-serial builds with 6,752
bytes RAM1 headroom (13 banks, code 425,208). No new boot allocation, so the increment
A/B device memory check (build `91e6b38c…`) still covers this slice. Remaining in 2.3:
prepared-companion history qualification, causal predecessor/source identity, then
successor-capture unguarding in a separate change. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 increments A/B boot abort — ROOT-CAUSED, FIXED, device-verified.**
The memory check found the increment A/B firmware never reached a running sketch
(`a7f10733…` emitted only `boot_reset,crash=1` before the USB port dropped; the
32-entry-prewarm variant `8c03dcfc…` behaved the same, so the undo-stack prewarm was
not the cause). A hardware bisect isolated it to **`0f398715` (increment A)**:
`7a275d1f` boots (21386 bytes, crash=0), `0f398715` and `162df28f` do not (0 bytes).
Root cause: `prewarmCaptureStopJobs` reserved `kRecordsPerBridge` (2048) rows for each
job's `undoPassIds`/`undoCompanionIds`, and both are
`InternalHeapFirstAllocator` vectors — 4 ring entries × 8 tracks × 2 lists × 2048 × 4 B
= **512 KB demanded from a ~340 KB internal heap at boot**. Fixed by bounding that
reserve to `kUndoCollectionReserveRows = 64` (16 KB total); a session longer than that
publishes no prepared undo entry and takes the plain commit. The fixed tip is flashed
and verified: build `91e6b38c…`, 22091 bytes streamed, 0 warnings, prewarm samples
`PSRAM 5,141,952 -> 3,775,872`, `heap 352,256 -> 290,816`, chunks `512 -> 416` — about
79 KB PSRAM and 20 KB internal heap over the reference build
(`captures/task23-undo-prewarm-fix/result.json`). Native: 2188 cases, 2183 passed, one
skipped, the same three reentry failures; build 6752 bytes RAM1 headroom.

**2026-09-16 task 2.3 deferred protected undo publication wired
(resumable-midi-boundary-playback-maintenance):** the normal deferred overdub stop
now records its `OverdubPassAdded` entry. `TrackUndoPublication` is externally
allocated and cold-prewarmed by `prewarmProtectedUndo()`, which also reserves the
undo stack's entry capacity (`Config::ABSOLUTE_MAX_UNDO_ENTRIES`, 512) — a
publication precondition whose device PSRAM admission must be measured.
`stepDeferredUndoPublication` runs `begin` + bounded `stepPrepare` inside the
existing `UndoCollection` phase; the atomic transfer proves `canPublish(...)` then
calls `advanceAtomicTransferWithUndo`, else plain-commits with the transaction
restored. `stepProtectedUndoDrain` drains one bounded quantum per DeferredCleanup
turn (`ReleaseRedo`/`Trim`/`Complete`, plus restore + `recoverPreparedEntry` for an
unpublished transaction), so `protectedState()` is Idle before the job retires.
Tests assert one published `OverdubPassAdded` and an Idle transaction; full native
2,188 cases — 2,183 passed, one skipped, the same three known reentry failures;
capture-serial builds with 6,752 bytes RAM1 headroom. Known deviations: the DEC-063
disabled-pass reclaim continuation is unwired, and STOPPED/empty routes publish no
protected entry. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 deferred undo collection, job-owned prepared entry
(resumable-midi-boundary-playback-maintenance):** a new
`PendingStopPhase::UndoCollection` on the prepared PLAYING route collects the
session's undo identities in bounded grains and builds the `OverdubPassAdded` entry
from the job's own slot and baseline via `TrackUndo::prepareOverdubSessionEntry`.
The collection has its own admission grain class
(`remainingUndoCollectionGrains`/`undoCollectionQuantum`, sized from
`Loop::requiredSessionStopUndoCollectionGrains`, included in
`finalizationComplete()`), so saturation denies admission instead of stalling. The
STOPPED/empty routes skip companion preparation and never enter the phase. Private
outputs are pre-reserved in `prewarmCaptureStopJobs`; `Loop::reclaimUnreferencedDisabledEditPasses`
moved to flash to hold 13 ITCM banks. New native
`test_deferred_stop_prepares_job_owned_undo_entry`; full native 2,188 cases — 2,183
passed, one skipped, the same three known reentry failures; capture-serial builds
with 6,752 bytes RAM1 headroom. Increment B remains: publish through
`TrackUndoPublication` + `commitWithUndo`/`commitStoppedWithUndo` and drain its
`ReleaseRedo → AwaitReclaim → Trim → Complete` phases in DeferredCleanup. Task 2.3
remains unchecked.

**2026-09-16 task 2.3 deferred-stop receipt audit — design decision required
(resumable-midi-boundary-playback-maintenance):** the last two receipt items need a
decision before implementation. The normal deferred overdub stop publishes **no**
`OverdubPassAdded` entry — `pushOverdubPassAdded` runs only from the fault-fallback
`commitCaptureForStop` via `finalizeCommitSideEffects`, while the deferred path calls
`TrackPreparedStopCommit::commit` with `undo = nullptr` — so the job-owned undo
baseline is correct preparation but currently unreachable in production; wiring the
protected undo publication (the prewarmed-but-unused resumable
`*CollectOverdubSessionUndoPasses` machinery plus `TrackUndoPublication`) is what
makes it load-bearing. The session-exit receipt cannot be job-owned because it is
retained past the job and consumed later by the idle publisher while playback reads
it for the stale-root overlay; it needs a Loop-owned multi-slot ring with the gate
testing a free slot. Ring size and multi-pending overlay behaviour are the open
questions. No code changed for these two items. See CONTINUOUS-CAPTURE-OWNERSHIP.md.

**2026-09-16 task 2.3 job-owned overdub-session undo baseline (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns the overdub-session undo baseline
(`ImmutableLoopSnapshotRef beforePublished` / `hasBeforePublished`). Admission moves
the live `Track::overdubSessionBeforePublished_` into the job and clears the live
member, so a successor session's `beginOverdubSession` records its own baseline
instead of overwriting the one this interval's `OverdubPassAdded` entry must record;
`TrackUndo::pushOverdubPassAdded` prefers the head job's baseline and an unconsumed
baseline is retired with the job. New native
`test_stop_job_owns_overdub_session_undo_baseline`; `test_continuous_capture` 29 pass
with the same three intentional reentry failures; full native 2,187 cases — 2,182
passed, one skipped, the same three; capture-serial builds with 6,752 bytes RAM1
headroom. Still open in this slice: the session-exit snapshot handoff receipt and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 job-owned capture preview (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns `CapturePreview preview`, transferred from the live session
at admission by constant work (`CapturePreview::swapWith`, per-vector swap, no
copy/allocation) with a job-identified read borrow, so the deferred cleanup seals
this interval's preview rather than a successor's. `Loop::capturePreviewView()` is
the single readable accessor and is out of line in flash (inlining it at 40+ display
call sites cost a RAM1 bank); 45 read-only display/Edit sites were migrated and
writers keep the member. `TrackPreparedStopCommit::completeDeferredCleanup` takes the
job's owned preview. New native `test_stop_job_owns_capture_preview` and
`test_capture_preview_borrow_release_is_owner_identified`; `test_continuous_capture`
28 pass with the same three intentional reentry failures; full native 2,186 cases —
2,181 passed, one skipped, the same three; capture-serial builds with 6,752 bytes
RAM1 headroom (13 ITCM banks). Still open in this slice: the overdub session/undo
before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 job-owned pending note changes (resumable-midi-boundary-playback-maintenance):**
`CaptureStopJob` now owns `PendingNoteChangeVec pendingChanges`; admission swaps the
live Add/Shorten/Hide set into the job in constant work and leaves the live session a
prewarmed empty set, so the worker no longer resolves the canonical tail against the
live session's intents and a successor interval is independent. `Loop::pendingNoteChanges()`
returns the job's owned set while a job-identified borrow is bound (writers use the live
member), and the borrow is released with the job. New native
`test_stop_job_owns_pending_note_changes` and
`test_pending_change_borrow_release_is_owner_identified`; `test_continuous_capture` 26
pass with the same three intentional reentry failures; full native 2,184 cases — 2,179
passed, one skipped, the same three; capture-serial builds with 6,752 bytes RAM1
headroom. Still open in this slice: `capturePreview` ownership across the stop, the
overdub session/undo before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 sealed raw-range ownership wired (resumable-midi-boundary-playback-maintenance):**
The approved ownership transfer is implemented. `CaptureStopJob` owns
`LoopEventStore::CaptureRange rawRange`; `beginDeferredOverdubStop` proves
`canSealCaptureRange` before any mutation, seals the producer extent at punch-out
and binds a job-identified read borrow on `Loop`. A denied admission performs no
seal and no transfer; an empty capture skips the seal. `Loop::captureReadSource()`
exposes producer and finalizing extents separately and ordering is scratch-only, so
the borrowed range is never mutated; closure NoteId resolves from the finalizing
extent first. `Track::retireJobCaptureSourceGrain` retires the owned extent in
grains, drops the borrow only while it still identifies that job, and re-arms the
descriptor's reserved destination. DisplayManager is unchanged; the
successor-capture guard stays deliberately in place. Five new native regressions
plus three migrated ownership assertions; `test_continuous_capture` 24 pass with the
same three intentional reentry failures; full native 2,182 cases — 2,177 passed,
one skipped, the same three; capture-serial builds with 6,752 bytes RAM1 headroom
(new functions are FLASHMEM and `Loop::beginCapture`/`discardCapture` moved to flash
to hold 13 ITCM banks). Device boot prewarm admission **PASS** on this build
(`41b30d18…`, `captures/task23-sealed-range-prewarm/result.json`): PSRAM free
5,142,912 -> 3,856,320 B (160,824 B/slot, unchanged), internal heap 352,256 ->
311,296 B, pool chunks 512 -> 416 (+4/slot = the four range destinations per track),
all slots `ready=1` with 8 credits and zero warnings. Task 2.3 remains
unchecked; per-job session/preview/undo/snapshot receipts, causal predecessor/source
identity and unguarding successor capture remain open.

**2026-09-16 task 2.3 FIFO/queue-head binding (resumable-midi-boundary-playback-maintenance):**
The stop-descriptor ring is now the explicit admission authority —
`Track::captureStopJobTail()` admits (null when full, `queue_full` stop stage),
`captureStopJobHead()` is the worker's stable per-grain descriptor, and
`!STOP_STATUS` reports `queue=depth/capacity`. Behaviour is unchanged at one job
(tail == head). Two new native regressions; full native 2,178 cases — 2,173
passed, one skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom.

**Sealed raw-range wiring is audited but not wired:** during the deferred stop
`Loop::capture.store` is still read (and sorted in place) by the Loop
display-window merge helpers and scanned by `appendCaptureNoteOffAtPhase` for
closure note identity, and `Loop::beginCapture` frees a pre-reserved tail. Sealing
at punch-out therefore needs a Loop-internal producer-or-sealed read fallback; the
recommended design and the rejected lazy-seal alternative are recorded in
CONTINUOUS-CAPTURE-OWNERSHIP.md. It moves capture-interval ownership between `Loop`
and the admitted Track job, so the architecture ownership-transfer protocol
applies — approval required before implementation. Task 2.3 remains unchecked.

**2026-09-16 task 2.3 ingress/staging isolation (resumable-midi-boundary-playback-maintenance):**
`RuntimePlaybackViewSet::captureStaging` is split into two independently prewarmed
roles in the same owner: a *live* ingress staging and a *frozen* stop-job staging.
Live ingress writes only the live role; the one admitted stop job borrows the frozen
role. Punch-out freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`); job cleanup resets only its own
retired frozen role, so old-job cleanup cannot clear or alias a successor interval's
ingress rows. No new owner/module, no canonical/persistence/SD change, no action-edge
copy. New native regressions in `test_playback_prewarm` and `test_continuous_capture`
(production Button A route) pass; full native 2,176 cases — 2,171 passed, one skipped,
the same three intentional public-reentry failures; `teensy41-capture-serial` builds
with 6,752 bytes RAM1 local headroom, RAM2 free 472,928. Added cost is one
2,048-event staging buffer per fully-prewarmed runtime slot (now counted by
`fixedExternalBytes`); boot prewarm admission was measured on device and passes —
build `fb4ab3a9…` reports PSRAM free 5,143,968 → 3,859,680 B across the 8 boot
slots (160,536 B/slot, two stagings per slot, no internal-heap fallback), all
slots `ready=1` with 8 credits, zero warnings, and 46.1 % of the 8.38 MB pool still
free (`captures/task23-staging-prewarm/result.json`; the run used a capture-only
SESSION_CAPTURE boot line). The selected-slot full-prewarm plus dense loaded
content still needs its own footprint check before the guards come off. FIFO/queue-head
binding, sealed raw-range wiring, causal predecessor/source, per-job
session/preview/undo/snapshot receipts and the three reentry cases stay open; task
2.3 remains unchecked. DEC-064 follow-up recorded.

**2026-09-16 dense snapshot representability + fragment transaction (openspec 2.3 / immutable-loop-snapshot-passes 3.7):**
The dense record/overdub pair (`task23-dense-snapshot-cc-playback`, 734 effective
rows) was lossy in publication: bar 2 needs 260 event references, above the fixed
16-page (256-reference) per-bar membership ceiling. Construction failed at
`appendCurrentReference`, and because pass-change progress had already advanced the
fragment was discarded, so a 710-row truncated candidate was published. Two bugs:
representability (valid content exceeds the ceiling) and atomicity (failure consumed
progress despite DEC-047). Fix: explicit `kSnapshotMembershipMaxRefsPerBar = 4096`
design envelope with derived 256-page limit applied to event and note membership,
`uint16_t` page counters, and transactional fragment incorporation (authoritative
progress advances only after successful incorporation; partial candidate discarded on
failure; representability failure parks publication after two consecutive failures
with `DIAG,snapshot_fragment`). DEC-047 amended + ExecutionBounds rows 21/22 updated
2026-09-16. Focused `test_loop_take_survival` 132/132; full native 2166 passed / one
skipped / three known task-2.3 reentry failures; `teensy41-capture-serial` SUCCESS
with RAM1 locals 6752. **Hardware verification PASS for representability:** uploaded
build `5bb90b47bdae299d630be640…`, dense recipe stored 766 effective rows with bar 2
at 270 refs (>256) and four restart cycles played 765 rows with 0 duplicates; the
only absent row is the pre-existing terminal loop-boundary Off (`NoteId=0`). Ordering
parity and that terminal-Off ledger behavior remain separate open items.

**2026-09-16 latest checkpoint (14:29):** descriptor build a9a7217f uploaded on
retry; dense hardware test then lost USB during initial recording, before overdub.
No reboot marker or fault cause retained; serial port absent. Native full run has
2,159 pass / one skipped / three known reentry failures; final focused guard run
has 14 pass / the same three failures. Stored dense content verification on the
prior fixture passed its scoped input comparison; audible timing and restored
playback remain open. Device reconnection is required to continue hardware
investigation. No capture reader is running. See active FOCUSED-STOP-HITL.md.

**2026-09-16 dense verification / FIFO descriptor slice:** stored dense overdub
content matches input (568 events, three chunks, five expected short-pair removals).
Timing is NOT passed: a stop ordering grain crossed a service deadline and two
late clock samples are retained. Restored musical playback produced no target
channel rows in the probes; output policy/readiness remains under investigation.
Stable external Track descriptors replace the singleton stop receipt; sequential
rotation passes, but staging/session/predecessor isolation and three reentry tests
remain open. Full native: 2,159 pass, one skipped, three expected reentry failures;
final focused admission/rotation: 14 pass and the same three failures. Build passes
with 6,752 bytes RAM1 headroom. See active FOCUSED-STOP-HITL.md for evidence and
latest upload status; no task checkbox closes.

**2026-09-16 dense reboot fix verified:** matching fault report recovered into
crash-log-retrieval.log: stack-boundary DACCVIOL in _svfprintf_r. Overlap diagnostic
scratch moved to RAM2; local frame reduced 1,372 -> 76 bytes. Two dense automated
runs now complete without reboot; adjusted Stop-window run passes focused silence
(18.9 s observation). Broad baseline/content/timing checks remain inconclusive;
no FIFO/reentry/task-2.3 completion. Firmware hash and details are in active
FOCUSED-STOP-HITL.md. All serial/MIDI runners are closed.

**2026-09-16 automated dense rerun:** same track-5/slot-1 recipe reproduced the
reboot; corrected boot marker is crash=1. Serial missed the early fault text;
SD crashlog has not been read. Runner/capture finished. The dense gate remains
open; no manual reproduction is required. See task23-dense-crash-report-rerun
and active FOCUSED-STOP-HITL.md.

**2026-09-16 overlap reboot follow-up:** user confirms spontaneous reboot during
record-pass/overdub overlap. Native Shorten/stop regression passes; root cause
remains unresolved. Fixed destructive double-read of Teensy CrashReport: startup
now preserves the crash flag and renders once to Serial and SD. Build/upload pass,
with unchanged 5,600-byte RAM1 headroom. Human overlap reproduction on track 5 /
slot 1 is the next checkpoint; serial capture is in task23-overlap-crash-report.
See active FOCUSED-STOP-HITL.md for firmware hash and evidence limits.


**2026-09-16 device checkpoint (track 5 / slot 1, authorized):** verified firmware
uploaded. Two normal stop cleanups, focused transport-Stop silence, and three
restart content cycles pass. The denser run disconnected/rebooted before its stop
trace drained and is INCONCLUSIVE_CAPTURE; reset cause is unknown. No dense or
full task-5.3 acceptance. Updated host verifier recognizes pending-job completion
after visible PLAYING and passes 24 regression tests. Original reports and a
separate reevaluation are preserved under captures/task23-reset-*. See the active
FOCUSED-STOP-HITL.md checkpoint for exact build identity and limits. FIFO/reentry
implementation remains unfinished; no task checkbox closes.


**2026-09-16 pending-job reset protection / intermediate HITL checkpoint:**
An admitted stop job now retains its runtime staging and canonical-copy output
across cache reset. Reset retargets STOPPED and yields; Track invalidation preserves
the borrowed generation until terminal recovery. Native coverage exercises partial
copy and every stop phase, including held-note closure and receipt release.
A Tier-A prewarm record exposes output credits and pool/heap headroom. Verify
boot/load memory and single-job copy/Stop timing on hardware before expanding the
FIFO reservations: prior eager prewarm exhausted device PSRAM. This checkpoint is
not task-2.3 acceptance; the same three reentry fixtures remain red, and FIFO,
causal-source and preview/session/undo/snapshot migration remain unfinished.
No upload has been performed. Exact results and build identity are in the active
change's CONTINUOUS-CAPTURE-OWNERSHIP.md; the procedure is in FOCUSED-STOP-HITL.md.

**2026-09-16 task 2.3 worker-owner integration:** The existing stop worker now
uses a stopped-interval receipt for Loop/slot/phase/geometry and its own pending
notes. Closures target the saved owner; one-node retirement never clears live
notes, and completion cannot transition a newer Track interval. Production
empty, paired and held-note stops complete in native tests. Public A/B/C reentry
still fails: raw capture, staging, preview and undo/snapshot session migration
and the fixed FIFO remain open. Affected tests: 266 pass, the three known reentry
cases fail. Capture-serial builds with 5,600 bytes RAM1 local headroom; no upload.
Task 2.3 is not complete. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).



**2026-09-16 task 2.3: public-route reproducer and raw range foundation:**
The actual Button A → TrackManager → Track route and MidiHandler musical input
now run in a native fixture. First-interval capture/stop passes; A/B/C reentry,
manager same-pitch held-note reentry and direct Track reentry remain intentional
failing requirements. None of the three admission guards has been removed.

LoopEventStore now supports pre-reserved exclusive capture-range rotation,
read receipts, incremental release and stale rewrite/pool-lifetime rejection.
The focused range/persistence/event-store/playback-prewarm run passes 209 tests.
The full native run had 2,136 passes, one skip and only the three expected reentry
failures (before the final persistence-prewarm correction). This API
is not yet wired to Track; it does not fix immediate reentry. Job-owned staging,
closure credits, pending changes, undo/snapshot receipts, causal FIFO resolution,
cleanup isolation and device verification remain open. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).
Capture-serial build passes with 6,176 bytes RAM1 local-variable space after
placing the bounded rewrite maintenance worker in flash. Device timing remains
unverified. Task 2.3 stays unchecked; no firmware upload or archive was performed.

**2026-09-16 continuous preparation across punch-in/out:**
Canonical preparation now runs during PLAYING as well as OVERDUBBING, using the
existing VisualCache owner and otherwise-unused wrap-append turns. Existing
record, stop, wrap-append and source-span priority remains. A confirmed newest
pass append with unchanged companion history preserves completed normalized
passes and resumes the first unfinished/new pass; snapshot identity publication
at the same content revision also preserves completed normalization. Unproven
source changes or changed history retain the existing safe invalidation path.

The companion merge now retains final per-pass output, in its original order,
in the canonical owner's existing raw scratch buffer. One extra prewarmed pass
descriptor array is required (16 KiB at 2,048 entries on Teensy); no additional
event buffer or active allocation is introduced. Each existing merge addition
also writes its normalized scratch entry, within the same Q-entry grain.
Successful stop cleanup adopts these results with pointer swaps after matching
source/worker generations, including when the committed companions changed
history. Reselecting current descriptors/history is resumable; completed pass
normalization is not replayed. Selected bridge/base and audible cursor remain
untouched. A scratch-owner receipt prevents another builder from being mistaken
for the retained result. `prefix_reuse` / `prefix_restart` records describe the
low-frequency ownership transitions rather than logging every prework grain.

Task 2.3 and HITL acceptance stay open. This does not claim instant reentry:
frozen-tail/bridge/finalization work remains, and new history during a wrap may
invalidate normalization for all potentially affected passes. The provisional
6,240-grain baseline remains unchanged; added bounded stores need device timing
validation alongside rapid punch-in/out, wraps and original-Off preservation.



Continuous-preparation validation: full native suite 2,130 passed / one skipped;
final focused suite 194 passed, including superseded scratch-owner rejection.
Post-commit adopted per-pass events match fresh canonical replay across the
existing stop variants, including companion edits and the large-capture fixture.
Strict OpenSpec and diff checks pass. Capture-serial build succeeds with RAM1
local headroom 6,176 bytes, unchanged. Upload and one retry failed: the Teensy
loader reported no boards on USB and requested the physical program button;
no `cu.usbmodem` port was present. This firmware is built but NOT uploaded or
HITL-validated. Reconnect/program-button action is needed before retrying.

**2026-09-16 live canonical-base prework and stop estimate correction:**
The `003056` manual capture exposed a small-tail estimate incorrectly used as
candidate capacity. Stop now estimates from the prewarmed capture owner and the
prepared canonical base (or its conservative full capacity when unavailable).
The pass estimate includes record, existing overdubs and the incoming capture.
It no longer infers capacity from a rolling emission window or final tail count.

Live overdub and Stop share `stepCanonicalBase`: prefix normalization followed
by the existing pass merge and view fill/order worker, one child per owner turn.
The live VisualCache turn can prepare the immutable candidate bridge base while
recording the overdub. Paint dirtiness alone does not block this work while
visual reconstruction is disabled; wrap append and source-span work retain
priority. Source/storage changes invalidate prework; current playback and the
selected bridge remain untouched. Stop resumes partial work or reuses a ready
base. Frozen-tail, companion and cleanup work still remains after Stop, so this
is not proof of immediate reentry or a realtime deadline pass. Task 2.3 and the
HITL gates remain open; the provisional 6,240-grain baseline is unchanged.



Validation: full native suite 2,126 passed / one skipped; final focused suite
189 passed after the prefix-owner validation adjustment. Strict OpenSpec and
`git diff --check` pass. Capture-serial build and upload succeeded; serial
`/dev/cu.usbmodem154944801` returned. RAM1 local headroom remains 6,176 bytes.
The device upload finished at 00:44:27 local time on 2026-09-16. Manual reentry
and deadline acceptance remain open.

**2026-09-16 bridge boundary reconciliation implementation:**
`refreshFrozenCompanionBridge` now invokes the shared compiler's
`rebindBoundary` rather than resetting bridge storage. Records, mapping, base
ranks, sorted base order and complete insertion order survive. The existing
merge scratch retains the complete insertion permutation after sorting; steady
compaction no longer destroys the order needed by another boundary binding.
Reconciliation runs only inheritance, affected-record checks, closure resolution
and cycle-index output in the existing entry quantum. No allocation, copy of
canonical events, or sort is introduced during this path. Original carried Offs
are enabled only by a matching live closure and disappear after it closes.

Admission checks the relevant subset of the ledger using fixed pitch/identity
summaries built from all changed records and inherited closures. Identity hash
collisions conservatively request reconciliation; they cannot bypass protection.
Changes to unrelated notes no longer invalidate preparation. Changes to affected
notes, cycle or original-Off deadline retain the closure-only reconciliation
path; no note protection or command-epoch check is removed. These smaller steps
still require device service/reservation validation; no unconditional deadline
or maximum stop-latency claim is made.

Native coverage compares repeated bindings against fresh compilation over six
cycles, changes unrelated notes every preparation turn, verifies unchanged map
and bridge storage generations, and preserves an inherited original Off during
a second source change. Capture refresh telemetry now says `closure_only`.
The provisional 6,240-grain baseline remains unchanged. Full native validation
reports 2,124 passed and one skipped; final focused validation passes 153 tests.
Firmware build and upload retry succeeded with 6,176 bytes RAM1 local headroom;
serial returned. Task 2.3 and hardware acceptance remain open.


**2026-09-16 post-finalization reentry starvation:**
`captures/session_20260916_000034.log` shows the populated bridge ready at
30.256907 s and phase-10 cleanup completed at 30.354501 s. Button A reaches
Live Overdub at 31.709, 35.371 and 37.227 s, then the combined transfer/snapshot
admission guard rejects it. Code inspection and a failing native reproducer
identify `serviceBridgeWindow` discarding a completed but unpublished future
window. It immediately rebuilds that window on subsequent turns, starving the
existing snapshot receipt consumer and leaving its admission slot occupied.
The Complete branch now releases the builder with `discard(&target)`, preserving
the ready/build destination. No snapshot admission policy is relaxed. The native
regression fails before this change and passes after, including repeated idle
yields, immutable future windows and the existing multi-cycle MIDI continuity
fixture (152 focused tests pass). Admission warnings now distinguish transfer
capacity from an occupied snapshot receipt. Full native validation: 2,123 passed, one skipped. Build/upload succeeded
with 6,176 bytes RAM1 local headroom; serial returned. Device reentry remains
unverified; task 2.3 and HITL/deadline gates remain open.


**2026-09-15 manual clean-stop routing regression:**
`captures/session_20260915_235602.log` records wrap commit at 48.314 s, Stop
Overdub at 48.428 s, and `bridge_begin` denial at device 48,438,934 us
(mask 1652 / 0x0674). Later Button A presses reach the finalization-pending
handler. The empty post-wrap candidate bypasses canonical/bridge preparation,
but the AtomicTransfer refresh guard incorrectly required a bridge for every
PLAYING destination and routed it back to CompanionBridge. The guard now
requires an actual pending capture pass. Empty stops proceed to existing
cleanup; populated PLAYING commits retain mandatory bridge preflight and
transport Stop retains precedence. Shared native routing coverage exercises
all three cases. The capture has ring overflow, so it cannot establish deadline
acceptance. Full native suite: 2,123 passed, one skipped. Firmware build/upload pass with
6,176 bytes RAM1 local headroom; serial port re-enumerated. Manual device
confirmation and task 2.3 remain open.


**Current DRY direction:** first recording reuses the existing playback view,
fill/order, coverage lifecycle, bridges and MIDI cursor. The separate recording
window/store/stream prototype was removed. Shared coverage-scoped fill is tested
(122 focused tests); source/lifecycle integration and original-boundary readiness
remain open. Earlier prototype test counts are historical, not acceptance of the
shared recording path. No delayed launch or new recording rejection is authorized.

**2026-09-15 first-record shared-window implementation:** bar 1 is installed as
raw playback coverage before RECORDING, then refreshed from the canonical
chunked capture store by the existing resumable fill/order owner. Source-count
and capture-display generation checks discard stale unpublished work; the
retained view survives a cache reset and binds final length at record stop.
Native focused coverage/prewarm tests pass 132/132 and both firmware builds
pass. Deferred main-loop maintenance is routed to FLASH, and the capture-serial
instrumentation target now passes its RAM1 local gate with 6,496 bytes of local
headroom. No upload or HITL verdict is claimed yet.

The recovered capture image has since been uploaded. Track 5 / slot 1 transport
Stop interruption passed focused precedence and silence verification; the clean
normal-stop rerun was inconclusive because the overdub-stop transition was not
confirmed. Broader HITL and reservation gates remain open.


**2026-09-15 first-record direction corrected by user:** no delayed launch is
acceptable. Prepare rolling one-bar playback windows during recording, with
bar 1 established at record start and retained for the existing final-bar return
boundary. Extend the currently overdub-only staged input path; prove late input,
held-note closure, final-length binding, and subsequent window handoffs without
rewinding transport or replaying historical events. The verified first-record
snapshot delay/catch-up defect remains unfixed. The deferred-launch proposal is
rejected history. See the active `resumable-midi-boundary-playback-maintenance`
design and specs; task and HITL gates remain open.


**2026-09-15 device-clock stop timeline ready:** capture-only telemetry now
retains every stopping grain's frozen identity, requested destination, actual
preceding MIDI service/due/deadline and span, followed by queued service and
NoteOn/Off emission records. Loss is explicit and invalidates the host timing
report. Focused native telemetry/tier: 43 passed; host timing/stop tests: 40
passed. Capture build remains within the RAM1 local gate (6,496 bytes). The
next device checkpoint is normal stop plus timed transport Stop after the
deferred-save setup precondition is confirmed. Task 4.1 remains open.

**2026-09-15 forced wrap Stop PASS:** the approved track 5/slot 1 four-bar
fixture reached `OVERDUB_STOPPING`; MIDI Stop followed 8.689 ms later. A
volatile per-track output barrier now blocks loop output before the Stop
handler's all-track CC123 fan-out and at the final Track emission call. The
rerun sent All Notes Off after 83 us, reached STOPPED after 173.607 ms, and
observed no target NoteOn for 18.9 s. Evidence:
`captures/resumable-handoff-wrap-forced4-barrier-hitl/`. This is focused
single-track silence evidence only; Task 2.3, layered playback and deadline
acceptance remain open. Native focused suites: 242 passed; host tests: 38
passed; capture firmware RAM1 locals: 6,496 bytes.

**2026-09-14 restart omission repaired and HITL PASS:** the prepared-stop path
now hands prewarmed session identities to the idle snapshot owner before close;
the stale-root overlay reads that receipt immediately. Companion publication
updates both note and MIDI-event membership. Three complete restart cycles each
played all 49 expected notes exactly once with matching NoteOffs, after a focused
Stop-interruption PASS. Evidence: `captures/resumable-handoff-admission-hitl/`
and `captures/resumable-handoff-restart-hitl/`. Firmware uploaded; transport stopped.
Native full suite 2,083 passed/1 skipped, final focused suite 124 passed, host tests
38 passed; RAM1 locals 6,496 bytes. Tasks remain 11/19. An earlier wrap-adjacent
run took legacy stop and is inconclusive; reproduce that admission fallback next
with the new capture-only denial diagnostics. Protected cleanup/undo, fully
bounded publisher/overlay work, layered parity and timing acceptance remain open.
See `FOCUSED-STOP-HITL.md` for scope and separate baseline verdicts.

**2026-09-14 restart survival FAIL:** after the focused interruption PASS, three
complete restart cycles emitted only base pitches 48–79; overdub pitches 24–39
and held 83 were absent. Canonical/display evidence still reports 49 notes while
playback's overlay has 32. Prioritize task 2.3's reserved bounded session-exit
snapshot handoff before clearing session identity; the prepared-stop production
path currently omits it. Do not insert the unbounded legacy queue call into an
active grain. Layered HITL is deferred until single-slot survival passes.
Evidence: `captures/resumable-stop-restart-survival/restart-result.json` and
`FOCUSED-STOP-HITL.md`. Transport stopped; firmware unchanged; tasks 11/19.

**2026-09-14 focused interruption PASS:** repaired concrete-output-port fixture
and stale-PLAYING skip; actual MIDI Stop observed 9.253 ms into OVERDUB_STOPPING,
All Notes Off after 70 us, then STOPPED and 9.49 s without resumed target notes.
Evidence: `captures/resumable-stop-hitl-strict-wrapper/interruption-result.json`.
This closes only the single-track interruption/silence checkpoint. Restart
survival, layered playback, broader baseline and full timing acceptance remain
open. Tasks remain 11/19; see `FOCUSED-STOP-HITL.md` for scope and next gates.

**2026-09-14 MIDI deadline correlation checkpoint:** uploaded capture-only retained
send/due/deadline timestamps and per-stop-phase summaries. Native 2,080 passed,
1 skipped. A normal stopping interval measured 97.760 ms; one canonical-order
turn crossed its entry service deadline (248 us duration, 33 us slack). Another
run retained 24.678 ms clock and 26.049 ms NoteOff lateness. This is partial
hardware evidence, not deadline acceptance. Scratch clears succeeded with the
revised host precondition; the timed interruption fixture was subsequently repaired and its focused
checkpoint passed (see the newer entry above). See `FOCUSED-STOP-HITL.md`; tasks remain 11/19 complete.

**2026-09-14 protected stop continuation:** bounded undo publication/restore,
STOPPED-only joint recovery, disabled-pass reclamation and session-close helpers
now have native coverage. Production playback honors the latched STOPPED
emission policy; full joint stop integration remains open (11/19 tasks).
See the change's `BRIDGE-PROGRESS.md` and `FOCUSED-STOP-HITL.md` for the focused
Stop regression checkpoint and explicit remaining integration. Upload and a focused HITL attempt were approved and performed on 2026-09-14.
The initial boot exhaustion is fixed: boot now reserves staging only; full
bridge readiness remains separately guarded. Device startup and normal UI are
confirmed, with 4,401 KB free PSRAM. Native: 2,077 passed, 1 skipped. The exact
managed-log binding is fixed. Normal stopping was observed; the interruption
retest remains INCONCLUSIVE_CAPTURE after a deferred-save clear refusal. See
`FOCUSED-STOP-HITL.md` for evidence and remaining gates.

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-09-12 (OpenSpec 6.4 SHIPPED; next 6.5 archive)

---

**2026-09-13 active OpenSpec continuation:**
`resumable-midi-boundary-playback-maintenance` is **11/19 tasks complete**.
DEC-060 records user approval of canonical per-pass companion application for
combined capture preparation. Preserve immutable pass membership and apply rows
independently before the canonical merge; flattened admission is not the oracle.
The bounded per-pass owner now applies and merges immutable input descriptors
with pass/event origins and generation/capacity guards. Per-pass mapping now
validates an explicit published-base provenance table and feeds the ranked bridge
compiler/cursor. Full-loop view fill now constructs and owns the table alongside
events from a completed effective-pass baseline. A generation-checked sealed-pass
reader now copies raw chunks in bounded event/chunk visits without allocation
or partial-result exposure (2026-09-14; event-store tests 34/34). Production canonical pass
lifetime integration remains open. Bounded history preparation
now covers Create/Delete/Length/Pitch/Velocity/NoteRange, including contiguous row
collapse and Create identity reactivation. DEC-061 records the approved stable
Create sort correction. A runtime-owned continuation now connects frozen
sealed-pass descriptors through history application to separate effective-pass
inputs, with a generation-bound companion consumer. A direct LoopPasses overload
now selects canonical passes and filters active history in bounded grains;
a frozen-source entry point now checks live revision/generation/epoch, loop
length and capture state even on completed-result access. The typed companion
consumer now also binds the prepared row owner, rejecting same-generation row
reuse and mismatched capture owners. A frozen-overdub entry now orders capture
with private indexes and normalizes it as the final pass without changing the
shared audible arrays. Companion continuation now preserves historical Delete/
Create membership across the stage boundary through a bounded prefix scan.
End-to-end native bridge traces now cover 24 canonical history/capture variants,
a four-cycle open-note capture trace, and epoch cancellation/rebuild in mapping
and compilation. Ranked mapping/compilation now also feeds runtime admission
with live owner/generation checks retained through final selection preflight.
Executable uint64 bounds now compose ranked preparation, mapping and compilation;
the conservative full-horizon result exceeds the existing stop budget, so
completed sealed prefixes can now be reused without selection/read/history replay.
A runtime-owned prefix continuation now binds live revision/generation/length,
latches denial until the source changes, separates cleanup from restart, and
yields ownership to a frozen tail. The VisualCache owner now schedules at most
one prefix child across all tracks, with Q=32 and a global early return. Stop,
recording and existing active visual work retain priority; eligible overdub
tracks rotate without allocating missing runtime storage. A runtime coordinator now extends the owned prefix
through frozen-tail normalization, row preparation and canonical per-pass
companions in separate grains. It binds all child generations and retains
live source/epoch checks through ranked bridge preflight. Production stop routing,
atomic admission, admitted-tail/service proof and HITL remain open. Ranked-tail
bounds now accept an explicit maximum pass count and published-base event count;
C=2048/P=2/B=2048 with no history/companions still needs 7648 additional grains
at Q=32. Invalid quantum, out-of-staging event counts and overflowing legacy
contract totals now deny before stop admission. The order builder now checks
retained adjacency during fill and bypasses merge/copy only when the exact
comparator proves the input already ordered (64 grains for 2048 rows at Q=32).
Unordered inputs retain the merge path; worst-case bounds and production stop
limits are unchanged. Frozen-tail normalization now imports completed canonical
capture indexes into its private array in Q-bounded steps. Shared-order reuse
invalidates an in-flight import; after copying, audible ordering is independent.
Canonical and audible order bindings now also record that storage generation,
so discarding a later audible order cannot revive an older canonical completion.
Stale bindings deny resume/rewrite/readiness; tail admission uses private sorting
until a current canonical order is explicitly rebuilt.
When canonical order is already complete/current, capture normalization has a
196-grain bound at C=2048,H=0,Q=32 instead of 1604. The initial canonical sort is
still charged. The executable prepared-order aggregate now gives 6240 additional
grains for C=2048/P=2/B=2048,H=R=0,Q=32. The general 7648-grain aggregate and
legacy order/rewrite credits are unchanged. DEC-062 now approves a separate
provisional 6240-grain ranked-preparation allowance for continued integration.
Determine realtime reservations and elapsed stop latency with HITL once the
complete path can run; the old 3072 is not a product latency ceiling.
`PendingStopAdmission` now accepts an optional uint64 ranked-work bound at begin,
validates the 6240 allowance and combined counter capacity before mutation,
and tracks ranked credits separately. Finalization requires all three counters
empty; transport Stop preserves credits and changes epoch/destination. Existing
production callers still request zero ranked credits pending typed tail/bridge
routing; exhausting credits does not replace prepared-source readiness checks.
Reserved tail/bridge step entry points now bind the pending stop's frozen
generation, epoch and admitted quantum, charge one credit before one child,
and deny exhausted/stale reservations without advancing it. Completed readiness
queries consume no credit. Native end-to-end coverage uses these entries;
production stage dispatch, setup charging and atomic publication remain next.
A STOPPED-only typed oracle publisher now owns canonical baseline events and
pass origins together, upgrading flat same-identity views before bridge mapping.
It preserves the existing view on invalid baseline/coverage preflight and needs
no allocation. Track's all-stopped merged-event preparation now invokes canonical
baseline construction using idle prewarmed workers. It publishes typed origins
only when the complete canonical event sequence matches the existing oracle;
otherwise it retains flat publication. Temporary workers are released on success
or denial, without cancelling an already-owned prefix. This is synchronous
STOPPED throughput, never an active grain. Production ranked-stop stage dispatch
and atomic canonical/audible handoff remain open. A joint audible preflight now
requires AtomicTransfer phase, exhausted stop counters, matching command epoch,
PLAYING destination, explicit base/target identities and ready bridge/ledger/
cursor state. It is read-only and does not establish canonical companion-row
publication. Final bridge selection also rejects phase >= loop length instead
of accepting it through modulo coverage. Canonical companion staging is still
required before enabling the combined production commit. Loop-owned companion
history staging now prewarms a separate EditPass vector while STOPPED and copies
history metadata, Create payload events and prepared companion rows in Q-bounded
units. Prospective IDs do not advance nextPassId; canonical rows/revisions stay
unchanged. Source/storage/row-owner/ID changes hide the candidate. Logical discard
retains storage. The history handoff now swaps prebuilt canonical rows and the
snapshot companion-ID list, advances IDs/revision, and retains an immutable ID
receipt for the existing undo owner. Snapshot clear/replacement and append guards
reject stale candidates. Old history stays allocated until STOPPED prewarm.
A joint canonical helper now preflights the reserved capture pass and history,
transfers capture, then publishes history without a stale revision recheck.
It retires capture using scalar writes and preserves the one-revision-per-seal
convention. Track's prepared-stop entry now preflights exact canonical/companion owners,
source identities, stop epoch/credits/destination, cursor and ledger, then commits
canonical data and publishes the bridge without a second fallible selection.
Native fixtures verify matching identities and retained selected-bridge storage.
Loop's prepared-history continuation now verifies every staged sealed capture
event against the raw frozen canonical order in bounded event/chunk visits.
The joint handoff requires this proof; same-count payload/order mismatches and
missing/extra events deny publication. The rewrite cursor also binds its original
store. Stop admission now reserves canonical preparation separately from the 6240
ranked grains, rejects combined counter overflow before mutation, and requires
both counters exhausted for commit. An admitted step checks Q, frozen epoch and
owners before charging exactly one canonical grain; completed queries are free.
STOPPED prewarm now reserves both swap sides. After receipt consumption, a
bounded recycler retains allocations and restores preparation within the remaining
row/snapshot horizon. Two consecutive commits reuse the same buffers; exhausted
capacity or intervening canonical/mailbox changes deny reuse. Stop admission now
reserves post-commit history recycling separately, checks the combined credit
limit, and permits consumption only in DeferredCleanup after preparation finishes.
The joint commit requires and preserves that reservation; transport Stop also
preserves it. The native joint fixture charges setup and recycling while retaining
canonical revision and selected-bridge data. Track's recycling entries now bind
that committed Loop and preparation generation, charge receipt-consumed setup
separately, and enforce the admitted Q for each later grain. Wrong owner or
replacement preparation cannot consume the old reservation. Completed queries
remain free only while canonical/mailbox identity stays current. Loop now offers
bounded session undo-ID collection into caller-reserved private lists, preserving
the legacy cursor prefix and duplicate/order semantics. Session mutations invalidate
paused and completed collection. The maximum 2048-row/2048-companion collection
fits 193 grains at Q=32, separate from ranked preparation and recycling. The
collector now also appends the final capture and prepared companion receipt in
bounded stages, preserving the legacy final-pass inclusion and duplicate rules.
Receipt generation/lifetime and canonical identity guard paused and completed
collection. TrackUndo now prepares a private UndoEntry by setting metadata,
retaining the supplied before-snapshot and swapping the bounded ID outputs;
single-pass entries retain the legacy scalar representation. Preparation consumes
the collection without releasing vector buffers or existing snapshot roots.
Global publication and redo/trim retirement still need bounded integration.
DEC-063's accepted lifetime helpers now drain immutable snapshot graphs, persisted
snapshot/chunk payloads and complete UndoEntry resources in bounded units. A
private-stack trim continuation releases each entry before rechecking physical
pressure and shifts surviving entries one per unit. Shared snapshot readers remain
unchanged in per-grain native checks. Focused suites pass 37/37 and 43/43; full native passes 2069 with 1 skipped.
Firmware passes with RAM1 code 425736; strict OpenSpec and whitespace checks pass. These helpers require exclusive unpublished ownership
beyond a no-fail point; production locking, attachments, commands, reservations and
joint dispatch remain open. See the current BRIDGE-PROGRESS lifetime section.
A native pressure-feedback reproducer now proves that retaining removed roots
can change the surviving undo prefix (10 versus 8 entries in the controlled case).
The [undo publication reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/UNDO-PUBLICATION-REASSESSMENT.md)
records the accepted DEC-063 protected transaction with bounded reclamation.
The latest deferred Undo/Redo request wins per affected Track and executes once
after finalization under the destination policy; Transport Stop stays immediate.
Feedback trimming remains unchanged. Implementation and native proof precede
runnable HITL. Focused pool-budget validation passes **18/18**; this review changed no
firmware behavior. Production dispatch,
receipt/undo sequencing and active snapshot-box refill remain open. This canonical staging work is separate from the
6240 ranked-preparation allowance. A required live storage-generation guard now rejects timeline
reset/revision reuse, disabled-history reclamation and deferred edit appends
that can relocate rows without a playback revision change. The marker advances
before reset releases chunks, reclamation moves rows, or append reallocates
history, and remains bound across frozen-tail handoff. Native lifetime and
scheduler policy coverage do not establish an on-device latency bound.
Empty-history normalization and empty-companion merging now bypass
the apply worker and its extra copies, with corresponding tighter bounds.
Per-pass bridge mapping now uses a bounded sorted origin index and binary
search instead of quadratic scans; its maximum Q=32 bound is 2564 grains.
Production atomic transfer/routing, remaining combined-history/companion parity, duplicate
mergeSequence parity, production lifetime enforcement, capture freezing,
and admission remain open. See the
[Create reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CREATE-HISTORY-REASSESSMENT.md).
See
[scope reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/COMPANION-SCOPE-REASSESSMENT.md).
Task 3.5 STOPPING MIDI-poll/save policy is verified. Bounded bridge compilation
and the production selected-bridge consumer retain cross-wrap open-note
closures in native tests; capture-side creation and the canonical/audible bridge
handoff remain incomplete. Private UndoEntry preparation validation and capture-serial build pass
(RAM1 code 425736, locals 6496). Final full native run: **2061 passed, 1 skipped**
(`/tmp/resumable-undo-entry-native.log`), including combined receipt handoff,
single/multiple-pass representation, buffer/snapshot retention and denial guards;
bounded
Delete and wrap-head selection now match the actual resolver in 1,000
generated differential fixtures, including retention masks and inferred loop
length. Maximum-size completion, cancellation, and restart/capacity guards pass.
DEC-059's revised pairing reference remains the oracle. Length overlap traversal now preserves shortening-before-deletion order in
bounded quanta with held actions; 500 generated cases match the actual Length
resolver when using its existing reconstruction and mutation/order primitives.
Reconstruction wrap queries now match the legacy helpers in bounded two-pass
lookups. Complete bounded pitch reconstruction now matches canonical spans and
projected/deduped note output, using prewarmed storage with stale-source and
partial-output guards. Bounded Delete/Length application and frozen batches now match sequential
reference application while retaining base provenance. Bounded bridge mapping
now preserves final canonical ranks separately from that provenance. The ranked
compiler and cursor consume them with a bounded immutable-base index sort;
combined capture/production integration remains open. Aggregate stop-grain/
latency proof, bridge mapping/publication, and production transfer remain open;
host timing fixtures **22/22**;
capture-serial RAM1 locals **6720**. No upload/HITL. Details and next integration
work: [BRIDGE-PROGRESS.md](../../openspec/changes/resumable-midi-boundary-playback-maintenance/BRIDGE-PROGRESS.md).

## Current branch

**`feature/overdub-wrap-snapshot-publication`** — 3.5 wrap-journal publication
forked from `feature/immutable-loop-snapshot-passes`. Parent:
**`feature/immutable-loop-snapshot-passes`** — forked from
`546169f66658b87c54e6166b79eba74628aa3d1e` for the immutable loop snapshot
architecture proposal. The maintained-LCR continuation from
`feature/overdub-stop-lag-stage-c` is not pursued on this branch. See
[`CURRENT_WORK.md`](CURRENT_WORK.md). Record and overdub `LoopSnapshot`
publication queue at stop and step from idle (`DEC-045`) in the current
firmware. Task `3.5` now queues bounded wrap journals and steps them during
PLAYING/OVERDUBBING after the first root (`DEC-047`). Playback still reads a
stale published window plus unpublished session passes until consumer
migration. OVERDUBBING idle does not run visual reconstruct. NOTE_EDIT close
stays synchronous.
Device PASS [`233308`](../../captures/session_20260825_233308.log): no
`snapshot_idle` after first publish until STOPPED; PLAY4 BPM 119.966–120.049.
Capture-only `snapshot_idle` telemetry exposed the root/queue boundary, and
device capture [`232633`](../../captures/session_20260826_232633.log) verified
that the PLAYING/OVERDUBBING rows before root publication were one in-flight
record construction with no queued successor passes. The final `pending=0`
row published the root; no later PLAYING/OVERDUBBING idle rows occurred.
4b.2c is closed without amending `DEC-045`.
Tasks `3.5`/`3.6` and 4.3 are closed. Remaining-interval yield proof is
parked (`kSnapshotIdleReserveYieldEnabled` is false). OpenSpec 4.4a–4.4c
are closed. 4.5a is closed: overdub stop holds the composed frame and
elevates session-exit Snapshot idle until the successor root is current
([`145633`](../../captures/session_20260908_145633.log)). 4.4d is closed:
NOTE_EDIT dirty paint/select uses the current snapshot as committed base.
4.4e Stage 1 is in tree: `resolveDisplayNotes(*snapshot)` copies
note-span membership. Device
Device PASS [`162207`](../../captures/session_20260908_162207.log)
questions 1–2 (open visual **98**).
Q3 [`170211`](../../captures/session_20260908_170211.log) earned Stage 2.
Stage 2 device PASS [`171444`](../../captures/session_20260908_171444.log):
current-root opens share the span NoteId set. Stage 3 device PASS
[`173531`](../../captures/session_20260908_173531.log): commit rematerialize
uses `copyNoteEditCommitBase`. Close bake PASS
[`174646`](../../captures/session_20260908_174646.log): empty-id exit does
not invent `NoteEditPassClosed`.
4.4f leftover NOTE_EDIT oracles are closed: current-root open skips
`assignMissingNoteIds(loop.midiEvents())`; E: rows undo/redo fallback
copies the opened snapshot root (`tryCopyNoteEditOpenedSnapshotEvents`).
RAM1 locals **6848**; native **1772 succeeded, 1 skipped**.
Device [`191346`](../../captures/session_20260908_191346.log): both
`current=1` opens `only_*=0` (120 then 119).
[`192155`](../../captures/session_20260908_192155.log): `current=0` open
keeps pass rematerialize (`only_store=5`); three `current=1` `root_rev=41`
opens are **141** / `only_*=0`.
Wrap-head vs last-Off pairing RC1–RC3 is accepted (native shipped;
[`185428`](../../captures/session_20260908_185428.log) RC3 analog PASS;
174646 recapture waived).
Close publish: `NoteRange` reuses the current snapshot pitch
([`note_edit_snapshot_note_range_pitch_bugfix.md`](../Plans/note_edit_snapshot_note_range_pitch_bugfix.md)).
4.4g is closed: `validateAndCleanupMidiEvents` copies
`copyIdleValidateEvents` (current-root span-filtered snapshot events).
Overdub analysis current-root gather already skips pass flatten (4.4a).
RAM1 locals **6848**; native **1773 succeeded, 1 skipped**.
Device [`193700`](../../captures/session_20260908_193700.log): second
overdub open `from=snapshot` at `root_rev=7` `play_rev=7`.
[`194039`](../../captures/session_20260908_194039.log): NOTE_EDIT open
`current=1` `root_rev=8` **177** / `only_*=0`.
[`194438`](../../captures/session_20260908_194438.log): Stage 1 idle
validate at 600.015 s on current `root_rev=1`; both overdub opens
`from=snapshot`.
4.4h Stage 1 shipped: `rebuildDisplayNotesInWindow` copies
`tryCopyCurrentSnapshotDisplayNotesInWindow` on a current root.
Device [`201106`](../../captures/session_20260908_201106.log): second
overdub open `from=snapshot` `root_rev=5` `play_rev=5` `notes=114`.
NOTE_EDIT open `current=1` `root_rev=6` **137** / `only_*=0`. `DFRAME`
**137** after second stop through session end.
DEC-055 Stage 3 device PASS
[`221906`](../../captures/session_20260908_221906.log): first Move
`push,e00` then `resolve,9669,1,3,0`; `phase,setup` 303 µs. Stage 2 device PASS
[`220445`](../../captures/session_20260908_220445.log): first Move
`UNDO_WARM,push,e00` (1358 µs, 79 notes, kind=Move); `focus_snap` = 0.
Stage 1 device PASS
[`212850`](../../captures/session_20260908_212850.log). Option B rejected.
Hydrate 1 device functional PASS
[`224533`](../../captures/session_20260908_224533.log): select neighborhood
and first `push,e00` hold. Fast fader-2 / bar-sweep sluggishness is stacked
Move `GEOM_APPLY` on `hostdrain`, not a neighborhood fail. Hydrate 2–3
device functional PASS [`231008`](../../captures/session_20260908_231008.log):
`candidates_hit` then `candidates_miss`; first `push,e00` holds.
Hydrate 4a mover-only seed is in tree
([`232826`](../../captures/session_20260908_232826.log) /
[`233644`](../../captures/session_20260908_233644.log) Move/Pitch).
DEC-056 miss-path device PASS
[`000740`](../../captures/session_20260909_000740.log): close skips
stale-parent publish; overdub source shows `id=93 p=85`. Current-root
close [`000237`](../../captures/session_20260909_000237.log) `root_rev`
4→6. Hydrate 4a empty apply scratch made no-edit close rematerialize
and bake Deletes; [`001857`](../../captures/session_20260909_001857.log)
hung at `EditSelectNoteState::onExit`. Empty-store bake device PASS
[`002450`](../../captures/session_20260909_002450.log)
`NoteEditPass close skip empty-store bake`. Same capture left NOTE_EDIT
open into OVERDUBBING. Compose Stage 1 device PASS
[`112612`](../../captures/session_20260909_112612.log): capture-active
NOTE_EDIT paint always gathers (`includeActiveCapture`) and keys the
paint cache on `captureDisplayRevision`. Open visual **48**; in-edit
`DFRAME` **53 → 63**. Native **1795 succeeded, 1 skipped**. RAM1 locals
**7008**. HITL `--preset note_edit_overdub_compose` / stimulus
`STIM-NE-OD-COMPOSE-02` v1 (promoted TEMP-A). `01` remains paint-only.
Compose Stage 1+2 device PASS. Exit persist parked mover shipped
([`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json)
**13/13**; [`note_edit_exit_persist_selected_mover_bugfix.md`](../Plans/note_edit_exit_persist_selected_mover_bugfix.md)).
First-note loop-end close commit is RC1
([`192140`](../../captures/session_20260909_192140.log) `ChangeLength` 80–2303;
watch HITL [`132708`](../../captures/session_20260909_132708.log) did not
reproduce the store-pair form;
[`note_edit_record_first_note_loopend_display_bugfix.md`](../Plans/note_edit_record_first_note_loopend_display_bugfix.md)).
`STIM-NE-OD-COMPOSE-03` last-bar beat-4 Shorten HITL device PASS
[`133714`](../../captures/session_20260909_133714.log). After that recipe,
[`134549`](../../captures/session_20260909_134549.log) Shorten `58` `2102–2111`
then LCR `e=2212` / reopen `empty_step`
([`note_edit_overdub_compose_shorten_hold_select_bugfix.md`](../Plans/note_edit_overdub_compose_shorten_hold_select_bugfix.md)).
([`note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md`](../Plans/note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md)).
([`note_edit_overdub_paint_overlap_compose_bugfix.md`](../Plans/note_edit_overdub_paint_overlap_compose_bugfix.md)).
Do not start hydrate 4b–5.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).
DEC-054 Stage 1 admission remains in tree; [`204458`](../../captures/session_20260908_204458.log)
failed the select-cadence gate. Parent 4.4 closed (inventory
2026-09-09). **4.5 SHIPPED** (2026-09-10): `base` / occupy / compose `02`
PASS. **4.6** Stages 1–6 **SHIPPED** / task checked off (derived-rep identity
freshness; §5 undo/load excluded; §6 materialize retirement remains). Stage 6
HITL `base` [`141547`](../../captures/session_20260911_141547.log).
(A→B@R native; RAM1 **7008**; native **1821/1**). Stage 2+ remain.
VisualCache stamp). Persistence and snapshot-identity undo (§5) remain
later. Hydrate 4b–5 and raise `N` stay parked.
The following publication-admission paragraphs retain historical intermediate
captures; their earlier “recapture remains open” wording is superseded by the
closed 3.5/3.6 evidence above.
Latest `teensy41-capture-serial` build restored RAM1 locals to **7008**
without USB-host ownership changes, so task `3.6` recapture is unblocked.
The follow-up admission fix allows queued session-exit snapshot work after
wrap-journal drain and permits the bounded Snapshot owner while tracks are
stopped but the transport clock remains running. Focused native coverage
passes; full native and device recapture remain open. Construction now
persists membership-copy, mutation, append, and event-sort progress at one
fixed-capacity membership page per admitted unit. The capture-serial build
remains at **7008** RAM1 locals; device `T_RT_reserve` recapture remains open.
Capture [`202635`](../../captures/session_20260827_202635.log) confirms journal
drain and eventual `pending=0`, but still records 79.169 ms and 74.453 ms
slices from whole-pass session-exit start. That start now uses a persistent
`CommittedEventRange` cursor and the existing journal caps so one admitted
unit builds at most one bounded pass-change fragment. Capture
[`204136`](../../captures/session_20260827_204136.log) published `root_rev=8`
when wrap journals drained while session-exit passes were still queued;
playback then dropped the first record pass. Wrap completion now waits for
queued passes, construction progress, and companion changes before
`storePublishedSnapshot`. Stage 1 STOPPED admission is closed on
[`221054`](../../captures/session_20260827_221054.log): Snapshot idle ran
after stop, the candidate stayed private, and publication occurred only at
`pending=0` / `root=0x70400ac8` / `root_rev=10`.
Stage 2 child timing is in tree on `snapshot_idle`
(`start`, `pass_change`, `step`, `finish`, `publish`, `probe`, `enqueue`,
`discard`, `emit`). Capture-serial RAM1 locals **6976**; native **1491/1491**.
Capture [`225659`](../../captures/session_20260827_225659.log) names the
224842 record-stop leftover as `discardPendingIfLoopLengthChanged`
(`discard=15262` on the 20.984 ms PLAYING idle). Exclusive leftover after
that probe is ≤ 39 µs. STOPPED session-exit `start`/`pass_change` reaches
19.996 ms. Publication closed at `pending=0` / `root=0x70431844` /
`root_rev=8`. The Stage 2 falsification review is recorded in
[`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md).
Session-exit length handoff device PASS
[`003015`](../../captures/session_20260828_003015.log): record-stop
PLAYING `discard=47`, first root `pending=0` / `root=0x703248cc` /
`root_rev=1`. Non-fit discard remains a content-sized destructor. Device recapture [`004119`](../../captures/session_20260828_004119.log)
names the 21 ms session-exit as `note_lookup` (86.9% of
`pass_change=21050`). Caller-scoped pitch lookup device PASS
[`005527`](../../captures/session_20260828_005527.log): `note_lookup`
max 857 µs, `pass_change` max 3.219 ms.
Wrap-tick derived reconstruction is removed from the wrap clock interval
([`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](../Plans/overdub_wrap_derived_rebuild_clock_interval_bugfix.md)).
Musical wrap stays on the clock. Rank 6 next-tick full-loop compatibility
gather is replaced by incremental overdub-seal admission when the one-shot
receipt proves completeness
([`playback_merged_compatibility_rebuild_clock_interval_bugfix.md`](../Plans/playback_merged_compatibility_rebuild_clock_interval_bugfix.md)).
`DEC-049` preserves that merged stream across `stopOverdubbing` so the
same incremental admission can run after stop, and admits Snapshot on 2
of every 6 deferred turns while a publication backlog is present. Device
Stage 2 PASS [`152513`](../../captures/session_20260831_152513.log);
Stage 3 drain PASS [`153113`](../../captures/session_20260831_153113.log)
(`root_rev=7` meets `playback_rev=7`). Live-record snapshot start walks
`capture.store` from a persisted index
([`live_record_snapshot_start_bounded_fragment_bugfix.md`](../Plans/live_record_snapshot_start_bounded_fragment_bugfix.md)).
Device [`190654`](../../captures/session_20260831_190654.log) closed the
copy/scan class; remaining `start` growth was `note_lookup`. That path now
skips `findNoteAtPitch` (`openNotes` only)
([`live_record_snapshot_start_skip_parent_note_lookup_bugfix.md`](../Plans/live_record_snapshot_start_skip_parent_note_lookup_bugfix.md)).
Device [`192422`](../../captures/session_20260831_192422.log) closed the
lookup class: `note_lookup=0` on all 32 live-record starts; after-first
`start` 167–233 µs. Remaining `idle_maint` spikes are display VisualCache
(wrap-stale + `from=passes`). [#41](https://github.com/Lytrix/MidiLooper/issues/41)
Stage 1 device PASS
[`202745`](../../captures/session_20260831_202745.log)
([`display_visual_cache_snapshot_reuse_refinement.md`](../Plans/display_visual_cache_snapshot_reuse_refinement.md)):
no wrap `dsz=0` while notes exist; no wrap `from=passes`; first overdub pass
stays on the paint frame (`183,111` → `184,180`; second wrap `249,246`).
Stage 1b device FAIL [`204132`](../../captures/session_20260831_204132.log):
STOPPED DNTE is short; NOTE_EDIT open rebuild then 291 notes / `presence=0`.
Stage 1c device FAIL [`205138`](../../captures/session_20260831_205138.log)
wrap 2 `205→271` then slice `260`. Stage 1d shipped `d35d64a7`. Device FAIL
[`205917`](../../captures/session_20260831_205917.log): idle Overlay fill
left longs after wrap 2. Stage 1e (in tree): idle rejects Overlay and
fills from current committed
([`display_visual_cache_idle_slice_current_fill_bugfix.md`](../Plans/display_visual_cache_idle_slice_current_fill_bugfix.md)).
Native **1518/1518**. RAM1 locals **6944**. Device recapture required.
Stage 1f (in tree): last-resort STOPPED paint rejects Overlay
([`display_visual_cache_stopped_paint_current_fill_bugfix.md`](../Plans/display_visual_cache_stopped_paint_current_fill_bugfix.md)).
Device gate [`213448`](../../captures/session_20260831_213448.log): first
STOPPED `DISP` must equal visualCache. Stage 1g (in tree): wrap-held
spans dirty On/Off bars only
([`display_visual_cache_wrap_dirty_span_bugfix.md`](../Plans/display_visual_cache_wrap_dirty_span_bugfix.md)).
Device [`221925`](../../captures/session_20260831_221925.log): wrap `dcnt`
inconclusive on a 4-bar all-bar overdub; wrap 3 idle did not fill; STOPPED
**128 / 137** with `DNTE` 39 **144+256**.
Device [`222349`](../../captures/session_20260831_222349.log): wrap first
`DISP` drops then `slice_clean` restores; first STOPPED **159 / 31** then
`OverlayRejected` **159 / 166**; B3 held across stop; no per-note dump.
Stage 1h shipped `4238b422`: `Off@0` does not linear-steal a mid-loop On
([`display_visual_cache_held_note_wrap_pair_bugfix.md`](../Plans/display_visual_cache_held_note_wrap_pair_bugfix.md)).
Stage 1i (native **1525/1525**, RAM1 **6944**): wrap/stop first paint
holds dirty cache
([`display_visual_cache_wrap_stop_first_paint_bugfix.md`](../Plans/display_visual_cache_wrap_stop_first_paint_bugfix.md)).
Stage 1j (native **1526/1526**, RAM1 **6944**): playhead tails are
paint-only
([`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](../Plans/display_visual_cache_stop_handoff_playhead_tail_bugfix.md)).
Device [`232910`](../../captures/session_20260831_232910.log): 1k wraps
2–8 hold; wrap 1 `104→92` after idle `append_resume` 89 is the grow-vs-prefix
hole (fix: compare to composed hold). Dump `reconstruct` 327 / `frame` 412.
First STOPPED **412 / 208**. 1k/1m device PASS [`002145`](../../captures/session_20260901_002145.log).
Occupy same-start hide is in tree
([`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](../Plans/overdub_occupy_same_start_shorter_hides_longer_bugfix.md)):
reconstruct keeps both `noteId`s; display adopt/idle keep the shortest end.
Native **1533/1533**. RAM1 locals **6848**. Device recapture required
(`002145` `norow` 75/76; pitch 63 frame 96+288). 1l not in that log.
([`display_visual_cache_wrap_compose_hold_bugfix.md`](../Plans/display_visual_cache_wrap_compose_hold_bugfix.md),
[`display_visual_cache_off_zero_tail_pair_bugfix.md`](../Plans/display_visual_cache_off_zero_tail_pair_bugfix.md)).
Stage 2.1 (idle copies current `LoopSnapshotNote` membership) is in tree.
Stage 2.0 load enqueue is disabled after restore/adopt
([`023222`](../../captures/session_20260908_023222.log) rebooted on a loaded
`start` with `ld=1`). Title still exits on restore completion only
([`020111`](../../captures/session_20260908_020111.log)). After `boot_commit`,
VisualCache Stage 2.1 fallback fills from passes until a current root
exists. Session-exit snapshot publication is unchanged. Device stability
PASS [`024731`](../../captures/session_20260908_024731.log)
([`loaded_hydration_stability_bugfix.md`](../Plans/loaded_hydration_stability_bugfix.md)).
Do not freeze an observed unit or the minimum observed `reserve_us` as the
`T_RT_reserve` guarantee. [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
records the 10,000 µs policy ceiling. Remaining-interval yield proof is
parked after [`115043`](../../captures/session_20260908_115043.log)
(`remaining=0` until STOPPED). `kSnapshotIdleReserveYieldEnabled` is false;
yield helpers stay compiled. OpenSpec `3.5`/`3.6` are closed. OpenSpec `4.3`
is also closed: `LoopContentResolution` now exposes explicit stateless
`LoopSnapshot` query overloads; compatibility pass/index/checkpoint paths
remain until caller migration. Next is `4.4`, which requires a new phase
selection and gate. Construction bounds stay unchanged.
For open 4b.3 attribution, `TrackManager::updateAllTracks` now emits
capture-only per-track `track_update` maxima and over-budget counts alongside
the aggregate `tracks` timing; device recapture is required.
Display paint fallback and visual-cache fill read published snapshot events
when a root exists; compatibility gather remains the no-snapshot fallback.
A nonempty dirty visual-cache prefix does not authorize the paint window
([`235954`](../../captures/session_20260825_235954.log)). Live overdub keeps
the PLAYING committed handoff only while the paint window stays inside
the last gather ([`001048`](../../captures/session_20260826_001048.log),
[`001907`](../../captures/session_20260826_001907.log)). First gather
includes a short loop tail so a 20-bar loop does not omit the last 2 bars
([`002634`](../../captures/session_20260826_002634.log)). Overdub-stop paint
holds the composed frame — device PASS [`004934`](../../captures/session_20260826_004934.log)
(`ODUB,stop,display` 8.8 / 12.2 ms). 4b.0 wrap/hold device PASS
[`092755`](../../captures/session_20260826_092755.log)
(`ODUB,stop,display` 21.4 / **11.1 ms** after wrap). **Now:** prepare
decomposition D1 **device PASS**
[`221655`](../../captures/session_20260826_221655.log) after 1A measure FAIL.
Next is D2 design session on
[`playback_window_prepare_algorithm_decomposition_investigation.md`](../Plans/playback_window_prepare_algorithm_decomposition_investigation.md)
([#38](https://github.com/Lytrix/MidiLooper/issues/38)). Ring B/RC2 is
**closed.** Residual **4b.1b** lives on
[`playback_window_build_owner_boundary_refinement.md`](../Plans/playback_window_build_owner_boundary_refinement.md)
§ *4b.1b* and is parked. Ring A firmware is not authorized. Ring B/RC2 shipped
the five-owner cursor (snapshot, visual-cache, wrap-append, persistence,
load/display) and RC2 coverage: refuse uncovered visual-cache reads; fall back
to committed-pass reconstruction. Device PASS
[`183452`](../../captures/session_20260826_183452.log) / reconfirm
[`185141`](../../captures/session_20260826_185141.log): wrap `from=passes`,
`DISP` 286→466 / 271→437. First-overdub `midi_gap` 222.596→146.802 ms vs
[`175350`](../../captures/session_20260826_175350.log); later steady 24.325 ms
vs 366 ms. Reconstruct timings stay on the owner-boundary plan.
OpenSpec `4b.2` first-take length updates during RECORDING remain in tree;
live-record snapshot append starts now admit at most one completed bar of
new range per start so setup cost stays smaller in realtime windows;
first-take display keeps continuous playhead length (host implement; device
verify open). Ring B now owns the five-way deferred idle turn; `4b.3`
post-stop `usbdisp` /
`load_frame` / `late_on` is now in tree (device verify open);
`4b.4` overdub `DisplayResolveLiveCapture` owner refinement is now in tree
(device verify open). `DEC-046` accepts the round-robin admission and
representation-coverage direction. Ring B/RC2 firmware is **closed**.
RC4 repair and Ring A remain separate.
Slice 1 telemetry is in tree and device-validated by
[`161834`](../../captures/session_20260826_161834.log) and
[`162522`](../../captures/session_20260826_162522.log): the dominant
long-loop rebuild destination is `T0/S6`, with `T4/S0`, `T6/S0`, and `T2/S2`
also observed; `162522` directly pairs the repeated compatibility gathers with
`T0/S6`. The playback rebuild `<count>` is rebuild-call count before one-shot
emission, not distinct-destination multiplicity or an eligible-set count.
RC4 question 3 is closed from code: `adoptPersistedSnapshot` /
`restorePassesSnapshot` do not publish a `LoopSnapshot`. Question 1 is
closed by [`165024`](../../captures/session_20260826_165024.log) and
[`171159`](../../captures/session_20260826_171159.log):
all 16 `cer_heap` rows report `spill=0`, `ext=0`, `clk=0`, and `iclk=0`.
The measured internal-first spill and nested-clock mechanisms are ruled out.
`cer_recon` is in tree to split internal-heap `filt` from PSRAM-first
`span`/`proj` and to record IntervalTimer ISR `itickus`.
[`170149`](../../captures/session_20260826_170149.log) rebooted at first
clock after Record; probe counters are now file-scope volatiles.
[`171159`](../../captures/session_20260826_171159.log) closes question 1:
`filt` stays flat, `span`/`proj` take the 7.9x, `itickus=0` on every row.
The follow-up `cer_snapshot` formatting probe was removed after
[`172305`](../../captures/session_20260826_172305.log) rebooted at record start.
`playback_build` now carries a deferred `path` field (`snapshot`,
`compatibility`, or `note_edit`) so later investigation captures can identify
the representation behind each emitted maximum sample without formatting on the
clock/interval path. The existing one-shot remains an aggregate: `count` is all
rebuild calls before emission, not distinct destinations.
Device capture [`202654`](../../captures/session_20260826_202654.log) validates
the split: 82 emitted samples contain 74 `compatibility` and 8 `snapshot`
paths, with no `note_edit` samples. `T0/S6` remains compatibility-backed
(maximum 325.541 ms), while `T4/S0` uses the published snapshot
(maximum 358 us). RC4 question 5 is closed as that `T0/S6` compatibility
replay; ignore it for snapshot-path sizing. **G1 is satisfied:**
`representativeRebuildWall` = 358 µs; `prefetchTicks` = 272; horizon
unchanged. Ring A firmware is not authorized.
[`205447`](../../captures/session_20260826_205447.log) `T0/S5` rebuild is
334 µs; the 377 ms gap at that switch is `idle_maint`, not the new-slot
rebuild.
[`172802`](../../captures/session_20260826_172802.log) confirms first-clock
recording and continued overdub/stop operation after removal.
Historical checkpoint: task 4.3 was still skipped at this capture.
[`125527`](../../captures/session_20260826_125527.log) kept unpublished
snapshot in-flight through OVERDUBBING/PLAYING with repeated
`snapshot_idle` slices and `midi_gap` spikes before cadence alternation.
[`130244`](../../captures/session_20260826_130244.log) still stacked
unpublished snapshot with long visual idle reconstruct bursts; host
refinement now constrains/gates that visual fallback during contention.
[`131338`](../../captures/session_20260826_131338.log) still showed PLAYING
stalls (`midi_gap` 195 ms, 184 ms) with first-PLAYING stacked
`idle_maint`/snapshot/persist and later `usbdisp`/`load_frame`/`late_on`
windows; host refinement now broadens unpublished visual constraints to
all pending unpublished work and caps each live-record append-start range.
[`131852`](../../captures/session_20260826_131852.log) confirms the parked
issues remain: same-turn `snapshot_idle` + `PERS,work` overlap is still
visible (`snapshot_idle us=26823` then `PERS payload_slice=20806`), large
OVERDUBBING windows remain (`midi_gap=236090`, `usbdisp=143649`,
`load_frame=25638`), STOPPED still hits `midi_gap=430552`
(`usbdisp=538063`, `load_frame=401498`, `idle_maint=337246`), and overdub
display still spikes (`timing_max,DisplayResolveLiveCapture,215619`) while
`ODUB,stop,display` remains 21.9-50.7 ms. In tree: same-turn lane now blocks
deferred save after snapshot on the same loop turn; recapture pending and
4b.1 remains open.
[`132627`](../../captures/session_20260826_132627.log) no longer showed
`PERS,work` while `snapshot_idle` was active (same-turn overlap not observed
in that run), but 4b.3 and 4b.4 owners remained:
post-overdub PLAYING `midi_gap=175230`
(`usbdisp=121603`, `load_frame=14728`, `late_on=184364`), STOPPED
`midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`, `late_on=30491`),
and `timing_max,DisplayResolveLiveCapture,157754`. In tree for 4b.3:
post-overdub drain now polls MIDI during `runDeferredLoadAndDisplayFrame`
between display/load slices; recapture pending.
[`133517`](../../captures/session_20260826_133517.log) still showed
`snapshot_idle`/`PERS,work` interleaving while pending (example:
`snapshot_idle us=7360` then `PERS,work 86`), and OVERDUBBING spikes remained
(`midi_gap=200549` with `usbdisp=129828`, `load_frame=23600`, `late_on=26036`;
`midi_gap=196173` with `usbdisp=68686`, `load_frame=15178`, `late_on=96834`,
`persist_save=32056`). Post-overdub PLAYING windows in that run were in the
26-52 ms class after stop-display lines (`ODUB,stop,display` 19.6-51.4 ms).
In tree for 4b.1: lane arbitration now includes RECORDING pending snapshot
work (not PLAYING/OVERDUBBING only), and persist lane toggles now use
RECORDING/PLAYING/OVERDUBBING pending snapshot detection; recapture pending.
[`134438`](../../captures/session_20260826_134438.log) showed no `PERS,work`
entries (`0` matches) while `snapshot_idle` remained active, so same-turn
snapshot/persist overlap was not observed in that run. 4b.3 remains open:
post-overdub PLAYING still reached `midi_gap=242938`
(`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
`ODUB,stop,display=44222`; STOPPED still reached `midi_gap=323973` with
`loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
`idle_maint=319996`, and `load_frame=38737`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=225601`. In tree for 4b.3:
`MidiHandler::handleMidiInput` now reduces per-call dispatch batch size from
`128` to `32` while `anyPlayingMidiDrainAfterOverdubStop()` is active, so
post-overdub drain turns bound `usbdisp` burst size before the next poll.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS; recapture
pending.
[`135317`](../../captures/session_20260826_135317.log) again showed
`PERS,work=0` while `snapshot_idle` remained active (4b.1 still open pending
repeated recaptures). 4b.3 remains open: first stop cycle held
`ODUB,stop,display=15428`, but a later `midi_gap=181392` was logged after
PLAYING re-entered OVERDUBBING; later cycles were `midi_gap=42033` and
`midi_gap=22132`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=163008`. In tree for 4b.4:
`DisplayManager::rebuildOverdubCommittedDisplayLayer` now skips overdub paint
snapshot gather+reconstruct when `visualCache` already has notes, preferring
held-layer/window-filter branches to bound resolve cost. Verification:
`pio run -e teensy41-capture-serial` PASS with RAM1 locals `4608`, and
`pio test -e native` PASS (`1472/1472`).
[`140201`](../../captures/session_20260826_140201.log) kept `PERS,work=0`
while `snapshot_idle` remained active. 4b.3 improved but remains open:
post-stop PLAYING `midi_gap` reduced to `48361` (from `142534` in `135317`),
but first-bars OVERDUBBING still reached `midi_gap=133468`. 4b.4 improved
strongly in this run with `timing_max,DisplayResolveLiveCapture=7107`
(from `163008`). The same capture showed overdub reopen source drift
(`DIAG,lcr,src,why=open` notes `0`/`165`/`228` versus nearby PLAYING `DISP`
totals `240`/`253`/`308`) and restamp miss evidence
(`seal_publish_span_cache_restamp ... invalidated_missing_id` together with
`capture hot stop verify: non-canonical storage (check=16)`). In tree:
`Loop::commitPendingCapturePass` now invalidates source-span cache on restamp
miss, and `Loop::rebuildOverdubSourceSpanCache` now seeds from clean
`visualCache` when prepared spans miss and no prior cache seed exists.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS;
recapture pending.
[`141034`](../../captures/session_20260826_141034.log) confirms 4b.4 stays
bounded (`timing_max,DisplayResolveLiveCapture=8608`), so 4b.4 is closed.
4b.3 remains open: first-overdub windows still show repeated
`load_frame`+`usbdisp`+`late_on` lag signatures around bars 4-8, and wrap
reopen still showed `DIAG,lcr,src,why=open ... notes=0` after a restamp miss.
In tree: source-cache fallback now also seeds from non-empty dirty
`visualCache` when prepared spans miss during an overdub session.

**Queued-dest visual cache:** `invalidateForSlotChange` leaves a PLAYING queued
destination `visualCache` in place. Firmware in tree; HITL open. Plan:
[`playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md`](../Plans/playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md).

**Boot title RAM1:** `cccbeaa` margin regression fixed — DMAMEM cold globals + `assignMissingNoteIdsToNoteOnsInChunkIds` in `.cpp`. Device PASS [`161855`](../../captures/session_20260816_161855.log). Investigation frozen: [`boot_title_loadloopjob_idle_slice_investigation.md`](../Plans/boot_title_loadloopjob_idle_slice_investigation.md).

**NOTE_EDIT entry while overdubbing:** `openNoteEditSession` calls `stopOverdubbing()` first — live capture commits before rematerialize (quick fix).

**NOTE_EDIT lengthened loop select:** committed-content span for bracket/nav; skip detailed-window filter on selectable inventory. Fixture [`015731`](../../captures/session_20260814_015731.log).

**Overdub + NOTE_EDIT exit bake:** device PASS [`025322`](../../captures/session_20260814_025322.log) — `rows=1 saved=1`, session flats 234 held. Native `dropUnrequestedSessionStoreDeletes`. Stage 3 unblocked.

**OLED first-frame mismatch:** RC3 device PASS [`014553`](../../captures/session_20260814_014553.log). Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

**Loop content-only history (DEC-035):** **Archived** 2026-08-14 — OpenSpec `2026-08-14-loop-content-history-persistence`; normative `openspec/specs/loop-content-history/`. Device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log).

**NOTE_EDIT leave-restore painted span:** RC1 native shipped; device gate open — [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md). [`200154`](../../captures/session_20260813_200154.log) Restore 5 `720–2255` / select `DNTE` 1535; same-loop [`193838`](../../captures/session_20260813_193838.log) first-select paints length **47**.

**NOTE_EDIT mover wrap-length jump:** RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log) — [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md). No `2351`/`2975`. Note 14 `2256–2304` stays on the wrap-stub plan.

**NOTE_EDIT note-off pairing LIFO:** native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS — [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md). @45.971 seals five overlap rows plus the mover (`canonical=7 pre_commit=7`).

**NOTE_EDIT edit-pass replay row payload:** native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS — [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md). `Length 115 48→287` and `Length 115 1008→1103` seal and hold. Rows replay as stored.

**NOTE_EDIT overlap shorten commit seal:** native shipped; device gate open — [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md). Deselect commit dropped the overlap `Length` row on a parked mover; [`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` then @166.848 seals `Length 10 960–1247` against note 14's focus. Slice B of the resolver contracts plan **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was the correct truncation.

**NOTE_EDIT Length replay loop-boundary:** native shipped; device gate open — [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Persisted `ChangeLength` 14 `2256–2304` replayed as a wrap; same-pitch notes shortened to 2255. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log).

**NOTE_EDIT wrap-stub commit:** RC3 native shipped; device gate open — [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). RC2 device FAIL: 14 still `2256–2304` when cache paints a non-zero span. RC1 withdrawn (67.824 min-length stay-hidden).

**NOTE_EDIT display unification:** Stages 1–2 and 5–9 shipped — [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md). Device [`192007`](../../captures/session_20260813_192007.log): 45 Hide/Restore `1440–1511` (not 2160); 76 as target still `840–2160`; first NOTE_EDIT `DISP` 59/60; resolve 16.8–52.3 ms. Paint gap and undo-warm stay open.

**Active work:** Occupy clock same-tick Off-before-On HITL **PASS** [`001021`](../../captures/session_20260819_001021.log) ([`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md)) — `n=0 a=1` = 0; `n=1 a=0` = 0. Parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4. Pin [`233247`](../../captures/session_20260818_233247.log) was 11. Interval catch-up in tree. Merged-capture gather **FROZEN**. USB occupy `playMidiEvents` catch-up HITL **FAIL** [`214856`](../../captures/session_20260818_214856.log) **reverted**. Loop-head storage-0 occupy HITL **PASS** [`203948`](../../captures/session_20260818_203948.log). Wrap-S catch-up HITL **PASS**. Occupy reads `ActiveNoteLedger` at `currentTick` ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)). Occupy lookup: `ledger.noteId(midiChannel, pitch)`. Phase 2 **PASS** [`040236`](../captures/session_20260818_040236.log). PLAYING/STOPPED/MUTED HITL **skipped** ([DEC-040](../DECISION_LOG.md#dec-040-skip-playingstoppedmuted-overdub-participant-hitl)). **Phase 4 in tree**. Phase 3 1-bar occupy **PASS** [`121933`](../captures/session_20260818_121933.log). Stage 1 `from=span` HITL **parked**. Do not shrink `kOverdubSourceWindowBars`. Queued (no firmware): [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md). **NOTE_EDIT hydrate** queued ([`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md)). **Playback gather** Stage 1 hooks landed. Wrap-move persist parked [`201446`](../captures/session_20260816_201446.log).

**Merged to `dev`:** PR [#35](https://github.com/Lytrix/MidiLooper/pull/35) LoopContentResolution prototype (`be0e05f`); PR [#30](https://github.com/Lytrix/MidiLooper/pull/30) overdub overlap; PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) Stage 5a-1/5a-2 + display RC4–RC5.

**Stage 5a-3:** **`pool_alloc` proof abandoned** (2026-08-12) — see [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](../Plans/long_overdub_stage5a3_critical_reclaim_verification_refinement.md).

**`chore/firmware-ownership-lifetime-review`** — P0/P1 review **closed** (manual MT gates PASS); Phase 5 + layered **`base`** **parked**.

**`feature/deferred-lazy-load`** — Phase B archived `openspec/changes/archive/2026-07-19-deferred-job-scheduler/`. Specs: `openspec/specs/deferred-job-scheduler/`.

*(Parallel: **`feature/persistence-work-queue`** @ `8b3f93e` — B1–B5 on `dev`.)*

### Firmware lines

| Line | Branch | Role |
|------|--------|------|
| v1 | `main` | Frozen — 16×2, 2-button looper |
| v2 | `midi-faders` | Frozen — iPad/DROID, no PSRAM required |
| v3 | `dev` | Active integration |

See [`docs/BRANCHING.md`](../BRANCHING.md).

## In flight

- **§36 companion admission + STOPPED first-paint:** A+B+C closed. Device PASS [`165107`](../../captures/session_20260821_165107.log) / [`183229`](../../captures/session_20260821_183229.log). Later (parked): bound `applyDeferredVisualCacheWrapAppend` (1.13 s), attribute 5 s STOPPED idle. — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Boot title `boot_commit` display stall:** Device PASS [`161804`](../../captures/session_20260821_161804.log) — USB 11.484 s, first `DISP` visual=2638, `boot_commit=288557` (289 ms). Prior gather was `161148` / `154353` (~70–75 s, visual=0) from `onBootSlotLoadComplete` wiping the idle-built cache. Guide: [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md).
- **Boot title startup stall hardening:** capture [`161410`](../../captures/session_20260820_161410.log) stops after `DisplayManager: Display buffer displayed` and never reaches boot load logs. `Looper::setup` now uses bounded SD bootstrap retries (`3` attempts, `50 ms` delay) and an explicit empty-runtime fallback when SD stays unavailable, preventing title-screen startup stalls from requiring repeated manual reboots. Native verification pending.
- **Overdub-stop seal lag optimization:** captures [`163904`](../../captures/session_20260820_163904.log), [`164944`](../../captures/session_20260820_164944.log), [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log), [`170724`](../../captures/session_20260820_170724.log), [`171246`](../../captures/session_20260820_171246.log), [`171909`](../../captures/session_20260820_171909.log), [`172553`](../../captures/session_20260820_172553.log), [`173206`](../../captures/session_20260820_173206.log), and [`174941`](../../captures/session_20260820_174941.log) keep the stop owner on `companion_seal` while `mark_range` remains low. Deterministic follow-up landed: overdub-stop path uses `SC_REC_FLUSH_PENDING_REVTS(8)` in `Track::stopOverdubbing` and in-edit `handleNoteEditFold`, removing variable USB serial flush work from the critical path before display refresh. Telemetry-visibility follow-up landed: `ODUB,stop` lines are Tier-A in `CaptureLineTier` so stop-stage lines remain visible under ring pressure with `flush(8)`. Display-specific follow-ups are reverted per user request. Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425084 / locals 4768`); HITL re-measure pending — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Idle-gather edit apply Phase A:** `applyNoteEditPassSequence` now performs strict contiguous `Velocity`/`Pitch` update collapse with barrier-preserving execution; non-collapsing baseline parity is covered by dedicated deterministic and randomized equivalence tests in `test_edit_apply`. Validation gates passed (`pio test -e native`, `pio run -e teensy41-capture-serial`) with RAM1 still within budget (`RAM1 code 424668 / locals 4640`) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay:** captures [`165534`](../../captures/session_20260820_165534.log) and [`165956`](../../captures/session_20260820_165956.log) show early-start delays (`Live Overdub -> Overdubbing started` ~2102-2249 ms) and `ODUB,stage,begin_capture` spikes (`2194189 us`, `2248843 us`). Root cause is start-path fallback to full-loop materialize in `Loop::rebuildOverdubSourceSpanCache` when prepared spans are unavailable. Stage now restores overdub-session deterministic fallback (reuse prior cache notes during active sessions; full materialize kept for non-session paths). Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425020 / locals 4768`); waiting on HITL re-measure — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay (closed):** HITL [`170724`](../../captures/session_20260820_170724.log) confirms the restoration removed the 2.2 s start stalls: `Live Overdub -> Overdubbing started` is now 2-8 ms and `ODUB,stage,begin_capture` is 8198 us / 2150 us. Same run exposes a remaining stop long-tail cycle (`ODUB,stop,seal=310980 us`, `flush=312270 us`, `display=320406 us`, `overlap_hold note_offs=100`), while `DIAG,seal_companion_batch` remains bounded (`rows=5 sealed=5 us=84`). Start regression is fixed; stop-path lag is still open — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start reboot (fault now read; repro conditions narrowed; hang reproduced):** USB drops right after `[TRACK] Overdubbing started`. Present in [`132145`](../../captures/session_20260820_132145.log) and [`143518`](../../captures/session_20260820_143518.log), i.e. before the 2026-08-20 removals, so commits `4af158a`…`6ff4142` are reverted and the tree is byte-identical to `28cf6e3`. **Two failure modes under one symptom, both memory exhaustion.** (a) *Fault:* the 2026-09-17 00:39 session's SD crashlog reads `DACCVIOL` at `Accessed Address: 0xE00` with `Code was executing from 0x6002C97C` = `Loop::tryGatherPublishedSnapshotEventsInWindow`. **The `0xE00 == 128 * sizeof(MidiEvent)` stride argument is withdrawn (2026-09-17, step B):** `sizeof(MidiEvent)` is **20 bytes** on device, measured with the `teensy41-capture-serial` toolchain, so the address does not decode to element 128 of a `MidiEvent` store; 28 is `sizeof(PlaybackBridgeRecord)`, and the faulting object is unidentified. The "snapshot-gather fill through a lost destination base" attribution is therefore **unverified** (the landed gather guard remains useful defence in depth). `ExternalMemoryFirstAllocator` aborts rather than returning null, which is the part that still stands. (b) *Hang — reproduced:* an 8-bar record plus overdub on track 4 slot 1 reached `[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` and went permanently silent with USB still enumerated, leaving **no crash record** (`crashlog.txt` unchanged, next boot `crash=0`) — `abort()` raises no `CrashReport`, which is why this has only ever shown up as a silent hang ([`hang-8bar-track4-slot1-01`](../../captures/hang-8bar-track4-slot1-01/session_20260917_094002.log)). Internal heap was already `free=68 used=393 of 461 KB` at 127.7 s uptime, ~212 KB down from boot, before that record began. User's repro conditions: several tracks already playing loops, then an 8+ bar pass with overdub. Diagnostic landed SESSION_CAPTURE-gated (gather destination guard + `displayManager` RAM2 tail canary, `free for local variables` 6752 → 6720). Native **2192 cases: 2188 passed, 1 skipped, 2 known unguarded reentry failures**; RAM1 locals **6720**; instrumented firmware flashed. **Both boundaries now deny instead of aborting** (overdub via `admitOperationMemory`, record arm via the required prewarm's own result - `Track::startRecording` returns before any mutation, and a missing runtime slot is a denial too). Device [`arm-admission-01`](../../captures/arm-admission-01/session_20260917_113447.log): one `op=companion_history_reserve`, one `op=record_start`, zero `Recording started`, zero `FATAL`, device alive. Native **2193 cases: 2189 passed, 1 skipped, 2 known unguarded reentry failures**. **All five are now investigated, and step E is implemented (DEC-065).** Loaded-set 8-bar rerun [`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log) closed the exhaustion RC: zero `FATAL`, zero `Recording started`, device alive, playback continuing, one `op=companion_history_reserve` + one `op=record_start` denial, **arm delta 1,240 KB**, heap 64-76 KB at the refusal. The audit shows the seven prewarms are one concurrently-live working set (not shareable) and that `sizeof(MidiEvent)` is **20**, not 28, which withdraws the `0xE00 == 128 * sizeof(MidiEvent)` crashlog attribution. Step E then made staging admission-provisioned: boot PSRAM headroom **+1,189 KB** (`tag 3` = 0 at boot), arm delta 1,469 KB including both staging roles, and the point-of-use gate now treats the probe as the authority for the `InternalHeapFirstAllocator` history reserve instead of vetoing on the heap floor while ~1.27 MB of pool was free. Result on the same recipe ([`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log)): **0 denial, 0 refusal, 0 FATAL**, 128 record notes, 16 overdub events, and the deferred stop completes `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` at 50.667 s. Next priority (user-directed): **HITL oracle repair, then the bridge tail.** C3/DEC-066 is closed as validated. The oracle repair is landed: the deferred stop (`OVERDUBBING -> OVERDUB_STOPPING -> PLAYING`) is first-class in the transition counts and the phase pairing, duplicate observer `ST,Track` edges are collapsed (they doubled every count and mis-paired a successor interval to the predecessor's stop), and a wall-clock-proxied stop reports its length instead of failing it. Device [`oracle-fixed-01`](../../captures/oracle-fixed-01/session_20260917_125305.log) reports **PASS**: deferred hop counted, overdub phase 16/16 notes (was 0/0), proxy-paced 9,216-tick length a report, SEVT/cleanup/display green, only an advisory `overdub_first_note_too_late`. Host tests 26/26, 30/30, 25/25 green. OPEN: the **successor-reentry device case** is scoped but not built - with the repaired oracle the canonical second-overdub run now PASSES ([`successor-reentry-06`](../../captures/successor-reentry-06/session_20260917_130431.log), two overdub intervals, both stops counted through the deferred hop, SEVT/display green) but it is sequential: measured OD1 stop 41,064.8 ms -> OD2 start 49,713.4 ms, an 8,648 ms gap against the predecessor's ~1.2 s deferred window, and removing the post-stop undo/redo does not change it. Needs a millisecond-scheduled successor press plus a verifier predicate that the successor starts before the predecessor's `bridge_ready`. Also found: an aborted run can wedge the deferred workspace save (`Persistence drain stuck after 256 iterations`, 158 KB pending) and then **every** later run fails its clear precondition until the device is re-flashed - the device does not reset on serial open and a 1200-baud touch did not reset it; recovery is `pio run -t upload`. The bridge tail is **profiled**: 490 deferred-stop grains, 68.0 ms of total grain work in a 1,068 ms window, 466 of the grains and 65.4 ms of the work in `CompanionBridge`, issued at a 1.2-3.2 ms cadence - so it is **cadence-bound, not work-bound**, and any optimization DEC would be about the deferred-stop owner's turn cadence/per-turn grain budget argued against the MIDI-boundary budget, not about the bridge algorithm. `CompanionHistory` is down to 4 grains (was ~2,030 Trim units), so C3's work shaping shows in the profile. Not pursuing C4, event-horizon ceiling reduction or seal-phase reserve. Previous C3 summary:  Per-owner capacity derivation replaced "2048 of everything": the companion-row reserve is derived from the loop's canonical payloads (clamped to the existing 2,048 ceiling), `RuntimeCaptureCompanions` now admits against the rows a frozen change set can produce rather than its whole vector, and the prepared history sizes its working rows to the interval's actual demand, removing the `Phase::Trim` walk (~2,030 units, ~64 prep turns per stop). Measured against the step-E baseline on the same loaded-set 8-bar recipe: arm delta **1,469 -> 1,315 KB**, `tag 2` **790 -> 715 KB**, stop press -> `bridge_base` **220 -> 112 ms**, stop-window allocations **0**, SEVT/canonical unchanged and correct, **0 FATAL / 0 denial / 0 refusal**. `bridge_ready` is unchanged at ~1.18 s because the bridge map/compile dominates it, exactly as the per-owner analysis predicted. Native **2196 cases: 2192 passed, 1 skipped, 2 pre-existing failures**. The event-horizon owners remain ceiling-bound by design (their interval term is the staging admission bound and their reserve is taken at the arm); the maximum-envelope case is fixture-covered on a dense loop rather than device-covered. **C4 is not being taken** - after reclaiming 1.19 MB at boot, giving up cross-track deferred-stop concurrency for ~228 KB is no longer attractive. Separately: the host-side HITL edges (`OVERDUBBING->PLAYING` vs the deferred `OVERDUB_STOPPING->PLAYING` path, the wall-clock bar-sync proxy and `overdub_first_note_missing`) need repair against the deferred state machine, independent of the memory work. — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub session index reboot undo:** HITL **PASS** [`003854`](../../captures/session_20260820_003854.log) — reboot `entries=2` `kind=1`; one undo 112→14 — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Cleanup branch telemetry/fader compile-gating:** `chore/cleanup-codebase-tidiness` stage shipped — telemetry-only timing work gated by owning feature macros; `MidiHandler` motor-fader USB-host service helper now feature-gated to `MIDI_USB_FADER_PROBE_PASSTHROUGH`; verification passed (`pio test -e native`, `pio run -e teensy41`, `pio run -e teensy41-capture-serial`) with zero warnings.
- **Overdub-stop handoff flash:** dirty `visualCache` must not overwrite the RC5a composed frame after overdub stop. HITL **PASS** [`001925`](../../captures/session_20260820_001925.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Display window follow readiness:** committed paint filters `visualCache.notes` for the moving 16-bar window plus lookahead; source-view is not display authority. Native **1390/1390**; RAM1 code **425276** / locals **4768**. HITL **PASS** [`000553`](../../captures/session_20260820_000553.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Companion-only projection fast path:** Full companion projection cache is admitted only while `capture.phase == CapturePhase::Overdub`. STOPPED/boot gather uses windowed apply. Mixed companion+non-companion rows keep legacy ordered replay fallback. Stage A on [`165107`](../../captures/session_20260821_165107.log) keeps Overdub `tracks` at 190 ms (was 136 s on [`162928`](../../captures/session_20260821_162928.log)).
- **Overdub participant discovery:** `feature/overdub-participant-discovery` — occupy clock same-tick Off-before-On HITL PASS [`001021`](../../captures/session_20260819_001021.log); parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4 `n=0 a=1`; USB `playMidiEvents` catch-up HITL FAIL [`214856`](../../captures/session_20260818_214856.log) reverted; wrap-S HITL PASS; loop-head storage-0 HITL PASS [`203948`](../../captures/session_20260818_203948.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Codebase consistency:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) **NOW** — Phase 4 TrackManager **shipped** PR #22; next extractions in [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)
- **StorageManager TU remaining:** **Merged** PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08); closeout [#16](https://github.com/Lytrix/MidiLooper/issues/16)
- **Firmware ownership lifetime review:** **Closed** 2026-08-06 — [`firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md); layered **`base`** HITL **parked** for dedicated refactor
- **HITL CLI rebuild:** Phase 3 — layered presets locked to `base` + `edit_full`; layered **`base`** device PASS deferred to dedicated HITL refactor
- **Edit-session-action-geometry:** **Archived** 2026-08-05 → `openspec/specs/edit-session-action-geometry/`; Phase 5 HITL matrix parked (`m8_edit_note_edit_hitl_automation_refinement.md`)
- **Note-edit control-surface split:** **complete** on `chore/note-edit-control-surface-split` — Phases 0–8; see [note_edit_control_surface_split_refinement.md](../Plans/note_edit_control_surface_split_refinement.md)
- **Long record display freeze:** queued — [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md); evidence [`012342`](../../captures/session_20260812_012342.log)
- **Next product slice:** loop length during overdub queued — [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md)
- **Archived this branch:** OpenSpec [`deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/) Phase B — gates B.1 [`231510`](../../captures/session_20260718_231510.log), B.3/B.4 [`022107`](../../captures/session_20260719_022107.log); specs in `openspec/specs/deferred-job-scheduler/`
- Memory reclaim + boot/display stack — **merged to `dev`** (PR #4)
- **Hygiene (`chore/codebase-hygiene-sprint1`):** safe debt **complete** — see [`codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md); sprint plans Status Done + [README index](../Plans/README.md#hygiene-sprint-chorecodebase-hygiene-sprint1)

## Shipped on branch (recent)

| Commit / session | Focus | Docs |
|--------|--------|------|
| `3e9253e` | DEC-029 archive + spec sync + Phase 8 closeout | DELIVERABLE_TRACKING, PHASE8_CLOSEOUT |
| `15c5750` | Playing move/length `refreshPlaybackPreview` | note_edit_playing_move_audition_bugfix |
| `7af8671` | DEC-030 §12 R1–R5 orthogonal state | note_edit_resolver_authority_contracts_refinement |
| `5af41c7` | P1 fold wrap unify; NOTELEN exit bracket; HITL serial proxy | firmware_ownership_lifetime_review |
| `85ae7d6` | P1 undo routing docs (session-gated **E:**) | LOOP_MIDI, ARCHITECTURE_RULES |
| `d87d0c6` + [`010126`](../../captures/session_20260718_010126.log) | Windowed display + queued countdown; device gate PASS | boot_load_windowed plan |
| `8b3f93e` | Sync-drain budget, clear-slot restore, post-reboot undo save | persist queue plan |
| `7a89f03` | Retire monolithic deferred save; work queue (B4) | persist queue plan |
| `b3d066f` | `stepPersistenceWorkItem` scheduler hook (B3) | persist queue plan |
| `3143ef0` | `StorageManager::admit*` API (B2) | persist queue plan |
| `736fa33` | NOTE_EDIT move/pitch bracket — mover NoteId lookup | CURRENT_WORK |
| `0f086f4` | Split-focus slot switching + loop-end commit (DEC-025) | Loops.md |
| `f6b496a` | Slot short-press deferred-restore fix | — |
| `346f7ce` | Playhead projection + undo geometry restore | — |
| `b1d3259` | Loop-scoped undo display (DEC-024 Phase 1) | loop_undo plan |
| `50ad01b` | Cold boot + USB Host defer + slot scan hardening | DEC-021/022 |
| `e40f26c` | DEC-020 Phase 4 mid-pass persistence writer | DEC-020 |
| `f0ee520` | DEC-020 Phase 3 cooperative scheduler HITL | DEC-020 |
| *(prior)* | Phase A step 3 + Phase B — one materialize per revision | handoff |
| `d635296` | Partial Phase A — PLAYING defer, REVT bounds; boot + 16-bar smoke | handoff |
| `eea4734` | Serial evidence index (archived captures) | plan |
| `894d2ea` | Runtime architecture docs + DEC-016 | Architecture/ |
| `d05e736` | Display boot/play OLED freeze | — |
| `b1260ce` | Cold buffers → external memory pool | INTERNAL_HEAP guide |

## Active OpenSpec

**2026-09-24 — the current working state.** Two changes carry this branch:

1. **`runtime-working-set-ownership`** — its native scope (P0/P1) plus **S2b.8** and **S2b.7** is complete;
   **every remaining gate is a device leg**, and all of them are blocked by the restored library being unstable
   across boots. Reported native line: full native **2,352 / 1 skipped / 0 failures** (2026-09-24, after
   S2b.7); RAM1 `variables 96,192 / code 391,080 + padding 2,136` (`padding 840` is the enforced
   floor). **No *currently eligible* native task remains open in this change** — the stage order is not
   bypassed merely because hardware evidence is blocked. (The current tree, which also carries Packet A's
   cases, reports full native **2,374 / 1 skipped / 0 failures**.)
2. **`restored-library-stability`** — **PROPOSED 2026-09-24; Packet A (observation-only firmware plus
   read-only analysis, 1.0–1.5) is COMPLETE, native- and device-verified read-only 2026-09-24; Packets B/C
   and the 4.x handoff/closeout remain open.** It owns the instability that blocks (1):
   the set of slots observed committed after a reboot is not reproducible **as measured**. It reports a
   per-slot observed state (SD payload + token, boot enumeration, request state/wait/terminal with full
   identity, committed content, selection) with **four distinct quantities** and an explicit observation
   **window**, through a **bounded on-demand observation** with a declared owner and a service point that runs
   **independently of normal load admission**, SD-read
   admission and an **invalidation/restart** rule covering request-generation change, set-epoch change **and
   content revision change** (a publication installs content under the same request identity, so it changes
   neither; content fields are never carried forward), independent of the
   settled census gates; the observed **current state** (`empty` / `populated` / `pending`) is reported
   **separately** from a **nullable historical cause** (`catalog omission`, `restore
   refusal`, `intentional eviction`, `content loss`, `ambiguous`, `unknown`) with evidence and precedence —
   `empty` is never a cause.
   One repeatable fixture inside the already-approved scratch range (track 5 slot 1, track 6 slots 1–8),
   stability proven by a **complete per-slot manifest comparison**; observation-only firmware first; any CLEAR needs **per-slot
   owner authorization**. It also carries the **S2b.7 device confirmation**, independently reportable. When
   the library is stable it **hands back** to (1) for S1 and the S2b device gates, then S3 — it does not
   absorb them.
3. **`deferred-slot-restore-liveness`** — **ARCHIVED 2026-09-24** as
   `2026-09-24-deferred-slot-restore-liveness`, with a reduced, accurately described scope. It delivered
   restore **preparation** under transport, request identity with named waits and four terminals,
   pre-publication stale-identity rejection, the post-restore census checkpoint, a content PASS, and CLEAR's
   permitted completion branch. It did **not** deliver drain-driven restore advancement (now
   `runtime-working-set-ownership` **S2b.7**, bound by
   `resumable-midi-boundary-playback-maintenance` **task 3.3**) or a runtime set change (now **S2b.8**,
   landed as external resets), and it did **not** by itself unblock CLEAR while another track keeps playing.
   Planning artifacts: the archived change's `proposal.md` / `design.md`.

The maintained-LCR and phase-3 changes below are retained as historical base context and are not
implementation scope here.

| Change | Focus |
|--------|--------|
| **`deferred-slot-restore-liveness`** | **ARCHIVED 2026-09-24 as `2026-09-24-deferred-slot-restore-liveness` with a reduced, accurately described scope — see the Active OpenSpec section above.** The text below is the pre-implementation intent, retained as history; the delivered scope is narrower (preparation under transport, request identity with named waits, pre-publication stale-identity rejection, the census checkpoint, a content PASS, and CLEAR's permitted completion branch — **not** drain-driven advancement, which is `runtime-working-set-ownership` S2b.7, and **not** a runtime set change).** Restore-liveness change: a queued slot restore becomes a liveness obligation with an explicit identity `(track, slot, requestGeneration, setEpoch)`, separate **named wait** states (`waiting_slot_owned` / `waiting_capture` / `waiting_commit_gate` / `waiting_job_capacity` / `waiting_retry`) versus exactly one **terminal** outcome (`committed` / `unnecessary` / `refused` / `superseded`), and validated rather than blind generation advance. **Scope, stated plainly: it delivers diagnosable preparation and named waits plus drain advancement where the commit gate admits — it does NOT by itself unblock CLEAR during another track's playback.** Two global gates bound it: `MidiServiceDrain::admitLoadJobChild` (commit/cleanup need a content-independent bound) and `hasLoadLoopJobCommitPending()` (true for *either* job instance, so the denial is global, not per job); both are the open clause of `resumable-midi-boundary-playback-maintenance` **task 3.3**. **The drain's caller contract is a three-way outcome** — completed / temporarily blocked / failed — with `saveState` kept as a `Completed`-only wrapper and both callers (`handleClearTrack`, `SavedSetIo`) using the detailed entry point: a blocked CLEAR **refuses by name, leaves the slot untouched, and leaves the save owed**; retention as an accepted interaction is design O7. The finite **100 ms post-load holdoff** (`LoadLoopJob.cpp`) is waited out within a bounded, MIDI-serviced budget or reported by name. The blocking drain must **advance** the restore (active and parked instances) using the **same all-tracks runtime eligibility** as the normal path — `handleClearTrack` stops only the selected track — and restore work must be charged in the drain's budget and progress detection. Modifies the `lazy-slot-hydration` capability, whose current text permits the stall. Four G2 packets (A desired-behavior fixtures → B liveness → C drain advancement + caller contract → D reconcile outcome) + closeout; Packet A's red set is excluded from the initial green gate behind a named flag. Owner: `StorageManager`; no ownership move. Evidence and non-goals: [`proposal.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/proposal.md), [`design.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/design.md) |
| **`restored-library-stability`** | **[Packet A COMPLETE and device-verified read-only 2026-09-24 — see the Active OpenSpec section above and the fourteenth-session entry; Packets B/C open, contract DEC-083.]** **PROPOSED 2026-09-24 — planning complete, `openspec validate` clean, Packet A (observation-only firmware + read-only analysis) can start immediately; nothing implemented; G2 review two rounds — BLOCKED then corrected (first four contract fixes, then two: the state-vs-cause split and the observation's ownership/admission/invalidation contract).** Owns the instability that blocks `runtime-working-set-ownership`'s device legs: the committed set is not reproducible **as measured**. Reports a per-slot **observed current state** (`empty` / `populated` / `pending`) **separately** from a **nullable historical cause** (`catalog omission` / `restore refusal` / `intentional eviction` / `content loss` / `ambiguous` / `unknown`) with evidence and precedence — `empty` is a state, never a cause; four distinct quantities with an explicit observation **window**; through a **bounded on-demand observation** with a declared owner and a service point that runs **independently of normal load admission**, SD-read admission, and an **invalidation/restart** rule covering request-generation change, set-epoch change **and content revision change** (a publication installs content under the same request identity, so it changes neither) — content fields are never carried forward; one repeatable fixture inside the approved scratch range (track 5 slot 1, track 6 slots 1–8) with any CLEAR needing per-slot owner authorization, stability proven by a **complete per-slot manifest comparison**; carries the **S2b.7 device confirmation** as an independently reportable packet; hands back for S1/S2b/S3. **Corrected findings:** `n_committed_slots` is a **window-scoped observed-completion count** (background restores included) — not the boot playback set, so the earlier explanation is withdrawn; `sets/index.bin` is `SetIndex{nextSequence}` (a saved-set folder counter, not a slot catalog); the shrinking boots report `reconcile_warnings=0`; `DIAG,lcr,skip,restore` is content-resolution; `Track N loaded state: EMPTY` appears on stable boots too; and both sweeps' `library_rows` are all-zero. |
| **`runtime-working-set-ownership`** | **Native scope (P0/P1) plus S2b.8 and S2b.7 COMPLETE 2026-09-24 — full native 2,352 / 1 skipped / 0 failures; every remaining gate is a device leg (no native task open).** The scaling-law change: runtime working sets become active-role owned, window-sized and generation-leased, so RAM scales with active roles rather than recorded slots. Landed and green: S0 verification, S1.2/S1.3-clause-2/S1.5-clause-3 fixtures, the accepted-stop latch (S4f.0/S4f.1/S4f.2/S2b.6), boot/load admission removal (S2b.1–S2b.3), S4.0/S2b.4 per-slot liveness and release, S4f.5's stop consumers, S3.0 placement pins + the post-link RAM1 budget check, and B01–B06. **Blocked on an unstable restored library — now owned by [`restored-library-stability`](../../openspec/changes/restored-library-stability/proposal.md) — for:** S1.1's per-owner census, S1.3's device slope, S1.4's undo/root baseline, S1.5's device refusal point, S2b.3's post-restore baseline, S2b.5's device gate, and the content PASS — the restored set is unstable across boots, so no deterministic single-slot capture can be produced. Envelope: 8 tracks x 1 sounding slot today (N derived, **provisional** until the sibling change lands); 16-channel banks and multi-slot-per-track are targets the law must not preclude. Governed by **Playhead-scheduled MVCC** ([NAMING.md](../Authority/NAMING.md)); invariants 1–31 plus **32–36**; `design.md` **Revisions 18–24**; **DEC-078** (every interaction accepted; finalization **per interval**; amends DEC-067/DEC-076 point 2; counter renamed **`accepted_stop_finalizer_wait`**), **DEC-079** (handoff corrections, [`IMPLEMENTATION_CONTRACT.md`](../../openspec/changes/runtime-working-set-ownership/IMPLEMENTATION_CONTRACT.md) is binding), **DEC-080** (execution packets); stages S0–S7 + **S2b** (boot/load admission), windowed playback **extracted** to `windowed-playback-generation`. **Execution order: S0/S1 → S2b → S3/S4 → S4f → S5 → decoupling**; design revisions stop unless implementation contradicts the contract. RAM1 floor currently **840**, most recent build `variables 96,192 / code 391,080 + padding 2,136`, enforced by `scripts/check_ram1_budget.py` against `scripts/ram1_budget.json` |
| **`capture-tier-a-budget-profiles`** | **Design (B1 pending)** — profile-selected, per-family **byte-budgeted** Tier-A capture with bounded bursts and edge/sample/stream semantics, so a 15–20 minute session fits the 1 MiB ring intact (measured Tier-A ~30–35 KB/s covers ~30 s). Critical reserve, per-family coverage, and a host verdict that cannot report an unconditional PASS over degraded evidence. Builds on the landed RC-MEM-7 degradation |
| **`immutable-loop-snapshot-passes`** | Phase 2 publication complete; 3.5/3.6 and 4.1–**4.6** closed; **5.1**–**5.7** SHIPPED; **6.1**–**6.4** SHIPPED (**6.4** device [`164947`](../captures/session_20260912_164947.log) 2026-09-12); next open: **4b.3**, **6.5**; §**6.6** post-closeout polish non-blocking; [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write); boot sync load still legacy; snapshot nodes pack into 32 KB slabs (`DEC-048`) |
| `loop-content-resolution` | Historical base prototype; maintained-LCR continuation not pursued on this branch |
| `loop-effective-event-source` | Historical base context; no new LCR work on this branch |
| **`hitl-cli-rebuild`** | **Phase 3 in progress** — layered `base` + `edit_full`; 3.2 bridge done; next: 3.3 device PASS |
| **`continuous-runtime-persistence`** | Phase 0–4 shipped; Phase 4 HITL **passed**; Phase 5 recovery **parked** (see firmware ownership review) |
| **`runtime-derived-representation-heap`** | M5 Steps 1–1b–2 shipped (`adoptPersistedSnapshot`); lazy load + 64+64 HITL pending |
| **`slot-selection-focus`** | **Shipped (firmware)** — orchestrator + footer extension; manual §8 pending |
| **`unified-interval-projection`** | Phases 1–5 code shipped; native PASS; **5.5 HITL deferred** (DEC-017); Phase **6.1–6.2** synced with geometry (2026-08-04) |
| `set-revision-persistence` | Set revisions, overlay browser — core shipped; loop picker **4.8–4.10** paused |
| `workspace-session-persistence` | Current workspace session model |
| `load-save-overlay-display-regression` | Overlay display fixes |
| `save-status-display` | Deferred save status on display |
| `long-loop-piano-roll-window` | 16-bar piano-roll window + overview; future `Timeline` projection consumer |
| `note-edit-tick-coordinates-and-audition` | Geometry wrap regression: tick spaces + session-store playback preview |
| `linear-loop-tick-storage` | Canonical linear storage + projection alias — prerequisite for UIP |

## Archived OpenSpec (normative specs in `openspec/specs/`)

| Change | Archived | Normative spec |
|--------|----------|----------------|
| **`recoverable-async-display-dma`** | 2026-09-25 | [`recoverable-display-dma-transport/`](../../openspec/specs/recoverable-display-dma-transport/) — 14/14; evidence in the archived [`EXECUTION_PACKET.md`](../../openspec/changes/archive/2026-09-25-recoverable-async-display-dma/EXECUTION_PACKET.md) |
| **`loop-content-history-persistence`** | 2026-08-14 | [`loop-content-history/`](../../openspec/specs/loop-content-history/) |
| **`note-edit-current-state`** | 2026-08-08 | [`note-edit-current-state/`](../../openspec/specs/note-edit-current-state/) — [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md) |
| **`edit-session-action-geometry`** | 2026-08-05 | [`edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| **`deferred-job-scheduler`** | 2026-07-19 | [`deferred-job-scheduler/`](../../openspec/specs/deferred-job-scheduler/) |

**Side fix (2026-07-03):** `note-edit-tick-coordinates-and-audition` — long-loop pitch edit regression at loop wrap; handoff [note_edit_geometry_wrap_regression_bugfix.md](../Plans/note_edit_geometry_wrap_regression_bugfix.md). HITL pending.

**Archived (2026-07-03):** `note-edit-fader-feedback-regression` → `openspec/changes/archive/2026-07-03-note-edit-fader-feedback-regression/`; normative spec `openspec/specs/note-edit-fader-feedback/`. Phase 12–13 + RC11 §8.3–8.4 deferred.

**Archived (2026-07-03):** `note-edit-stable-note-id` → `openspec/changes/archive/2026-07-03-note-edit-stable-note-id/`; normative spec `openspec/specs/note-edit-stable-note-id/`.

**Handoffs (shipped):** [note_edit_fader_feedback_next_steps_handoff.md](../Plans/archive/handoff/note_edit_fader_feedback_next_steps_handoff.md), [note_edit_stable_note_id_phase_a_handoff.md](../Plans/archive/handoff/note_edit_stable_note_id_phase_a_handoff.md)

Parked (not active): `currentset-savedset-storage-layout`

## Current constraints

- Record/overdub stop: no full validate; deferred SD save only
- Default build env: `teensy41-capture-serial`; upload only after user confirms
- Base module (encoder + 4 GPIO) capable in principle; DROID is extension only
- Governance docs do not change firmware by themselves
- Do not implement from [ROADMAP.md](ROADMAP.md) — scope is [CURRENT_WORK.md](CURRENT_WORK.md) only
- **`edit-session-action-geometry`** — **archived** 2026-08-05; normative `openspec/specs/edit-session-action-geometry/`
- **`loop-content-history-persistence`** — **archived** 2026-08-14; normative `openspec/specs/loop-content-history/`

## Current blockers

- `currentset-savedset-storage-layout` parked until revision model stable — see CURRENT_WORK

## Accepted decisions (summary)

Full log: [DECISION_LOG.md](../DECISION_LOG.md).

- Authority: `docs/Authority/` → OpenSpec → Guides → plans → code
- Gesture-first; five gestures per button
- passes[] per slot (v4+); NoteEditSession for live edit RAM
- Current auto-save; Save appends Set revision without clearing Current
- Overlay confirm: Edit short + encoder short only (DEC-001)
- Persistence RAM model: `StorageSession` on `StorageManager` — request/held/dispatched revision load (DEC-012, **shipped**)

## Current architecture notes

- Persistence owner: `StorageManager` — no parallel save Manager without reassessment (DEC-008)
- **`StorageSession`** (DEC-012): job RAM in `Internal.cpp`; overlay in `Overlay.cpp`; FSM in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`; orchestrator in `StorageManager.cpp`
- HITL serial line buffer in RAM (FLASHMEM `processHitlSerialCommands` cannot access DMAMEM on IMXRT1062)
- **Capture-serial RAM budget (2026-07-04):** note-edit overlap code in FLASHMEM (`NoteEditMem.h`); deferred `#DBG`/`REVT`/`PERF` via 96 KB PSRAM ring in `DebugSessionCapture` — see [capture_serial_ram1_recovery_extmem_debug_enhancement.md](../Plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md)
- Normative specs: `openspec/specs/revision-load/`, `storage-session-jobs/`, `storage-session-layout/`
- **Next persistence layout:** `transport.bin` / `global.bin` — not started (post DEC-012)
- **Interval projection:** `IntervalProjection` engine owns all wrap math — **`include/Utils/IntervalProjection.h`** + **`src/Utils/IntervalProjection.cpp`**; Phases 1–5.1–5.4 + 5.6 shipped (grep gate on D7 consumers); `Track.projectionCycleStartTick` + queued start at grid; `PlaybackCursor` retired
- `ButtonManager` / GPIO dormant (DEC-005)
- M8 edit + pool-budget archived to `openspec/specs/`
- **Runtime scheduling:** contract [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md); roadmap [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Interval reservation is **not** authorized. Persist `admit*` and `DeferredJobScheduler` (DEC-027) remain separate.
- **Active linkage:** [`CURRENT_WORK.md`](CURRENT_WORK.md) overdub-lag stages `P1`–`P4` feed the roadmap as pre-contract owner-boundary work (causality + arbitration ordering), not as interval-reservation implementation.


2026-09-16 task-2.3 integration checkpoint: production canonical stop preparation
now uses a separately reserved output Capture instead of mutating raw capture
chunks. Explicit-source transfer/preview APIs and full-pool admission checks are
covered by native tests. The service-paused Button A/manager/Track reentry tests
remain red: FIFO, live/head staging, preview/session/undo/snapshot ownership,
causal sources and generic-reset lifetime isolation are not implemented. Keep
2.3 and HITL open; see the active change's CONTINUOUS-CAPTURE-OWNERSHIP.md for
bounds, evidence and limitations. No firmware upload or device action occurred.
