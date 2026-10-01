# EVIDENCE — arranging S2b.7(a)'s three conjuncts at one drain edge (2026-10-01 device runs)

**Goal.** S2b.7(a) asks for a fixture that yields, at **one drain edge**: *a save owed* + *a restore holding the SD
session* + *another track playing*, with `deferredSaveBlockedByActiveSlotLoadSd()` true. **Firmware unchanged** (no
build, no flash); five device legs on the already-installed image, plus one soft `!REBOOT` per leg.

## 1. The mechanism, read from the code (this is what the legs then tested)

* **Who can reach the blocking drain.** Two call sites only: `MidiButtonActions.cpp` (the **CLEAR** long-press) and
  `SavedSetIo.cpp:365` (the save-new-set pre-flight). Both enter only when `hasDeferredSaveWork()` is true — so a
  drain edge requires a **save owed**, which is why a leg where the take's save had already run produces no row at
  all.
* **What bit 1 means.** `deferredSaveBlockedByActiveSlotLoadSd()` is
  `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()` (`src/StorageManager/LoadLoopJob.cpp:855`). It is
  about a **slot-load session or an open SD file**, *not* about queued `RRQ` requests — which is why a run can show
  hundreds of `RRQ` rows and still read `flags=0` at the edge.
* **The fresh-boot requirement.** The restore work that makes a session active comes from the **boot** sweep. A
  device left running has none (the first attempt produced `RRQ after=0` and no row), so each leg needs a
  **soft `!REBOOT`** (`StorageManagerHitlSerial.cpp`, `SCB_AIRCR = 0x05FA0004`; transport-gated, no flash, the port
  re-enumerates). The sweep's window on this device is **≈ 3–24 s** after boot.

## 2. What the legs observed

| run | edge | save owed | restore at the edge | another track sounding |
|---|---|---|---|---|
| `s2b7a-drain-shapeb-01` | no drain (`RRQ=0`, `boot banner: False`) | – | **no restore at all** | device left running from a previous state |
| `s2b7a-drain-shapeb-02` (step-1 CLEAR, 44.1 s) | `Clear waiting…` → drain ran | **yes** | none (sweep over at 22 s) → `flags=0`, `ok` | – |
| `s2b7a-drain-shapeb-03` (27.25 s) | **no drain** | **no** — `hasDeferredSaveWork()` false | – | take still open |
| `s2b7a-drain-shapeb-04` (11.120 s) | `Clear waiting…` → drain ran | **yes** | none at the entry → `flags=0`, `ok` | cleared track playing, stopped by its own clear |
| `s2b7a-drain-shapeb-05` (12.155 s) | `Clear waiting…` → drain ran, **refused by name** | **yes** | **yes — see below** | **yes** (notes across the edge) |

### The one edge that holds all three (`-05`, device time 12.155–12.158 s)

```
[12.155] [INFO] Clear waiting for deferred save completion
[StorageManager] Draining persistence work queue to SD card...
#CAP,12158059,RRQ,5,6,0,0,terminal,none,committed        <- 12.1581 s: a restore on track 6 Loop 7 completes
#CAP,12158504,PERS,drain_progress,24,24,0,post_load_holdoff
[12.158] [ERROR] Clear refused: deferred save temporarily blocked by post_load_holdoff; slot left untouched, save still owed
```

* **A save owed** — `Clear waiting for deferred save completion` (the `hasDeferredSaveWork()` branch), and the
  refusal leaves it owed (`save still owed`).
* **A restore holding the SD session** — the restore request **`RRQ,5,6`** (0-based track 5 ⇒ track 6, slot 6 ⇒
  Loop 7) reaches `terminal,none,committed` **3 ms before** the drain row, and the drain reports the A3.5 reason
  **`post_load_holdoff`** — the commit holdoff that exists *because* a slot load just held the session. The clear is
  then **refused by that name** and the slot is left untouched. This is also the outcome the earlier slice recorded
  as *not occurring*: a `TemporarilyBlocked` drain with a **named** wait, on device.
* **Another track sounding at the edge** — the device's own MIDI out carries notes on channel 6 immediately before
  and after the drain: `MO,144,6,58` at 12.022 s, then `MO,144,6,48` 12.162, `MO,128,6,48` 12.277, `MO,144,6,49`
  12.344, `MO,144,6,50` 12.535.

## 3. What this run does **not** establish (stated, not glossed)

* **`flags` bit 1 is 0 at the drain entry in `-05`.** The session had already moved into its post-load holdoff, so
  the predicate read false while the drain *named* the same event. The literal clause — bit 1 true — is observed in
  the **earlier** run (`captures/s2b7-drain-probe-01`, `PERS,drain_progress,795,801,3,ok`), where no track was playing
  at that instant.
* **The sounding channel (6) is the clear target's own track.** The leg clears track 6 Loop 8, so ch6 notes are not
  cleanly *another* track; what is proven is that a **different slot's** restore (Loop 7) was completing at the edge.
  The boot rows show tracks 4/5/7 resume playing, but none of them emits a note inside that second, so "another
  track" rests on the boot state plus the absence of a stop for those tracks rather than on a note row.
* **Therefore S2b.7(a) stays partial.** The precise remainder is one run holding **bit 1 true** *and* a *different*
  track's audible notes at the same edge. The blocker is not a missing surface: it is that the sweep's sessions and
  the other tracks' sounding content are on independent duty cycles, and the leg that clears a *reload* target puts
  the clear on the very track whose content would otherwise supply the third conjunct. The named next arrangement is
  to record a **dense scratch take first, leave it playing**, and only then clear a second slot inside the sweep
  window (the leg run here ordered the scratch record *after* the clear, at 14.6 s).

## 4. Device-state consequences (reported because they are not reversible)

* **Track 6 Loop 8 (the authorized target) no longer has its card payload.** The user authorized clearing it; in
  `-02` the step-1 drain **completed** (`State saved successfully (v4)`) after that clear, which persists the
  cleared state, and `-05` cleared it again with saves completing afterwards (`CurrentSet saved successfully`).
  The 4-bar sweep take that occupied it is gone; no other slot was cleared.
* **Track 5 Loop 1** was re-recorded twice as the scratch slot (its role from the earlier probe). Expected.
* One soft `!REBOOT` per leg; no flash, no firmware change, no build.
* The first leg (`-01`) ran against a device left in a previous session's state and produced no drain row; that is
  recorded rather than retried silently.
