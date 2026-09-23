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

**Device** (HEAD + fix, `firmware.hex` sha256 `52d5a805…`). Three post-fix boots, **0**
`could not reconcile` warnings — against a warning on **every** pre-fix boot
(`captures/rwso-24cap/session_20260923_201842.log`, `captures/rwso-growth/boot-n2/`). The boot census is
unchanged and correct: enabled set 512 → 480 (32 chunks), restore walk `phase=before 480 → phase=after
480` with `credits=0` on every row, `track=1,slot=2` the only committed library slot, payload to
`chunks_free=441`. Post-fix logs: `captures/sd-fix-boot-01/`, `captures/sd-fix-24cap/`,
`captures/sd-fix-24cap-nograin/`.

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

**Next step:** retry the content run to a clean capture — the fix's own claim does not depend on it, but
the content PASS and the slope sweep do.
