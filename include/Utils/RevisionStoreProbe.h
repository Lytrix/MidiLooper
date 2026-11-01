//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// revision-manifest-prototype B.4: on-target cost of the prototype `RevisionStore::Store`, not wired to playback.
// Built only with REVISION_STORE_PROBE=1 (env `teensy41-capture-serial-revstore-probe`); triggered by the stopped-only
// serial command `!REVSTORE_PROBE`. Prints plain `REVSTORE,...` rows straight to USB.

namespace RevisionStoreProbe {
void run();
}
