"""Bridge layered ScenarioContext to legacy scenario handlers (Phase 2)."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from types import SimpleNamespace

from hitl.baseline_canonical_args import (
    canonical_baseline_legacy_args,
    canonical_edit_minimal_base_legacy_args,
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

    def record_seed_args(self, ctx: ScenarioContext) -> SimpleNamespace:
        flags = canonical_edit_minimal_base_legacy_args() + self._slot_target_flags() + [
            "--record-bars",
            str(self.config.record_bars),
        ]
        return self.args_for(ctx, scenario_flags=flags, preset="edit_minimal")

    def record_overdub_args(self, ctx: ScenarioContext) -> SimpleNamespace:
        flags = canonical_baseline_legacy_args() + self._slot_target_flags()
        if self.config.follow_current_session:
            flags.append("--follow-current-session")
        return self.args_for(ctx, scenario_flags=flags, preset="base")

    def edit_minimal_args(self, ctx: ScenarioContext) -> SimpleNamespace:
        flags = self._slot_target_flags() + [
            "--record-bars",
            str(self.config.record_bars),
            "--post-seed-settle-ms",
            "3500",
            "--start-transport",
            "--press-ms",
            str(self.config.press_ms),
            "--phase-wait-ms",
            str(self.config.phase_wait_ms),
        ]
        return self.args_for(ctx, scenario_flags=flags, preset="edit_minimal")

    def long_loop_display_args(self, ctx: ScenarioContext) -> SimpleNamespace:
        flags = self._slot_target_flags() + [
            "--record-bars",
            "32",
            "--start-transport",
            "--press-ms",
            str(self.config.press_ms),
            "--phase-wait-ms",
            str(self.config.phase_wait_ms),
            "--boot-settle-ms",
            str(self.config.boot_settle_ms),
        ]
        return self.args_for(ctx, scenario_flags=flags, preset="long_loop_display_window")

    def _slot_target_flags(self) -> list[str]:
        from hitl.layered_cli import slot_target_legacy_flags

        return slot_target_legacy_flags(
            self.config.track_number,
            self.config.loop_slot,
            self.config.midi_channel,
        )

    def common_args(self, ctx: ScenarioContext, *, preset: str | None = None) -> SimpleNamespace:
        flags = self._slot_target_flags() + [
            "--press-ms",
            str(self.config.press_ms),
            "--phase-wait-ms",
            str(self.config.phase_wait_ms),
        ]
        if self.config.serial_port:
            flags.extend(["--serial-port", self.config.serial_port])
        if self.config.follow_current_session:
            flags.append("--follow-current-session")
        return self.args_for(ctx, scenario_flags=flags, preset=preset)
