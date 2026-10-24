# 1.3 — shipping activation without removals

**Change:** exactly two files; no route, branch, fallback or emitter is removed.

- `platformio.ini` — `teensy41-capture-serial` now defines
  `-D PLAYBACK_WINDOW_ROLLING_ENABLED=1` and the activation marker
  `-D PLAYBACK_WINDOW_ROLLING_SHIPPING_ACTIVATED=1`. The lane flag is the rollback; the marker is
  deliberately **not** `PLAYBACK_WINDOW_ROLLING_QUALIFICATION`, so the shipping image does not carry the
  qualification receipts/instrumentation.
- `include/RuntimePlaybackView.h` — the activation guard accepts either named marker and still rejects
  any other capture build that turns the lane on:
  ```
  #if PLAYBACK_WINDOW_ROLLING_ENABLED && !defined(PIO_UNIT_TEST_NATIVE) &&
      !(defined(SESSION_CAPTURE) &&
        (defined(PLAYBACK_WINDOW_ROLLING_QUALIFICATION) ||
         defined(PLAYBACK_WINDOW_ROLLING_SHIPPING_ACTIVATED)))
  #error "rolling playback requires native, a qualification build, or the explicit shipping activation"
  #endif
  ```
  The guard is the explicit boundary the spec asks for, not a silent gate removal.

## Builds

| env | RAM1 | result |
|---|---|---|
| `teensy41-capture-serial` (shipping, rolling ON) | variables 96,192 / ITCM 393,216 (**code 392,184 + padding 1,032**) / locals 34,880 | SUCCESS |
| `teensy41-capture-serial-window-qualification` | identical line | SUCCESS |

Both are above the reviewed `padding >= 840` floor (192 B headroom); no re-base. The shipping activation
costs the same lane code as qualification (the qualification-only receipts are off).

- Shipping activation HEX SHA-256: **`2fab59abbb33a0ebc2c1da2ef78e6335feb44186f7ee966f5bee74bc5d122168`**
  (artifact stored at `captures/device-images/shipping-rolling-2fab59ab.hex`, gitignored; SHA-256 identifies it).
- Full native: **3,443 / 7 skipped / 3,436 succeeded**; storage **63/63**; RWO **241/241**.

## Compatibility state

Every compatibility family remains compiled and reachable as the named fallback: prepared pair, fixed/bridge,
first-Record/Record-return, paged emission selection, merged fallback + overdub echo. The activation does not
touch them; their removals are 2.1–2.5, each with its own RED/GREEN and device requalification.

## Rollback

`PLAYBACK_WINDOW_ROLLING_ENABLED=0` on the shipping env (the 1.1 cold reference, HEX `dab405b3…`) restores the
legacy route exactly; this remains the accepted rollback until the last family is removed. The 1.5 drill will
return 0, build/flash and record the pair.

## Next

1.4 — flash this HEX, run the registered route matrix, and compare ordered MIDI + ledger to the 1.1 cold
reference; a fallback pass is not a parity pass.
