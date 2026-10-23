# Cold shipping baseline — task 1.1 (2026-10-24)

**Purpose.** Freeze the exact shipping (`PLAYBACK_WINDOW_ROLLING_ENABLED=0`) image and the route evidence that the
activation parity run (1.4) and every family-removal slice compare against.

## Image identity

- Source: `2ee3dcb7` (branch `feature/unified-rolling-playback-path`).
- Env: `teensy41-capture-serial`; `PLAYBACK_WINDOW_ROLLING_ENABLED=0`; legacy emitters authoritative.
- Build: **SUCCESS**, `variables 96,192 / ITCM 393,216 (code 391,752 + padding 1,464) / locals 34,880`,
  floor 840 (624 B headroom).
- HEX SHA-256: **`dab405b31f3e18886f1a431e32c287a42a4eed1d2d455dafe79ce04759f90079`** (`firmware.hex`).
- This binary is byte-identical to the default image used by the 2026-10-24 device campaign (`e9628c55` source), so that
  campaign's default legs are the cold reference rows below.
- Rollback partner (qualification image): `cbd4703d661de0b2b91848ed224c7992c24155dce799ddfe7b0fecde79a273d6`
  (`teensy41-capture-serial-window-qualification`, `ROLLING=1`).

## Cold-reference route matrix

| Route | Cold evidence (default image) | Verdict / note |
|---|---|---|
| Base record, two overdubs, post-stop undo/redo, display, persistence | `2026-10-24-s315f-closeout-base-default-host-2` | **PASS** (host 120; `max_pers` 37,179/40,780 µs) |
| Base, internal clock | `2026-10-24-s315f-closeout-base-default-internal` | **PASS** (39,430/43,253 µs) |
| Live + frozen Record / stop return | rolling Record campaign is **qualification-only**; the default leg is the base record | default base record PASS; no rolling-leg cold run |
| Overdub | base legs (both overdub passes) | **PASS** (sound-path content; the default has no overlay window) |
| NOTE_EDIT | `2026-10-23-s315f-default-noteedit-probe-22` / `…-noteedit-probe-02` | content pair PASS; the default has no windowed preview route (ROLLING=0) |
| Switch / inbound slot | — | **no cold switch run** (the mixed scenario ran on qualification only) |
| Undo/redo | base legs (`undo_redo_after_last_overdub`, display undo/redo) | **PASS** |
| Dense / restored (24 bar) | `2026-10-24-s315f-closeout-dense-default-24bar` | **leg PASS**; lane/snapshot terms not applicable on the cold image |
| Internal vs external clock | base host + internal legs | **PASS** |
| Persistence/display contention | base legs | PASS; display stall already falsified (max 2.3 ms) |
| RAM high-water | base legs' `#HEAP_STATS` | RAM2 floor **147,456 B**, PSRAM peak **3,972,024 B** (default) |

## What the baseline does not claim

- No cold switch evidence; 1.4 must either run the switch on the cold image or name it as a gap (the mixed oracle was
  setup-limited in the reader change).
- No rolling Record cold leg; the default's legacy record path is the reference for the base record content.
- This is the legacy reference, not a claim that the shipping image is the final product path.
