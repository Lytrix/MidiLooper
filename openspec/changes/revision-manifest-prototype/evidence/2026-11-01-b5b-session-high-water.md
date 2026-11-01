# B.5b — PSRAM high-water of the firmware itself over an owner-rig session

Date: 2026-11-01. Image: probe env HEX `8f70c93a7b59672d…` (the default capture image plus the stopped-only
`!REVSTORE_PROBE`; the store is not active during the session). Fresh boot forced by the capture session:
`#CAP,2168002,DIAG,boot_reset,crash=0`, `BOOT,load,ok`. Log: `captures/session_20261101_170808.log`.
Probe rows: [`2026-11-01-b5b-revstore-probe-raw.txt`](2026-11-01-b5b-revstore-probe-raw.txt).

## Session

The owner rig (`scripts/hitl_midifile_chain.py`, DEC-221) at 120 BPM:
- clear scratch slot 4, then a 20-bar Record of `16ths.mid`;
- 12 overdub windows sliced from a continuous `chords.mid` stream;
- 4 bars of play, stop, then 8 bars on the device's internal clock (`--hold-clock-on-settle`).

Report: `device_record_starts=1`, `device_overdub_starts=12`, `device_cap_last=13`, 6,002 clock pulses. No pool
refusals in the log. Afterwards one MIDI Stop (the harness's own repair for a held transport) stopped the transport,
and `!REVSTORE_PROBE` read the accounting **on the same boot**; no probe had run on that boot before.

## Result

| Reading | Value |
|---|---|
| Pool total | 8,376,828 B |
| In-session peak (`[Memory] ext ... high=`) | 6,767 KB |
| **High-water since boot, after stop** (`acct_high`) | **7,146,845 B** |
| Live after stop | 7,024,681 B |
| **Free after stop** | **1,333,200 B** |
| Event-store chunks | 131 of 512 (1,001,472 B backed); 68 were loaded at boot |
| Live by block size: > 128 KB | 2,050,048 |
| ≤ 128 KB | 2,631,675 |
| ≤ 32 KB | 885,328 |
| ≤ 8 KB | 1,428,732 |

For scale: after boot and load the same card used 4.93 MB. The session added about 2.1 MB, of which only about
0.33 MB is new event-chunk backing (63 chunks). The take itself is small: a 20-bar 16ths take plus 12 short chord
windows is on the order of 1,000 rows, about 16 pages in the store's terms.

## Reading

- **The store cannot be added beside today's firmware at its full budget.** After one ordinary owner session there
  are 1.33 MB left. The B.5 budget of about 2.76 MiB (sized to the 131,072-event admission ceiling) does not fit. Even
  a 1 MiB pool would leave about 0.3 MB of reserve.
- **Most of the PSRAM is not content.** Event chunks are 1.0 MB of 7.0 MB. The rest are runtime and derived
  structures: the capture ring (1 MiB, capture builds only), boot-reserved rolling views and other view buffers
  (≤ 128 KB class, 2.6 MB), capture stagings, and companion and bridge structures. That is the overhead the
  manifest model is meant to make unnecessary. It is also why the migration has to **replace** owners rather than add
  one.
- **Chunk granularity is expensive for small passes.** Today each pass takes whole 256-event chunks (5,216 B), so
  roughly 1,000 rows of new content cost 63 chunks (0.33 MB). The same rows are about 16 KiB of 1 KiB pages.

## Consequences for stages C–E (input to the decision, not a decision)

1. **Stage C** (one playback reader from the manifest) can only use a pool sized to the content actually present, not
   the admission ceiling. Every slot's content today is at most 131 × 256 = 33,536 rows (0.51 MiB if every chunk
   were full; in practice far fewer), which fits in the 1.33 MB left, but with little reserve.
2. **Stage D** (recording into the pool) must retire `LoopEventStore` chunk backing for the same content in the same
   stage. Pages then replace chunks, at the smaller granularity, and the full budget becomes available as chunks go.
3. **Stage E** is where most memory comes back: per-slot capture stagings, rolling-view prewarm (DEC-243) and the
   merged/bridge structures. Their sizes come from the size classes above, not from content.
4. The 1 MiB capture ring exists only in capture builds; a shipping image has that much more headroom.

## Gaps

- The peak during a denser session (more slots playing, longer takes, NOTE_EDIT) is not measured. One owner-rig run
  is a lower bound on the real high-water.
- The size classes are not attributed to owners. The accounting tags (`TagViewPrewarm`, etc.) are almost all zero
  outside their bracketed scopes, so per-owner attribution needs those scopes widened or a per-owner census.
