#pragma once
#include <array>
#include "Globals.h"
#if defined(ARDUINO)
#include <SD.h>
#endif

namespace StorageManagerInternal {
struct RuntimeBundleRecoveryDefaults {
  uint32_t epoch = 0;
  std::array<uint8_t, Config::NUM_TRACKS> selected{};
};
enum class BootBundleRecovery { NoWorkspace, Ready, Repaired, Blocked };
// Cold setup only; no runtime publication or source-payload mutation.
bool writeRecoveryRuntimeBundle(File& file, const RuntimeBundleRecoveryDefaults& defaults);
bool validateRuntimeBundleForBoot(const char* path);
BootBundleRecovery prepareCurrentWorkspaceForBoot();
}
