#pragma once
// Host tooling uses the same cold firmware writer, not a second file-format implementation.
#include "RuntimeBundleRecovery.h"
namespace OfflineRecovery {
using RuntimeBundleDefaults = StorageManagerInternal::RuntimeBundleRecoveryDefaults;
inline bool writeRuntimeBundle(File& file, const RuntimeBundleDefaults& defaults) {
  return StorageManagerInternal::writeRecoveryRuntimeBundle(file,defaults);
}
}
