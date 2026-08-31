//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

namespace PersistenceLayout {

/// SD root for all MidiLooper persistence (lowercase folder names on disk).
inline constexpr const char* kRoot = "/MidiLooper";

/// Mutable runtime workspace — separate lifecycle from sets/.
inline constexpr const char* kCurrentRoot = "/MidiLooper/current";
inline constexpr const char* kCurrentTempDir = "/MidiLooper/current/temp";

/// Immutable revision history — no mutable Current state inside sets/.
inline constexpr const char* kSetsRoot = "/MidiLooper/sets";
inline constexpr const char* kSetsArchiveDir = "/MidiLooper/sets/archive";

/// Recovery checkpoints and boot diagnostics — not inside current/ or sets/.
inline constexpr const char* kRecoveryRoot = "/MidiLooper/recovery";
inline constexpr const char* kCheckpointsDir = "/MidiLooper/recovery/checkpoints";

/// Revision snapshot files: revisions/v####.bin (extension only; prefix is v####).
inline constexpr const char* kRevisionFileExtension = ".bin";
inline constexpr const char* kRevisionTempSuffix = ".tmp";

/// Human-editable system config (JSON); runtime records use .bin fixed layouts.
inline constexpr const char* kSettingsPath = "/MidiLooper/system/settings.json";

}  // namespace PersistenceLayout
