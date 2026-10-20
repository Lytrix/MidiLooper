//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

/// "Last row emitted with…" state for the two gate-row transition records.
///
/// **Why the magic exists (measured on device, 2026-10-20).** Both tables live in `DMAMEM`: the Teensy core linker
/// script collects `*(.dmabuffers)` into `.bss.dma (NOLOAD)`, which has **no initial image**, and the startup code
/// clears only ordinary `.bss`. Default member initialisers therefore never run — the same defect class as DEC-121
/// (the capture journal) and the trap 5.3 had to follow for the receipt sampler. Here it is the null check on `exit`
/// that breaks: a queued slot switch prepares its launch through `preparePlaybackLaunchForSlot`, which is the first
/// row for that (track, slot) of the boot, and the compare then dereferenced stale OCRAM bytes as a C string —
/// DACCVIOL at `0xD2E3A540`, `PC 0x59768` (`strcmp`), auto reboot, four times for four switches on two images
/// (`openspec/changes/windowed-playback-generation/evidence/2026-10-20-s37d-device-switch-crash`). "Never written"
/// is therefore proven by a **written magic** — the pattern `Diagnostics::DiagLastRecordSlot` and 5.3's sampler
/// arming already use — and never by a member initialiser.

#include <cstdint>

namespace GateRowLastState {

/// `'LGL1'` / `'RGL1'`: the first row for an entry stamps it. A mismatch means the entry has not been written since
/// boot (or holds another image's bytes), so it must be reset before anything is compared.
inline constexpr uint32_t kLaunchMagic = 0x4C474C31u;
inline constexpr uint32_t kRollingMagic = 0x52474C31u;

/// One (track, slot) target of the launch gate row.
struct LaunchState {
  uint32_t magic = 0;
  const char* exit = nullptr;
  uint32_t loopLengthTicks = 0;
  uint16_t flags = 0;
  uint8_t launchPreparation = 0;
};

/// One track of the rolling (`win_gate`) gate row. `seen` is part of the state and is stale in the same way: a stale
/// non-zero `seen` with a coincidentally matching key used to reach the same `strcmp` on the first row of a boot.
struct RollingState {
  uint32_t magic = 0;
  uint16_t key = 0;
  uint8_t slot = 0;
  uint32_t loopLengthTicks = 0;
  uint8_t requestResult = 0;
  const char* exit = nullptr;
  bool seen = false;
};

/// Reset `state` when it has never been written since boot and report whether it did. Runtime assignment is what
/// applies the member defaults here; the initialisers in this header are declarations, not boot-time behaviour.
template <typename State>
inline bool ensureInitialized(State& state, uint32_t magic) {
  if (state.magic == magic) {
    return false;
  }
  state = State{};
  state.magic = magic;
  return true;
}

}  // namespace GateRowLastState
