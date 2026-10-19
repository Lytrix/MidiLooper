"""Open MIDI and serial resources for a HITL run."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Optional

try:
    import mido
except ImportError:  # pragma: no cover
    mido = None  # type: ignore[assignment]


@dataclass
class HitlSession:
    out_port: Any
    in_port: Any
    collector: Any = None
    markers: list[str] = field(default_factory=list)

    def snapshot_lines(self) -> list[str]:
        if self.collector is not None:
            return self.collector.snapshot()
        return []

    def close(self, *, on_stage: Optional[Callable[[str], None]] = None) -> None:
        for name, method in (('collector', 'stop'), ('out_port', 'close'), ('in_port', 'close')):
            resource = getattr(self, name)
            if resource is not None:
                if on_stage is not None:
                    on_stage(name)
                getattr(resource, method)()
                setattr(self, name, None)


def open_midi_session(
    *,
    midi_out_name: str,
    midi_in_name: str,
    serial_port: Optional[str] = None,
    serial_baud: int = 115200,
) -> HitlSession:
    if mido is None:  # pragma: no cover
        raise RuntimeError("mido is required for HITL device runs")
    from hitl.midi_io import _find_midi_port

    out_name = _find_midi_port(midi_out_name, is_input=False)
    in_name = _find_midi_port(midi_in_name, is_input=True)
    out_port = mido.open_output(out_name)
    in_port = None
    collector = None
    try:
        in_port = mido.open_input(in_name)
        if serial_port:
            from hitl.serial_collector import SerialCaptureCollector

            collector = SerialCaptureCollector(serial_port, serial_baud)
            collector.start()
        return HitlSession(out_port=out_port, in_port=in_port, collector=collector)
    except Exception:
        if collector is not None:
            collector.stop()
        if in_port is not None:
            in_port.close()
        out_port.close()
        raise
