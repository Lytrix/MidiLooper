# Device evaluation — DEC-234 case 1 (inside-final-bar), 2026-10-31

**Verdict: PASS for the case-1 boundary.** The committed Record pass keeps the final bar and its exact pair; no
wrapped `@32` phantom and no orphan row.

## Identity

- Branch `feature/unified-rolling-playback-path`, working tree (DEC-234 fix + parked display-source checkpoint +
  pre-existing dirty work); no commit.
- Image HEX `firmware.hex` sha256 **`634e0be9c9d5f7ae22ce2a63af66ace13c30b1cbdd71795bff28b1818e0a19a1`**.
- Boot proof: `DIAG,boot_reset,crash=0` and `BOOT,load,ok` in
  `captures/case1-inside-final-bar/session_20261031_114514.log`; reset acknowledged, boot settle
  `boot_loaded after 3.8 s`.
- **Upload note:** the default-env RAM1 post-script was red from the unrelated DEC-232 display-investigation
  DTCM transfer. For this owner-authorized evaluation the post budget script was temporarily removed from
  `[env:teensy41-capture-serial]`, the image was built and uploaded with `teensy-gui`, and `platformio.ini` was
  restored immediately (`git diff` clean). This is an **evaluation image**, not an acceptance image; the budget
  is not re-based.

## Fixture

`captures/case1-inside-final-bar/record.mid` (PPQ 96, one bar): NoteOn tick 76 (clock 19), NoteOff tick 92
(clock 23). With the record start at device tick 120, the stored pair phases are `32..64` per bar — the last
NoteOff phase is `3*768 + 64 = 2368`, rem **64** inside the final bar, below the 128-tick grace.

`overdub.mid` (one bar): NoteOn tick 288, NoteOff tick 336 (pitch 72). One 4-beat overdub window.

Run: `scripts/hitl_midifile_chain.py --record-bars 4 --overdub-count 1 --overdub-min-beats 4
--overdub-max-beats 4 --settle-beats 1 --play-bars 1 --stop-bars 4 --hold-clock-on-settle`.
Preconditions: `mode=LOOP_EDIT` from `disp_src`; clear verified on attempt 1; view prewarm `ready` after 2.394 s.

## Device result

Record stop (`session_20261031_114514.log`):

```text
RECS,stopToStopped,3,3048,120,2928,3072,0     raw=2928, final=3072 (4 bars)
```

Committed dump (`captures/case1-inside-final-bar-content-eval/serial.log`, read-only `--dump-only`):

```text
CAPTURE_CONTENT,begin,track=8,slot=3,length=3072
CAPTURE_CONTENT,rows,record,0,8
CAPTURE_CONTENT,rows,overdub,0,2
CAPTURE_CONTENT,rows,effective,0,10
CAPTURE_CONTENT,event,record,0,32,144,1,60,100,1
CAPTURE_CONTENT,event,record,0,64,128,1,60,0,1
CAPTURE_CONTENT,event,record,0,800,144,1,60,100,2
CAPTURE_CONTENT,event,record,0,832,128,1,60,0,2
CAPTURE_CONTENT,event,record,0,1568,144,1,60,100,3
CAPTURE_CONTENT,event,record,0,1600,128,1,60,0,3
CAPTURE_CONTENT,event,record,0,2336,144,1,60,100,4
CAPTURE_CONTENT,event,record,0,2368,128,1,60,0,4
```

Checks:

- loop length **3072** = 4 bars; the final bar is kept;
- Record pass = exactly 8 rows / 4 pairs, NoteIds 1..4, each On/Off at its original phase;
- last pair `2336..2368` (bar 3 + 32/64), rem 64 < grace 128;
- **no** duplicate/wrapped NoteOn at phase 32 (only NoteId 1's legitimate row), no orphan On;
- effective = record + overdub exactly (8 + 2 = 10).

## Playback parity — PASS with the target track soloed

The first playback capture served another track's 79..84 rows while the target track was silent, so the test
now isolates the target (track 8, 1-based) for the capture:

- `scripts/hitl/actions/slots.py`: `press_track_solo()` long-press helper (`SOLO_TRACK`, exclusive; the same
  gesture clears solo).
- `dump_and_capture.py`: `--solo-track N` sends that gesture before playback and clears it after.
- `verify_playback_content.py`: when the track serves its output on a different channel than the captured
  channel (track 8 serves MIDI channel 8; the take was captured on 1), the served rows are normalized to the
  dump channel for the content comparison; `--midi-channel 8` selects the served channel.

Run (`captures/case1-playback-final`, loop 3072, effective 10 rows):

```text
content-review.json: result=PASS, storage_matches_effective=true, cycle_cut=anchor
cycle0: rows=10 missing=0 extra=0 inventory_equal=True exact_order=True
cycle1: rows=10 missing=0 extra=0 inventory_equal=True exact_order=True
solo toggled (exclusive) at the start and cleared after the capture; no RING,overflow
```

The served content matches the committed dump exactly. The exact-boundary take's playback capture
(`captures/case1-exact-boundary-playback`) is `PASS_WITH_KNOWN_RESIDUALS`: the only missing row per cycle is
the documented terminal `NoteId=0` Off at `length - 1`, with zero unexplained rows and zero extras.

## Still open

- Exact-boundary device control (last event exactly on the bar line → no extra bar). Native
  `test_record_stop_trims_at_an_exact_bar_line` is green; the device variant is not run.
- Representative multi-overdub chain with the case-1 Record shape (this run has one minimal overdub window).
- Playback parity with an isolated target slot.

## Representative 8-bar / 12-window chain (same image)

The same record pattern, `--record-bars 8`, `chords.mid` for 12 overdub windows, `--stop-bars 8`,
`--hold-clock-on-settle` (log `captures/case1-chain/session_20261031_114951.log`).

Record stop:

```text
RECS,stopToStopped,3,6120,120,6000,6144,0      raw=6000, final=6144 (8 bars)
```

Committed dump (`captures/case1-chain-content/serial.log`):

```text
CAPTURE_CONTENT,begin,track=8,slot=3,length=6144
CAPTURE_CONTENT,rows,record,0,16
CAPTURE_CONTENT,rows,effective,0,146
record: 32/64(id1) 800/832(id2) 1568/1600(id3) 2336/2368(id4)
        3104/3136(id5) 3872/3904(id6) 4640/4672(id7) 5408/5440(id8)
```

Checks:

- loop length **6144** = 8 bars; last Record pair `5408..5440` (bar 7 + 32/64, rem 64 < 128);
- Record pass = exactly 16 rows / 8 pairs, NoteIds 1..8, no wrapped `@32` duplicate, no orphan;
- effective = 146 rows across 14 committed overdub passes (12 windows; two wrapped passes);
- survives the full 12-window chain: the sealed Record vector was not mutated by any overdub commit.

Playback parity for this chain is still not claimed: the first playback capture served another track's
79..84 rows, so the target slot must be isolated (muted tracks or the managed preset) before the
`content-review.json` inventory check can pass.

## Exact-boundary control (same image)

Pattern `captures/case1-exact-boundary/record.mid`: NoteOn at clock 13 (phase 752), NoteOff at clock 15
(phase 0 = the record start tick). Five record bars, one 4-beat overdub window (log
`captures/case1-exact-boundary/session_20261031_115307.log`).

```text
RECS,stopToStopped,3,744,120,3696,3072,0       raw=3696 (transport quantize would be 5 bars)
CAPTURE_CONTENT,begin,track=8,slot=3,length=3072
CAPTURE_CONTENT,rows,record,0,8
752/768(id1) 1520/1536(id2) 2288/2304(id3) 3056/3071(id4, Off NoteId=0)
```

Checks:

- length **3072 (4 bars)**, not 3840: the exact bar-line last event does **not** gain an extra bar;
- the boundary note is closed by the documented loop-boundary synthetic Off at `length - 1` (3071,
  `NoteId=0`), not left as an orphan;
- the three interior pairs keep their exact phases.

## RAM1 acceptance gate — resolved (2026-10-31)

The 4,096 B DTCM transfer was the DEC-233 diagnostic string family in `MidiButtonActions.cpp` crossing the
`_VectorsRam`/periodictable align edge. The linker now carves that TU's read-only strings to FLASH
(`*MidiButtonActions*.o(.rodata.str* .rodata.*.str*)`). The remaining +32 B was the parked display-source
fix's `gDrawSourceCommittedPassGathers` counter; it is now `DMAMEM` (RAM2) in
`DisplayNoteWindowGather.cpp`, so the approved placement is restored without a budget re-base.

- Working tree (DEC-234 + DEC-233 + display fix): **SUCCESS** at `variables 96,192`, `locals 34,880`,
  ITCM `391,336 + 1,880` (1,040 B above the floor).
- Display-only staged state (HEAD + parked display fix + DMAMEM counter): **SUCCESS** at `variables 96,192`,
  `locals 34,880`, ITCM `391,976 + 1,240` (400 B above the floor). The display checkpoint is now independently
  commit-ready.
- Final image: `firmware.hex` sha256
  `1f9043054bab7340599331276cdcd4d5d6fdefa462e6393fc6c7c0068a9ac72d`, uploaded and boot-verified after the
  board was reconnected (`DIAG,boot_reset,crash=0`, `BOOT,load,ok`, `BOOT,ram1,192512`; no `RING,overflow`).
  The persisted case-1 content restored on the new image: dump `length=3072`, record 8 rows / 4 exact pairs,
  effective 10 rows. (The first upload attempt failed with "No Teensy boards were found"; a PROGRAM/reconnect
  recovered the board. No firmware behavior changed between `634e0be9…` and `1f9043…`: only the counter's
  section and the FLASH-carved diagnostic strings moved.)
