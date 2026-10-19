# Existing seeded-edit qualification recipe

This slice corrects host predicates/unit tests only. No hardware execution or new stimulus/marker is authored.

| Field | Recipe |
|---|---|
| Acceptance | Seeded-edit qualification cannot pass without intended committed seed, current boot app-paint evidence, intended moved/stacked identities, applied empty deselect and safe cleanup. |
| Reused stimulus | Existing note_edit_reselect_stacked preset, STIM-NE-RESELECT-STACKED-01@v1, unchanged EDIT_RECORD_FIXTURE seed. Catalog already listed in prior title-fix slice; no new stimulus needed. |
| New stimulus | None; canonical registry/gesture plan unchanged. |
| Gestures | Existing scenario helpers only; no new MIDI action or byte construction. |
| Existing evidence | DFRAME (non-Tier-A, app-paint path, not physical DMA completion), direct timestamped select/focus logs, DNTE (non-Tier-A, may shed), deferred select_apply (#DBG without occurrence timestamp), existing RECA/RECS/REVT/stop status/persistence responses. Missing/displaced mandatory identity is unqualified, never reconstructed as success. |
| Marker tier/check contract | N/A: no firmware marker or tier registration changed. |
| RAM1 | No firmware/build change; installed title image remains unchanged (last verified padding968/floor840). No new device allocation/state. |
| Host RED | Boot timer/stale witness; nonempty deselect; wrong Move/identity; delayed twins; zero-observation optional checks; incomplete seed; missing pending-job diagnostic. |
| Offline evidence | captures/i3b-seeded-edit-t8s4-07 and -09, original artifacts preserved; replay summaries are observations, not rerun PASS. |
| Device entry point, later | Canonical host_midi_hitl.py run --preset note_edit_reselect_stacked with separately authorized target and pinned firmware/tempo/clock; managed360s bound unchanged. Do not execute invocation.json/command.json. |
| Verdict | Core exact Move/deselect/identities distinct from optional qualification; UNMEASURED never qualifies timing/retention. Seed/core evidence missing => refusal/incomplete. Missing boot witness => timeout, not timer success. Pending job and fence errors separately visible. Final canonical shutdown remains mandatory. |
| Remaining gate | Clean exact2bar seed, observable selected identity/geometry and intended clock pacing, plus full edit leg/teardown on authorized hardware. No acceptance inferred from host tests. |
