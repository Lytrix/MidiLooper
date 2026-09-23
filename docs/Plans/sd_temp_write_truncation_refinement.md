# SD temp-write truncation — defect, fix, and evidence

**Type:** Bugfix / refinement ([DELIVERY_RULES](../Authority/DELIVERY_RULES.md) work type "Bugfix").
**Owner:** SD file-format and atomic-write owner (`CurrentSetStorage` / `StorageManager` persistence).
**Status:** Fixed and native-verified; device-verified for its own claim (boot catalog reconcile).
**Found by:** the `runtime-working-set-ownership` device session
([ARCHITECTURE-REVIEW § Device session — RWSO-24CAP](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)),
which was blocked by it.

Related decision: **DEC-022** — *Runtime bundle save tail integrity*. DEC-022 found the same class at
one site (`runtime.bundle.bin.tmp` appended after a stale gap) and fixed it *locally* by removing that
temp before writing. This refinement fixes the **class**: the write mode itself.

---

## The defect

`MidiLooper/sets/index.bin` stopped reconciling at boot, and the restored and playing sets drifted
between boots.

**Root cause.** Teensy's SD class maps the write modes like this
(`framework-arduinoteensy/libraries/SD/src/SD.h`):

```cpp
if (mode == FILE_WRITE)       flags = O_RDWR | O_CREAT | O_AT_END;  // appends
else if (mode == FILE_WRITE_BEGIN) flags = O_RDWR | O_CREAT;        // offset 0, no O_TRUNC
```

So **`FILE_WRITE` appends**, and **`FILE_WRITE_BEGIN` does not truncate** — `O_TRUNC` exists in SdFat
(`common/FsApiConstants.h`) and neither Teensy mode passes it. Writing a file "from scratch" with
`FILE_WRITE` is therefore only correct when the file is absent. Every temp write in this codebase used
`FILE_WRITE`.

`index.bin` is `[nextSequence:u32][kSaveFileToken:u32]` and the token is verified **from the end of
the file** (`verifySaveFileTokenAtPath`). `index.bin.tmp` was never removed by boot hygiene and
`atomicRenameTempFile` never removes it after a failed write. So one failed write leaves a partial temp
(for example 5 bytes), and the next write appends a whole record after it:

| step | `index.bin.tmp` | consequence |
|---|---|---|
| failed write | `[AA BB CC DD EE]` (5 B) | temp left behind, never cleaned |
| next write (append) | `[AA BB CC DD EE][seq][token]` (13 B) | — |
| rename over `index.bin` | 13 B | token is read at the wrong offset |
| next boot | — | `readSetIndexFromSd` fails → **`could not reconcile`**, forever |

The state is self-perpetuating: the write path only runs after a successful read, so a malformed index
is never rewritten.

**Second half of the defect: the harness hid it.** The native SD stub
(`test/test_support/firmware/SD.h`) implemented `FILE_WRITE` as *create-or-truncate* — the comment said
"as the write path expects". That is what the code assumed, not what the device does, so the whole
stale-temp corruption class was invisible to native coverage.

## The fix

1. **`CurrentSetStorage::openForRewrite(path)`** (`include/CurrentSetStorage.h`,
   `src/StorageManager/CurrentSetStorage.cpp`) — removes any existing file, then opens with
   `FILE_WRITE`. FLASH-resident: a rewrite is a cold SD path, never the MIDI tick.
2. **Applied to the 16 from-scratch write sites** — every temp file and the binary-copy destination, in
   `CurrentWorkspaceStorage.cpp`, `SavedSetIo.cpp`, `RevisionLoad.cpp`, `WorkspaceSave.cpp`,
   `RevisionCommit.cpp`.
3. **Deliberately NOT applied** to the sites where the current semantics are required, and a test pins
   that:
   - the in-place patches `patchLastActiveUnix`, `patchAnchorFields` and `writeEpochFileHeaderCrc`
     (seek then write into an existing file);
   - the mid-pass seal journal (`MidPassChunkPersist.cpp`, which branches on `exists` to append);
   - the crash log (`main.cpp`).
4. **`reconcileSetIndexOnSd` is now self-healing** (`src/StorageManager/SavedSetIo.cpp`): an
   unreadable index is *recoverable metadata*, because `nextSequence` is re-derived from the saved-set
   folder scan by `reconcileNextSequence` anyway. It rebuilt the index instead of returning false — which
   is what repairs a card that is **already** malformed, since the preventative fix alone cannot.
5. **The native SD stub now models the device**: `FILE_WRITE` positions at the end (append),
   `FILE_WRITE_BEGIN` positions at 0 without truncating. This is what makes the class visible.

## Evidence

**Native.** New `test/test_storage_manager_load` cases:

- `test_sd_rewrite_does_not_inherit_a_stale_temp_body` — the exact `index.bin` mechanism: a 5-byte
  stale temp, then a rewrite of one 8-byte record, asserting the file is exactly 8 bytes and
  `verifySaveFileTokenAtPath` holds. **Red-proven:** with `openForRewrite`'s remove disabled the test
  fails `Expected 8 Was 13` (prefix) — the append.
- `test_sd_in_place_patch_keeps_the_body_and_does_not_append` — guards the other side: an in-place
  patch keeps the body length and does not move the following byte.

Full native after the fix: **2,289 cases / 1 skipped / 2,288 succeeded / 0 failures** (was 2,287).

**RAM1.** `teensy41-capture-serial` **SUCCESS** with the post-link budget enforced;
`code 392,200 / padding 1,016 / locals 34,944` — **112 B better** than the pre-fix `392,312 / 904`
(single-argument call sites plus a FLASH-resident helper).

**Device** (HEAD + fix, `firmware.hex` sha256 `52d5a805…`). **Two** genuine post-fix boots — only two of
the four post-fix logs contain a boot sequence (`BOOT,load,start`); the other two opened serial without
resetting the board and so say nothing about boot — and both have **0** `could not reconcile` warnings,
against a warning on **every** pre-fix boot (`captures/rwso-24cap/session_20260923_201842.log`,
`captures/rwso-growth/boot-n2/`). The boot census is unchanged and correct: enabled set 512 → 480
(32 chunks), restore walk `phase=before 480 → phase=after 480` with `credits=0` on every row,
`track=1,slot=2` the only committed library slot, payload to `chunks_free=441`. Booting post-fix logs:
`captures/sd-fix-boot-01/`, `captures/sd-fix-24cap-nograin/`; non-boot: `captures/sd-fix-24cap/`,
`captures/sd-fix-boot-02/`.

## What this does NOT fix

**A separate, pre-existing capture-loss problem remains and still blocks the content PASS and S1.3's
slope sweep.** In the post-fix content runs the managed serial log truncates mid-run (561 and 1,732
lines where a good run produced 45,909), with **0** `RING,overflow`/`tier_a_evicted` rows — so the
firmware's ring did not shed; the capture itself stopped. The harness is capture-driven, so it never
reaches the overdub phase and reports `INCONCLUSIVE_CAPTURE (SPARSE_CAPTURE)` with a **measured**
`record loop length None` (no record was observed, though both runs did press transport → track 5 →
slot 1 → record). The same class is already recorded in the change as *"the HITL content verdict is a
coin flip … a failed content gate is retried, never reinterpreted"*, and the documented remedy is a
retry (optionally with `teensy41-capture-serial-no-grain`, which was used for one of the two attempts
and did not change the outcome).

## Round 4 follow-up: the warning is gone, but the catalog is NOT yet faithful

The objective also asked whether the catalog is **stable**, and it is not — recorded here so the fix is
not credited with more than it does.

**What works.** A save now completes on device — `[StorageManager] CurrentSet saved successfully (v6
deferred slices).` appears after a record/stop cycle, where the pre-fix failure mode was
`persistence handoff: first_missing=request` with no `PERS,request`/`SAVE,*` markers at all. And the
post-fix boot still carries **0** `could not reconcile` warnings.

**What does not.** One slot's restored content does not match its pre-reboot state:

| probe (attributed read: track select, then slot, then the CC feedback in that window) | pre-reboot | post-reboot |
|---|---|---|
| track 5 slot 1 | **24 bars (18432 ticks)** | **3 bars (2304 ticks)** |
| track 2 slot 3 (the census's committed `track=1,slot=2`) | 18 bars (13824 ticks) | 18 bars (13824 ticks) |

So `sets/index.bin` reconciliation and the save path are repaired, but **the catalog is not yet faithful
across a reboot**, which is the S1.3 prerequisite ("grow the library across boots"). The slope sweep
therefore stays blocked.

**A read-method correction, and a warning about the direct-MIDI route.** My earlier "track 5 reads 3
bars" datapoint — which I had withdrawn as a wrong-slot read, then reinstated as a *stability* symptom in
the rwso device session — is **neither**: the correct method is an **attributed** read (tap the track,
tap the slot, then take the CC-feedback line that arrives inside that window). A naive read that keeps a
serial reader running across taps mis-attributes feedback and reported `EMPTY` for a slot that in fact
held 24 bars. The attributed scan is what the table above uses.

Also recorded: **do not drive record/overdub by raw MIDI.** A direct attempt (transport → track → slot →
short `Note36` → notes → short `Note36`) mis-drove the device — the second short press started an
**overdub** (`Overdubbing started`) rather than stopping the record, and the run had to be halted with a
further stop press. The managed harness is the authoritative content creator; this was the risk that
caution existed for. The attempt wrote into the approved scratch slot (track 5 slot 1), which is the
likely origin of the 3-bar loop above; nothing outside the scratch slot was touched.

## Round 5: the content-run blocker is the clear precondition, and it is intermittent

Round 4 left "the restored content is not faithful" as the finding. Round 5 shows that conclusion needs
one correction and that a **different** defect is the operative blocker for the content PASS.

**The operative blocker, definitively localized.** Running the authoritative preset with the harness's own
narration captured (`captures/sd-fix-persist4/`) aborts at the clear step every time it aborts at all:

```
[track 5] clear selected loop (long press record/overdub)
[hitl] ACTION RECOVERY_FAILED requested=EMPTY retries=0 state=None blocked
[error] Clear aborted in firmware (deferred save); the selected slot is not a valid fixture
```

with, on device, `[StorageManager] ERROR: Persistence drain stuck after 33 iterations …` followed by
`[ERROR] Clear aborted: could not complete deferred save first`. `StorageManager::processDeferredSaveState`
returns immediately while `isTransportActiveForPersistence()`, so the blocking clear-drain cannot advance;
and the drain has no transport-gate classification, so it reports the generic `Stuck`. The harness does not
help because its `_prepare_deferred_save_before_clear` never printed "transport stop before clear long
press" — its transport-running check does not see a start that precedes its anchor, and no
`PERS,*`/`SAVE,*` marker is present in that window to make `save_active` true.

**It is intermittent, and not explained by a board reset.** The three most recent runs disagree:

| run | boot rows in log | clear aborted | recorded? |
|---|---|---|---|
| `rwso-24cap-retry2` (earlier success) | 0 | 0 | yes — 18432 ticks + overdub + save |
| `sd-fix-persist3` | 0 | 0 | no |
| `sd-fix-persist4` | 0 | 1 | no |

I first suspected the serial-open **reset** (a reset would create a fresh boot save that the harness then
blocks by starting the transport before the clear). **That hypothesis is not supported:** all three logs
have `boot_rows = 0`, so none of them reset. It is withdrawn rather than recorded as a cause.

**What the transport-stop prep does and does not fix.** Pre-stopping the transport and idling before a run
fixes the **capture sparsity** (561 lines → 25,164 in `sd-fix-persist3`) but not the clear: the harness
still starts the transport itself before it presses clear.

**So "the restored content is not faithful" (round 4) is not yet established either.** The 24-bar reading
before that reboot was taken while the device held an uncommitted, mis-driven session state; what *is*
established is that the restore is **deterministic** — two consecutive post-reboot attributed reads both
gave track 5 slot 1 = 3 bars and track 2 slot 3 = 18 bars. Write→restore fidelity is therefore
**untested**, because no recent run has recorded, and it cannot be tested until the clear unblocks.

### The transport is NOT the gate; the load path is — and the drain cannot name it

The host anchor fix below **is landed** (it repairs a real blind spot, see the next section) but it did
**not** resolve this clear failure, and that is itself the finding: the failing run had **no transport
start at all** in its log — so the transport was never running, the pre-clear step correctly did nothing,
and the drain was blocked by something else while reporting only `Stuck`:

```
[StorageManager] ERROR: Persistence drain stuck after 33 iterations (max 33, expected ~1 slice steps, ~512 SD bytes)
[ERROR] Clear aborted: could not complete deferred save first
```

`expected ~1 slice steps` with a **real 512-byte payload** means work is owed and none can be stepped. The
remaining gates, both in the **load owner**, are exactly two (`LoadLoopJob.cpp`):

```cpp
bool deferredSaveBlockedByActiveSlotLoadSd()  { return SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile(); }
bool deferredSaveBlockedByPostLoadCommitHoldoff()  { ... postLoadLoopCommitSaveHoldoffUntilMs_ ... }
```

`processDeferredSaveState` returns early on either, so the blocking clear-drain spins to its stuck budget.
The boot log shows a restore was queued and then skipped — `Queuing boot playback loop slot restore 6
pending; first 5/0`, `LoadLoopJob done 5/0`, `#CAP,…,DIAG,lcr,skip,restore` — but the failing run's own log
covers only the clear window (73 lines, no boot), so **which** gate held cannot be named from the telemetry
that exists. That absence *is* the actionable defect:

1. **Firmware (diagnostics, small — the necessary next step):** classify the gates so the drain reports
   `BlockedBySlotLoad` / `BlockedByPostLoadHoldoff` (and `BlockedByTransport` for completeness) instead of
   the generic `Stuck`. Only then can the behavioural fix be chosen on evidence rather than inference.
2. **Firmware (behaviour, needs a decision):** make the blocking CLEAR drain handle its real gates — either
   wait out a transient load gate, or declare that CLEAR does not require the workspace save when the only
   blocker is one of them. This changes CLEAR semantics and should not be invented here.

**The host anchor fix that *is* landed.** `_prepare_deferred_save_before_clear` asked "is the transport
running?" only from its own baseline anchor, so a start that preceded the anchor read as "stopped"; the
base preset starts the transport itself, so that is reachable. It now checks the whole log first, matching
the existing precedent in `_ensure_transport_stopped_before_record_arm`
(`scripts/hitl/legacy_record_baseline.py`). Host tests: `test_capture_transitions` 48/48,
`test_deferred_save_idle` 19/19, `test_hitl_serial_timing` 30/30. This is a genuine repair, but it is **not**
the unblock for the failure above — recorded that way so it is not credited with more than it does.
