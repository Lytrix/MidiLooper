#define PIO_UNIT_TEST_NATIVE
#include "RuntimePlaybackView.h"
#include <cstdio>
int main() {
  printf("MidiEvent=%zu PlaybackPassEventOrigin=%zu PlaybackBridgeRecord=%zu\n",
         sizeof(MidiEvent), sizeof(PlaybackPassEventOrigin), sizeof(PlaybackBridgeRecord));
  printf("FixedPlaybackEventStorage=%zu RuntimePlaybackView=%zu RuntimePlaybackViewSet=%zu\n",
         sizeof(FixedPlaybackEventStorage), sizeof(RuntimePlaybackView), sizeof(RuntimePlaybackViewSet));
  printf("kViewCount=%zu kEventsPerView=%zu kBridgeCount=%zu kRecordsPerBridge=%zu kCaptureStagingEvents=%zu\n",
         RuntimePlaybackViewConfig::kViewCount, RuntimePlaybackViewConfig::kEventsPerView,
         RuntimePlaybackViewConfig::kBridgeCount, RuntimePlaybackViewConfig::kRecordsPerBridge,
         RuntimePlaybackViewConfig::kCaptureStagingEvents);
  return 0;
}
