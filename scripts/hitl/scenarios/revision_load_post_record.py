"""Revision commit → load after a record baseline (skip transport-stop prelude)."""

from __future__ import annotations

from hitl.scenarios.revision_load import run_revision_load


def run_revision_load_post_record(args: object) -> int:
    setattr(args, "skip_workspace_save_prelude", True)
    return run_revision_load(args)
