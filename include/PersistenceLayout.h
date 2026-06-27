//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

namespace PersistenceLayout {

/// SD root for all MidiLooper persistence (lowercase folder names on disk).
constexpr char kRoot[] = "/MidiLooper";

/// Mutable runtime workspace — separate lifecycle from sets/.
constexpr char kCurrentRoot[] = "/MidiLooper/current";
constexpr char kCurrentTempDir[] = "/MidiLooper/current/temp";

/// Immutable revision history — no mutable Current state inside sets/.
constexpr char kSetsRoot[] = "/MidiLooper/sets";
constexpr char kSetsArchiveDir[] = "/MidiLooper/sets/archive";

/// Recovery checkpoints and boot diagnostics — not inside current/ or sets/.
constexpr char kRecoveryRoot[] = "/MidiLooper/recovery";
constexpr char kCheckpointsDir[] = "/MidiLooper/recovery/checkpoints";

/// Revision snapshot files: revisions/v####.bin (extension only; prefix is v####).
constexpr char kRevisionFileExtension[] = ".bin";
constexpr char kRevisionTempSuffix[] = ".tmp";

/// Human-editable system config (JSON); runtime records use .bin fixed layouts.
constexpr char kSettingsPath[] = "/MidiLooper/system/settings.json";

}  // namespace PersistenceLayout
