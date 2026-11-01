#pragma once

#include <cstdint>

#include "RevisionManifestReaderConfig.h"

/// **DEC-193 — the build gate's one canonical refusal name.**
///
/// The playback window lane has a single predicate that decides whether an owed build may advance this turn
/// (`Track::committedPlaybackRetryCanProgress`). Until this slice it answered only yes/no, so a slot parked at
/// `OwesBuild` reported *that* it owed a build and never *why* it was not being built. This enum names the first
/// failing term in the same order the predicate evaluates them, so host and device report the same word for the same
/// state.
///
/// Report-only: no decision reads this value. Adding a term here without adding it to the predicate (or the reverse)
/// is a drift defect; `test_dec193_*` and the gate implementation are the two places that must move together.
enum class PlaybackBuildRefusal : uint8_t {
  /// The gate is open: the turn may advance the build.
  None = 0,
  /// The slot has no playback runtime, so it cannot owe a build.
  NoRuntime,
  /// A retiring coverage source is this turn's work, not a refusal.
  SourceRetiring,
  /// The root qualification is waiting for the Capture ready edge.
  CaptureReadyWait,
  /// Nothing is retained for the slot: there is no obligation to advance.
  NoDemand,
  /// The retained demand's position was invalidated and is not re-granted here.
  DemandInvalidated,
  /// The demand holds no qualified position at all (made without a clock stamp).
  Unqualified,
  /// The clock epoch moved: the demand's position describes an earlier transport.
  EpochMismatch,
  /// The restarted clock is behind the tick the demand was admitted at.
  AdmittedTickAhead,
  /// The demand's deadline has passed.
  DeadlineExpired,
  /// The interval holds more entries than a window can retain.
  WindowExceedsRetainedCapacity,
  /// The interval exceeds the paged lane's declared closure capacity.
  ClosuresExceedDeclaredCapacity,
  /// The interval pins Note-edit rows the paged lane cannot resolve.
  PinnedEditRowsUnresolved,
  /// No future occurrence admits the demand within the declared lead.
  CannotCompleteBeforeHandoff,
  /// No build role is available in the prewarmed view set.
  NoCoverageRole,
  /// Another promise on this track owns the outstanding work.
  SiblingOutstanding,
  /// The loop geometry changed since the demand was retained.
  GeometryChanged,
  /// The slot's playable source is no longer current.
  SourceNotCurrent,
  /// The demand names a different revision than the loop.
  RevisionMismatch,
#if REVISION_MANIFEST_READER
  /// C5.3 (DEC-246 amendment 19): the routed slot's manifest owes preparation; nothing else is read in its place.
  ManifestNoCommittedSource,
  ManifestImportOwed,
  ManifestIndexOwed,
  ManifestCapacityDenied,
#endif
};

/// The row token for a refusal. Flash-resident literals; an if/else chain, never a table (the RAM1 rule).
inline const char* playbackBuildRefusalName(PlaybackBuildRefusal refusal) {
  switch (refusal) {
    case PlaybackBuildRefusal::None: return "none";
    case PlaybackBuildRefusal::NoRuntime: return "gate_no_runtime";
    case PlaybackBuildRefusal::SourceRetiring: return "gate_source_retiring";
    case PlaybackBuildRefusal::CaptureReadyWait: return "gate_capture_ready_wait";
    case PlaybackBuildRefusal::NoDemand: return "gate_no_demand";
    case PlaybackBuildRefusal::DemandInvalidated: return "gate_demand_invalidated";
    case PlaybackBuildRefusal::Unqualified: return "gate_unqualified";
    case PlaybackBuildRefusal::EpochMismatch: return "gate_epoch_stale";
    case PlaybackBuildRefusal::AdmittedTickAhead: return "gate_admitted_tick_ahead";
    case PlaybackBuildRefusal::DeadlineExpired: return "gate_deadline_expired";
    case PlaybackBuildRefusal::WindowExceedsRetainedCapacity: return "gate_window_too_large";
    case PlaybackBuildRefusal::ClosuresExceedDeclaredCapacity: return "gate_closures_exceed";
    case PlaybackBuildRefusal::PinnedEditRowsUnresolved: return "gate_pinned_edits";
    case PlaybackBuildRefusal::CannotCompleteBeforeHandoff: return "gate_no_lead";
    case PlaybackBuildRefusal::NoCoverageRole: return "gate_no_role";
    case PlaybackBuildRefusal::SiblingOutstanding: return "gate_sibling_outstanding";
    case PlaybackBuildRefusal::GeometryChanged: return "gate_geometry_changed";
    case PlaybackBuildRefusal::SourceNotCurrent: return "gate_source_not_current";
    case PlaybackBuildRefusal::RevisionMismatch: return "gate_revision_mismatch";
#if REVISION_MANIFEST_READER
    case PlaybackBuildRefusal::ManifestNoCommittedSource: return "gate_manifest_no_committed_source";
    case PlaybackBuildRefusal::ManifestImportOwed: return "gate_manifest_import_owed";
    case PlaybackBuildRefusal::ManifestIndexOwed: return "gate_manifest_index_owed";
    case PlaybackBuildRefusal::ManifestCapacityDenied: return "gate_manifest_capacity_denied";
#endif
  }
  return "gate_unknown";
}

/// **DEC-193 — dispatch: was the build ever offered service?**
///
/// `PlaybackBuildRefusal` says *why the retry gate refused*. It cannot say *that the retry was never reached*: an owed
/// build can be bypassed by role arbitration inside `stepPlaybackViewMaintenance` before the gate runs. This second
/// name covers that half. `Retry` is the reach proof; `SelectedNoBranch` means the step returned before the service
/// body; the branch values name where it stopped.
enum class PlaybackDispatchRefusal : uint8_t {
  /// The turn offered service and reached the committed retry.
  Retry = 0,
  /// A holder was selected but the step returned before the service body (e.g. no build/preparation role).
  SelectedNoBranch,
  /// The service body ran but no branch was recorded (an unnamed early return).
  ServiceEntered,
  /// The live-capture branch owned the turn.
  LiveCapture,
  /// The frozen Record-return branch owned the turn.
  RecordReturn,
  /// The rolling/cold path owned the turn.
  Rolling,
};

inline const char* playbackDispatchRefusalName(PlaybackDispatchRefusal refusal) {
  switch (refusal) {
    case PlaybackDispatchRefusal::Retry: return "dispatch_retry";
    case PlaybackDispatchRefusal::SelectedNoBranch: return "dispatch_selected_no_branch";
    case PlaybackDispatchRefusal::ServiceEntered: return "dispatch_service_entered";
    case PlaybackDispatchRefusal::LiveCapture: return "dispatch_live_capture";
    case PlaybackDispatchRefusal::RecordReturn: return "dispatch_record_return";
    case PlaybackDispatchRefusal::Rolling: return "dispatch_rolling";
  }
  return "dispatch_unknown";
}

/// **DEC-194 — which term of `coverageRoleAvailable` is false, when the window lane needs a role.**
///
/// Report-only. The first failing term in the predicate's own order, so "the role is unavailable" becomes a word.
enum class PlaybackCoverageRoleRefusal : uint8_t {
  None = 0,
  NoViewSet,
  BuildActive,
  CompletedPromisePending,
  OwnsCoverageSource,
  CanonicalWindowBusy,
  BuildRoleOutOfRange,
  BuildRolePublished,
  BuildRoleComplete,
  ReadyRoleOccupied,
};

inline const char* playbackCoverageRoleRefusalName(PlaybackCoverageRoleRefusal refusal) {
  switch (refusal) {
    case PlaybackCoverageRoleRefusal::None: return "role_available";
    case PlaybackCoverageRoleRefusal::NoViewSet: return "role_no_view_set";
    case PlaybackCoverageRoleRefusal::BuildActive: return "role_build_active";
    case PlaybackCoverageRoleRefusal::CompletedPromisePending: return "role_promise_pending";
    case PlaybackCoverageRoleRefusal::OwnsCoverageSource: return "role_owns_source";
    case PlaybackCoverageRoleRefusal::CanonicalWindowBusy: return "role_canonical_busy";
    case PlaybackCoverageRoleRefusal::BuildRoleOutOfRange: return "role_build_slot";
    case PlaybackCoverageRoleRefusal::BuildRolePublished: return "role_build_published";
    case PlaybackCoverageRoleRefusal::BuildRoleComplete: return "role_build_complete";
    case PlaybackCoverageRoleRefusal::ReadyRoleOccupied: return "role_ready_occupied";
  }
  return "role_unknown";
}

/// **DEC-194 — why a completed promise was not judged (and therefore kept the coverage role blocked).**
///
/// Report-only. The first false precondition in `revalidateCompletedPlaybackWindow`'s own order. `None` means the
/// promise was judged (cleared and published).
enum class PlaybackRevalidateRefusal : uint8_t {
  None = 0,
  NoEpoch,
  NoRuntime,
  NotPending,
  NeedsService,
  NoViewSet,
  BuildSlotOutOfRange,
  GenerationMoved,
  NotComplete,
  InvalidCoverage,
  LengthMismatchCandidate,
  LengthMismatchWork,
  PhaseOutOfRange,
  PhaseNotBoundary,
  SourceNotCurrent,
  SourceRevision,
  SourceIdentity,
  PreviewInactive,
  PreviewRevision,
  PromiseInvalidated,
  ClockBeforeAdmitted,
  DeadlineOverflow,
  TooEarly,
  NotAligned,
};

inline const char* playbackRevalidateRefusalName(PlaybackRevalidateRefusal refusal) {
  switch (refusal) {
    case PlaybackRevalidateRefusal::None: return "rv_none";
    case PlaybackRevalidateRefusal::NoEpoch: return "rv_no_epoch";
    case PlaybackRevalidateRefusal::NoRuntime: return "rv_no_runtime";
    case PlaybackRevalidateRefusal::NotPending: return "rv_not_pending";
    case PlaybackRevalidateRefusal::NeedsService: return "rv_needs_service";
    case PlaybackRevalidateRefusal::NoViewSet: return "rv_no_view_set";
    case PlaybackRevalidateRefusal::BuildSlotOutOfRange: return "rv_build_slot";
    case PlaybackRevalidateRefusal::GenerationMoved: return "rv_generation";
    case PlaybackRevalidateRefusal::NotComplete: return "rv_not_complete";
    case PlaybackRevalidateRefusal::InvalidCoverage: return "rv_invalid_coverage";
    case PlaybackRevalidateRefusal::LengthMismatchCandidate: return "rv_length_candidate";
    case PlaybackRevalidateRefusal::LengthMismatchWork: return "rv_length_work";
    case PlaybackRevalidateRefusal::PhaseOutOfRange: return "rv_phase_range";
    case PlaybackRevalidateRefusal::PhaseNotBoundary: return "rv_phase_boundary";
    case PlaybackRevalidateRefusal::SourceNotCurrent: return "rv_source_not_current";
    case PlaybackRevalidateRefusal::SourceRevision: return "rv_source_revision";
    case PlaybackRevalidateRefusal::SourceIdentity: return "rv_source_identity";
    case PlaybackRevalidateRefusal::PreviewInactive: return "rv_preview_inactive";
    case PlaybackRevalidateRefusal::PreviewRevision: return "rv_preview_revision";
    case PlaybackRevalidateRefusal::PromiseInvalidated: return "rv_invalidated";
    case PlaybackRevalidateRefusal::ClockBeforeAdmitted: return "rv_clock_before_admitted";
    case PlaybackRevalidateRefusal::DeadlineOverflow: return "rv_deadline_overflow";
    case PlaybackRevalidateRefusal::TooEarly: return "rv_too_early";
    case PlaybackRevalidateRefusal::NotAligned: return "rv_not_aligned";
  }
  return "rv_unknown";
}

/// **DEC-192 — why a repeat publication request did *not* look idempotent.**
///
/// Report-only. Named at the request so a restarting rebuild explains itself instead of being inferred from `q`/`a`.
enum class PublicationIdentityRefusal : uint8_t {
  None = 0,
  RebuildFlagMismatch,
  FromLoaded,
  PassCountMismatch,
  RecordRowMissing,
  RecordRowExtra,
  OverdubRowMissing,
  CompanionCountMismatch,
  CompanionRowMissing,
};

inline const char* publicationIdentityRefusalName(PublicationIdentityRefusal refusal) {
  switch (refusal) {
    case PublicationIdentityRefusal::None: return "pubident_ok";
    case PublicationIdentityRefusal::RebuildFlagMismatch: return "pubident_rebuild_flag";
    case PublicationIdentityRefusal::FromLoaded: return "pubident_from_loaded";
    case PublicationIdentityRefusal::PassCountMismatch: return "pubident_pass_count";
    case PublicationIdentityRefusal::RecordRowMissing: return "pubident_record_missing";
    case PublicationIdentityRefusal::RecordRowExtra: return "pubident_record_extra";
    case PublicationIdentityRefusal::OverdubRowMissing: return "pubident_overdub_missing";
    case PublicationIdentityRefusal::CompanionCountMismatch: return "pubident_companion_count";
    case PublicationIdentityRefusal::CompanionRowMissing: return "pubident_companion_missing";
  }
  return "pubident_unknown";
}
