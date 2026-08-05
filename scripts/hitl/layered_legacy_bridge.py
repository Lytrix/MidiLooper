"""Bridge layered ScenarioContext to legacy scenario handlers (Phase 2)."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from types import SimpleNamespace

from hitl.baseline_canonical_args import (
    canonical_baseline_legacy_args,
    canonical_edit_full_legacy_args,
    merge_legacy_cli_args,
)
from hitl.config import HitlConfig
from hitl.context import HitlRunContext, ScenarioContext


@dataclass
class LayeredLegacyBridge:
    """Shared legacy args bag across scenarios in one preset run."""

    config: HitlConfig
    shared_ctx: HitlRunContext = field(default_factory=HitlRunContext)
    legacy_args: list[str] = field(default_factory=list)

    @classmethod
    def from_config(cls, config: HitlConfig) -> LayeredLegacyBridge:
        return cls(config=config, legacy_args=list(config.legacy_extra_args))

    def args_for(
        self,
        ctx: ScenarioContext,
        *,
        scenario_flags: list[str],
        preset: str | None = None,
    ) -> SimpleNamespace:
        merged = merge_legacy_cli_args(scenario_flags, self.legacy_args)
        return SimpleNamespace(
            preset=preset,
            out_dir=Path(ctx.out_dir),
            verify_only=self.config.verify_only,
            verify_serial_log=self.config.verify_serial_log,
            serial_port=self.config.serial_port,
            legacy_args=merged,
            hitl_context=self.shared_ctx,
        )

    def record_overdub_args(self, ctx: ScenarioContext) -> SimpleNamespace:
        flags = canonical_baseline_legacy_args() + self._slot_target_flags()
        if self.config.follow_current_session:
            flags.append("--follow-current-session")
        return self.args_for(ctx, scenario_flags=flags, preset="base")

    def edit_full_args(self, ctx: ScenarioContext) -> SimpleNamespace:
        # Slot targets + Mode B follow: foundation_runner owns MIDI/serial; legacy
        # edit_full reuses them via hitl_session (owns_resources=False).
        flags = (
            canonical_edit_full_legacy_args()
            + self._slot_target_flags()
            + [
                "--midi-out",
                self.config.midi_out_name,
                "--midi-in",
                self.config.midi_in_name,
            ]
        )
        if self.config.follow_current_session or self.config.needs_managed_capture():
            flags.append("--follow-current-session")
        return self.args_for(ctx, scenario_flags=flags, preset="edit_full")

    def _slot_target_flags(self) -> list[str]:
        from hitl.layered_cli import slot_target_legacy_flags

        return slot_target_legacy_flags(
            self.config.track_number,
            self.config.loop_slot,
            self.config.midi_channel,
        )
