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

### The decisive edge (`-10`): all three conjuncts **and** the literal bit-1 clause at one instant

The `-05`/`-06` legs pressed CLEAR *after* their MIDI-clock stream had ended, so the transport lost its clock and a
sounding track stopped just before the edge. Driving the clock in a **background thread through the clear** fixes
exactly that, and `captures/s2b7a-drain-shapeb-10.log` (24.318 s) then holds everything at one edge:

```
[22.020] Selected track 6
[22.563] Loop 8: Start Recording                     <- the clear target, on track 6
[24.318] Button press: Record/Overdub (long)         <- CLEAR, with the clock still running
[24.318] Clear waiting for deferred save completion  <- (1) a save owed
[StorageManager] ERROR: Persistence drain blocked by transport_active after 0 iterations ...
[24.318] Clear refused: deferred save temporarily blocked by transport_active; slot left untouched, save still owed
#CAP,24318924,PERS,drain_progress,665,665,2,transport_active
```

* **(1) A save owed** — `Clear waiting for deferred save completion` (the `hasDeferredSaveWork()` branch), and it
  stays owed (`save still owed`).
* **(2) A restore holding the SD session, by the literal clause** — the row's `flags=` **`2`** is bit 1:
  `deferredSaveBlockedByActiveSlotLoadSd()` **true at the drain entry** (its definition is
  `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()`, `LoadLoopJob.cpp:855`). The same run's restore
  traffic is adjacent (`RRQ,4,4,running` at 20.26 s, `RRQ,4,5,running` at 20.34 s — 0-based track 4 ⇒ track 5).
* **(3) Another track playing** — the drain's own verdict is **`transport_active`**, the name its gate reports for
  this case, and in the same window the *scratch* track is live while the clear targets a **different** track:
  `Selected track 5` at 19.722 s → `Loop 1: Start Recording` at 20.221 s, then the clear on track 6 Loop 8 at
  24.318 s. The transport cannot have been a stopped artefact: a background clock thread kept driving it through
  the press.

Scope of the third conjunct's evidence, stated rather than inflated: it is the **drain's named gate** plus the
same-window activity of a different track — not a note row, because this run's capture ring **shed** twice
(`RING,shed,2`) and no track note rows survive in it. What the item asks for is the *condition* being observed
rather than assumed, and the device's own refusal reason is the strongest available form of that.

## 3b. Clause status after `-10`

All three conditions and the literal `deferredSaveBlockedByActiveSlotLoadSd()` clause are observed **at one edge**,
with no external advancement performed inside any run (the legs only press and clock; nothing touched the restore or
the save state). Corroboration: `-01` (bit 1 + progress moved inside the call), `-05` (a restore terminal 3 ms
before the drain, the drain naming `post_load_holdoff` and refusing by name with the slot untouched) and `-06` (the
same named refusal, with another track's notes 1.75 s before the edge). The earlier slice's note that a
`TemporarilyBlocked` drain with a **named** wait had not occurred is now superseded: it occurs, with three different
names (`post_load_holdoff`, `transport_active`, and the `waiting_commit_gate` name already in the corpus).

## 3. What this run does **not** establish (stated, not glossed)

* **`flags` bit 1 is 0 at the drain entry in `-05`.** The session had already moved into its post-load holdoff, so
  the predicate read false while the drain *named* the same event. The literal clause — bit 1 true — is observed in
  the **earlier** run (`captures/s2b7-drain-probe-01`, `PERS,drain_progress,795,801,3,ok`), where no track was playing
  at that instant.
* **The sounding channel (6) is the clear target's own track.** The leg clears track 6 Loop 8, so ch6 notes are not
  cleanly *another* track; what is proven is that a **different slot's** restore (Loop 7) was completing at the edge.
  The boot rows show tracks 4/5/7 resume playing, but none of them emits a note inside that second, so "another
  track" rests on the boot state plus the absence of a stop for those tracks rather than on a note row.
* **What `-05` alone did not settle — superseded by the `-10` edge above.** At `-05` the bit-1 clause read 0 (the
  session had reached its post-load holdoff) and the sounding channel was the clear target's own track. Both were
  fixed by driving the clock through the clear on a *different* target, which is what `-10` does: there bit 1 is
  **1**, the refusal names `transport_active`, and the scratch track is the live one. The paragraphs below are kept
  as the record of what the earlier attempt could and could not show.

## 4. Device-state consequences (reported because they are not reversible)

* **Track 6 Loop 8 (the authorized target) no longer has its card payload.** The user authorized clearing it; in
  `-02` the step-1 drain **completed** (`State saved successfully (v4)`) after that clear, which persists the
  cleared state, and `-05` cleared it again with saves completing afterwards (`CurrentSet saved successfully`).
  The 4-bar sweep take that occupied it is gone; no other slot was cleared.
* **Track 5 Loop 1** was re-recorded twice as the scratch slot (its role from the earlier probe). Expected.
* One soft `!REBOOT` per leg; no flash, no firmware change, no build.
* The first leg (`-01`) ran against a device left in a previous session's state and produced no drain row; that is
  recorded rather than retried silently.
