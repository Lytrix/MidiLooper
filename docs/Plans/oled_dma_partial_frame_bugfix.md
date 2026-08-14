# OLED DMA partial first frame

**Status:** RC1–RC2 device FAIL [`014156`](../../captures/session_20260814_014156.log); RC3 shipped (device gate open)  
**Priority:** P0 — blocks DEC-035 Stage 3 `U:nn` reboot check  
**Evidence:** [`session_20260814_013908.log`](../../captures/session_20260814_013908.log), [`session_20260814_012547.log`](../../captures/session_20260814_012547.log), [`session_20260813_214817.log`](../../captures/session_20260813_214817.log)  
**Owner:** `SSD1322_API::display`

---

## Debugging boundary

```
trust: DisplayManager::update builds a full frame (DFRAME 608 notes, ~15 ms)
trust: LoadLoopJob apply is not the stall (focus apply 1.3 ms)
→ current: SSD1322 GRAM transfer (panel shows first row only)
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — still `SSD1322_API::display` |
| State transition change? | **NO** — same `display()` call sites |

## Symptom

Firmware keeps painting (`#CAP,DFRAME` 608 notes). The panel shows pixels on the top row and does not take later frames.

`SSD1322_API_send_buffer_DMA` already names this failure: touching OLED CS mid-transfer corrupts the top rows of GRAM.

## RC1 — dual CS owner on the RAM write

`TsyDMASPI0.begin(OLED_CS)` made the DMA RX-complete ISR raise CS (`MasterBase::endTransaction`) while `SSD1322_API` also holds CS for the same payload.

If that ISR ends the write before 8192 bytes are accepted, SSD1322 keeps only the first GRAM rows. `remained()==0` so firmware treats the frame as done. No `DMA transfer timeout` in `012547`.

**Invariant:** API is the sole owner of OLED CS for the RAM write. TsyDMASPI clocks data only (`kCallerOwnsCsPin`).

**Also:** first main-screen `display()` after `finishBootSetup` uses blocking `SSD1322_API_send_buffer` so GRAM is rewritten after the boot-load gap. Source framebuffer is `arm_dcache_flush`ed before the DMA memcpy.

### RC1 device FAIL — [`013908`](../../captures/session_20260814_013908.log)

| Time | What |
|------|------|
| 7.088 s | `Boot setup complete` / `usb_host,begin` |
| 7.429 s | `onBootSlotLoadComplete` invalidates caches; `SSD1322_API: blocking SPI resync` |
| — | No `#CAP,DISP` on that paint |
| 13.541 s | First `DISP` 684 notes, then `DFRAME` 608 notes / ~15 ms (DMA path) |

The one blocking-SPI frame ran on the post-invalidate paint. Every later 608-note frame used DMA. The panel stayed on frame 1. Dual-CS was not sufficient.

### RC2 device FAIL — [`014156`](../../captures/session_20260814_014156.log)

Every `display()` used blocking SPI (`SSD1322_API: SPI display (DMA bypass)`). `DFRAME` still reports 608 notes / ~11 ms. Panel unchanged.

Two owners:

1. **DMAMEM cache:** `DisplayManager` framebuffer is DMAMEM. CPU draws update the data cache; `SPI.transfer` reads physical RAM. Without `arm_dcache_flush_delete` before send, the panel can show one GRAM row while firmware reports a full frame.
2. **Boot display skip:** `skipFocusLoadForSlotSession` suppressed OLED for ~6.8 s during STOPPED background slot restore (`load_frame` 6801559 µs). One sparse paint at ~7.3 s, then no `DISP` until 13.4 s.

## RC3 — dcache flush before SPI + paint while STOPPED restoring

**Invariant:** SPI reads the same bytes the CPU just drew.

```cpp
arm_dcache_flush_delete(buffer, buffer_size);  // SSD1322_API_send_buffer
```

Boot skip gated like RC-A but only while timing-critical transport is active:

```cpp
skipFocusLoadForSlotSession =
    focusSlotRestoreWork && SlotLoadSession::isActive() && !captureActive &&
    timingCriticalTrackActive;
```

STOPPED boot/background restore keeps painting every `DISPLAY_UPDATE_INTERVAL`. RECORDING still paints (RC-A). PLAYING + slot load may still skip for timing.

## Device gate

1. Boot to main piano roll on track 0 slot 4 (68-bar).
2. Serial contains `SSD1322_API: SPI display (DMA bypass)`.
3. `#CAP,DISP` lines appear during the 6–7 s boot restore window (not only after 13 s).
4. Full roll visible; sidebar `U:nn` / LOOP EDIT updates.
