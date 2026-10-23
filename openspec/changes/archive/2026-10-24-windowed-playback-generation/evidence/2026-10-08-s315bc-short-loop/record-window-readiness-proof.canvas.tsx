import { Callout, Card, CardBody, CardHeader, Divider, Grid, H1, H2, Stack, Table, Text, useHostTheme } from "cursor/canvas";

export default function RecordWindowReadinessProof() {
  const theme = useHostTheme();
  return (
    <Stack gap={20} style={{ padding: 28, maxWidth: 1060, margin: "0 auto", background: theme.bg.editor, color: theme.text.primary }}>
      <div>
        <Text size="small" tone="secondary">MIDI LOOPER · ARCHITECTURE PROOF · 2026-10-08</Text>
        <H1>Can late Record input be ready on a one-bar loop?</H1>
        <Text>Under the currently declared owner-service contract: <strong>no universal next-traversal guarantee</strong>. A persistent complete current window solves unchanged wraps, but not the arrival of new content after its last safe preparation opportunity.</Text>
      </div>
      <Callout tone="warning" title="Proved impossibility for the existing contract">
        A changed interval has at most 768 ticks to its next one-bar traversal. The current canCompleteWithinLead predicate requires at least 1,792 ticks of available lead for an empty successor and 2,304 for a two-event successor, before extra source work. These are conservative admission thresholds, not measured build durations or proof that hardware could never be faster.
      </Callout>
      <Grid columns={2} gap={18}>
        <Card><CardHeader>Admission inequality</CardHeader><CardBody>
          <Text weight="semibold">Available ≥ margin + grains × holders × advance</Text>
          <Text size="small">Current terms: margin 768 ticks; 8 cross-track opportunities per grain (7 denied + 1 served); permitted advance 64 ticks. Every value comes from <code>Track::playbackViewCoverageContract</code> and <code>CoverageServiceContract::canCompleteWithinLead</code>.</Text>
        </CardBody></Card>
        <Card><CardHeader>Adversarial arrival</CardHeader><CardBody>
          <Text>A valid Record event arrives just before the end of its one-bar cycle. It cannot already be in an immutable current window. Its next occurrence is one cycle away: ≤ 768 ticks. A private successor must incorporate it and finish before that tick.</Text>
        </CardBody></Card>
      </Grid>
      <div>
        <H2>Current admission threshold versus next traversal</H2>
        <Table headers={["Window contents", "Charged grains", "Admission lead (ticks)", "Next one-bar traversal (ticks)", "Current gate"]} rows={[
          ["Empty, complete coverage", "2", "768 + 2 × 8 × 64 = 1,792", "≤ 768", "Refused"],
          ["One note pair (2 events)", "3", "768 + 3 × 8 × 64 = 2,304", "≤ 768", "Refused"],
          ["Hypothetical one-grain update", "1", "768 + 1 × 8 × 64 = 1,280", "≤ 768", "Still refused"],
        ]} rowTone={["danger", "danger", "warning"]} />
        <Text size="small" tone="tertiary">Source: RuntimePlaybackViewBuild.cpp admission arithmetic; TrackPlaybackWindowBuild.cpp owner declaration and grain derivation; RuntimePlaybackOrderBuilder.cpp ordering work bound. The native test asserts 1,792 exactly and the two-event one-bar refusal; 2,304 is calculated from the production two-event grain formula, not separately asserted. The hypothetical row is arithmetic only, not implemented behavior.</Text>
      </div>
      <Divider />
      <div>
        <H2>What is already achievable</H2>
        <Table headers={["Case", "Mechanism", "Proof status"]} rows={[
          ["Unchanged short loop", "Retain one complete current window; reuse it on every wrap", "Structurally plausible; reader cutover not implemented"],
          ["Record first bar", "Build from generation-qualified Capture in private roles; keep current immutable", "Existing raw first-bar worker is separate; unified production path not implemented"],
          ["Whole LoopSnapshot publishing", "Keep candidate private, publish atomically; playback reads Capture/frozen range meanwhile", "Existing authority rule; partial root forbidden"],
          ["Late accepted event", "Needs a new bounded successor publication protocol before its next affected tick", "Not proved by the current 64/7/768 service bound"],
        ]} rowTone={["info", "neutral", "info", "warning"]} />
      </div>
      <Callout tone="neutral" title="Eight sounding tracks: a necessary capacity bound">
        If eight tracks each accept a two-event change at the same late point, the existing build costs at least 8 × 3 = 24 owner grains. One-grain-per-turn service must guarantee 24 opportunities inside 768 ticks: at most 32 ticks per opportunity with zero margin, or about 21 ticks with a 256-tick margin. Today's declared permitted gap is 64 ticks. This is a necessary bound, not proof that a faster schedule fits CPU or competing work.
      </Callout>
      <Callout tone="warning" title="Stop-time geometry is a separate obligation">
        Record stop chooses the final loop length and may need to project raw coverage or close held notes. A prebuilt window transfers unchanged only if its Capture generation, final geometry, content and closures match. Otherwise the frozen range must remain pinned while the owner constructs a corrected successor. Prebuilding raw bars alone cannot prove an instant stop return for every final length.
      </Callout>
      <div>
        <H2>Conditions for a real guarantee</H2>
        <Stack gap={8}>
          <Text>1. Keep a prewarmed, complete <code>current</code> per sounding slot, with <code>ready</code> and private <code>build</code>. A late Capture append invalidates only the candidate source identity, not the reader.</Text>
          <Text>2. Establish a bounded incremental successor protocol or stronger measured owner opportunity. For a one-bar deadline, its <strong>new</strong> declared terms must satisfy margin + charged work × holder contention × maximum phase advance ≤ 768 ticks, including lookup, order, closure, publication and competing work. Current margin alone consumes the entire budget.</Text>
          <Text>3. On failure, name the missed affected interval and preserve the prior complete source for only its permitted traversal. One-pass-late is containment, <strong>not</strong> a readiness pass; a second miss is a fault. Record input must not be silently discarded to make the calculation pass.</Text>
          <Text>4. Prove the production entry → deferred owner → clock reader on native interleavings, then measure the worst case on a pinned device image under eight-track contention, late append, stop, persistence and display work. No supported-route cutover until this passes.</Text>
        </Stack>
      </div>
      <Text weight="semibold">Conditional proof: if a complete current exists, every accepted change has a retained source, its charged successor work has a finite bound G, all competing holders are included, and the measured service guarantee satisfies M + (G + K) × H × Δ ≤ D for the earliest affected deadline D, the owner finishes ready before D. Only then may the edge adopt it. Repeated edits must preserve already-useful work rather than restart G indefinitely. None of these new bounds is established for one-bar late Record input today.</Text>
      <Callout tone="info" title="Decision required before implementation">
        The current architecture cannot promise next-traversal readiness for arbitrarily late accepted Record input on a one-bar loop. Choose and qualify a faster bounded service model, or explicitly change the musical latency requirement. Reusing an unchanged current window solves wraps; it does not solve a late content mutation.
      </Callout>
      <Text size="small" tone="tertiary">Evidence: include/Globals.h (768 ticks/bar, eight tracks); src/Track/TrackPlaybackWindowBuild.cpp (64 / 7 / 768 declaration); src/Track/RuntimePlaybackViewBuild.cpp (admission formula); src/Track/RuntimePlaybackOrderBuilder.cpp (two-event order grain); src/Track/TrackCaptureStopCommit.cpp and src/Track/RuntimePlaybackRuntime.cpp (final geometry, stale raw-view refusal); docs/Authority/Architecture/Snapshots.md (atomic whole-root publication); native test test_one_bar_short_loop_does_not_bypass_the_declared_owner_deadline (179/179 focused suite in prior turn). No firmware changed for this analysis.</Text>
    </Stack>
  );
}
