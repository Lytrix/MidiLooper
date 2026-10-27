// Measurement-only declarations; compile as an object, inspect size_* symbols.
#include "Track.h"
#include "Utils/RecordPreparationRole.h"
#include "TrackUndoPublication.h"
struct TrackCaptureStopTestAccess {
  using Job = Track::CaptureStopJob;
  using Presentation = Track::CaptureStopPresentation;
  using Jobs = Track::CaptureStopJobs;
  using Receipt = Track::CaptureStopJobs::RecordStagingReceipt;
  using Latch = Track::PendingInteractionLatch;
};
#define SZ(name, type) extern "C" { char size_##name[sizeof(type)]; }
SZ(MidiEvent, MidiEvent)
SZ(EventChunk, EventChunk)
SZ(LoopEventStore, LoopEventStore)
SZ(CaptureRange, LoopEventStore::CaptureRange)
SZ(ChunkReservation, LoopEventStore::ChunkReservation)
SZ(CaptureChunkIdList, CaptureChunkIdList)
SZ(TrackCaptureStopJob, TrackCaptureStopTestAccess::Job)
SZ(CaptureStopPresentation, TrackCaptureStopTestAccess::Presentation)
SZ(CaptureStopJobs, TrackCaptureStopTestAccess::Jobs)
SZ(RecordStagingReceipt, TrackCaptureStopTestAccess::Receipt)
SZ(InteractionLatch, TrackCaptureStopTestAccess::Latch)
SZ(Track, Track)
SZ(Loop, Loop)
SZ(TrackPlaybackRuntime, TrackPlaybackRuntime)
SZ(LoopPlaybackRuntime, LoopPlaybackRuntime)
SZ(RuntimePlaybackView, RuntimePlaybackView)
SZ(RuntimePlaybackViewSet, RuntimePlaybackViewSet)
SZ(RecordStagingCursor, RecordSourceStaging::Cursor)
SZ(RecordPreparationRole, RecordPreparation::Role)
SZ(RecordPreparationWorkspace, RecordPreparation::Workspace)
SZ(RecordPreparationCursor, RecordPreparation::RecordPreparationCursor)
SZ(OrderCursor, LoopEventStoreOrderCursor)
SZ(CommitCursor, LoopEventStoreCommitCursor)
SZ(CapturePreview, CapturePreview)
SZ(UndoEntry, UndoEntry)
SZ(PendingNote, PendingNote)
SZ(PendingNoteChange, PendingNoteChange)
SZ(PendingCapturePass, PendingCapturePass)
SZ(ImmutableLoopSnapshotRef, ImmutableLoopSnapshotRef)
SZ(ActiveNoteLedger, ActiveNoteLedger)
struct CandidateCustodyBox {
  TrackCaptureStopTestAccess::Presentation interval;
  uint32_t consumerMask;
  uint32_t state;
};
SZ(CandidateCustodyBox, CandidateCustodyBox)
SZ(CaptureAdmissionStaging, CaptureAdmissionStaging)
SZ(CaptureNoteReservation, CaptureNoteReservation)
SZ(FixedPlaybackEventStorage, FixedPlaybackEventStorage)
SZ(PlaybackBridgeRecord, PlaybackBridgeRecord)
SZ(Capture, Capture)
SZ(LoopSnapshot, LoopSnapshot)
SZ(LoopSnapshotNote, LoopSnapshotNote)
SZ(LoopSnapshotEvent, LoopSnapshotEvent)
SZ(SnapshotSpanRoot, SnapshotSpanRoot)
SZ(TrackUndoPublication, TrackUndoPublication)

// --- CONTRACTS.md C1.1 / C4.3 contract mirrors (declaration-only) -------------------
// Field-for-field mirrors of the CONTRACTS.md types, measured so C2 can charge them.
// These are sizeof probes only: nothing here is production storage, no firmware type is
// modified, and no produced object is linked into firmware.
enum class ContractCustodyState : uint8_t { Free, Sealed, Retiring };

struct ContractCustodyReceipt {
  LoopEventStore::CaptureRange rawRange;
  Loop* owner;
  LoopId loopId;
  uint64_t interval;
  uint64_t frozenCaptureContentGeneration;
  uint32_t loopLengthTicks;
  uint32_t generation;
  ContractCustodyState state;
  uint8_t obligations;
  uint8_t custodyIndex;
  uint8_t slot;
  bool ownsExtent;
};
SZ(ContractCustodyReceipt, ContractCustodyReceipt)

// One slab per Track: K receipts plus the small owner word. K is a free parameter, so the
// trace-minimum (4) and trace-plus-reader (5) shapes are both priced.
template <size_t K>
struct ContractCustodySlab {
  ContractCustodyReceipt receipts[K];
  uint8_t count;
};
SZ(ContractCustodySlabK4, ContractCustodySlab<4>)
SZ(ContractCustodySlabK5, ContractCustodySlab<5>)

// Append-at-end price of the C4.3 `UndoEntry::interval` field. A wrapper measures the worst
// case, where no existing trailing padding in `UndoEntry` can absorb the new member.
struct ContractUndoEntryWithInterval {
  UndoEntry base;
  uint64_t interval;
};
SZ(UndoEntryWithInterval, ContractUndoEntryWithInterval)
