#!/usr/bin/env python3
"""Claude Code PostToolUse hook that forces the running Godot editor to
rescan and live-reload scripts after Claude edits files.

Expects Claude Code's hook payload (tool name + tool_input) on stdin.
Finds the Godot project root by walking up from the tool-input file path
until a `project.godot` is found, then reads `<project>/.godot/claude_reload.json`
for the local-only port + token that the editor's ClaudeReloadServer wrote.

Wire it up by adding to the game project's `.claude/settings.json`:

    {
      "hooks": {
        "PostToolUse": [
          {
            "matcher": "Edit|Write|MultiEdit|NotebookEdit",
            "hooks": [
              { "type": "command", "command": "python path/to/claude_godot_reload_hook.py" }
            ]
          }
        ]
      }
    }

The script never fails the tool: any error just prints a short message and
exits 0, so a stopped editor never blocks Claude's work.
"""

from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path


def read_payload() -> dict:
    try:
        return json.loads(sys.stdin.read() or "{}")
    except Exception:
        return {}


def collect_edited_paths(payload: dict) -> list[str]:
    """Extract every file path touched by Edit, Write, MultiEdit, NotebookEdit."""
    tool_input = payload.get("tool_input") or {}
    paths: list[str] = []
    for key in ("file_path", "notebook_path"):
        value = tool_input.get(key)
        if isinstance(value, str) and value:
            paths.append(value)
    # MultiEdit uses `edits` internally but still reports via `file_path` in
    # the hook payload — no special-casing needed.
    return paths


def find_project_root(anchor: str) -> Path | None:
    start = Path(anchor).expanduser().resolve()
    if start.is_file():
        start = start.parent
    for candidate in [start, *start.parents]:
        if (candidate / "project.godot").exists():
            return candidate
    return None


def to_res_path(project_root: Path, absolute: str) -> str | None:
    try:
        rel = Path(absolute).expanduser().resolve().relative_to(project_root)
    except ValueError:
        return None
    return "res://" + rel.as_posix()


def post_reload(project_root: Path, res_paths: list[str]) -> None:
    discovery = project_root / ".godot" / "claude_reload.json"
    if not discovery.exists():
        # Editor isn't running or this build of Godot has the reload server
        # disabled. Nothing to do.
        return
    try:
        info = json.loads(discovery.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"[claude-reload] ignoring malformed {discovery}: {exc}", file=sys.stderr)
        return

    port = info.get("port")
    token = info.get("token")
    if not port or not token:
        return

    body = json.dumps({"paths": res_paths}).encode("utf-8")
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/reload",
        data=body,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "X-Claude-Token": token,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=2):
            pass
    except urllib.error.URLError:
        # Editor was closed between writing the discovery file and now —
        # silently ignore. Not a workflow problem.
        return
    except Exception as exc:
        print(f"[claude-reload] request failed: {exc}", file=sys.stderr)


def main() -> int:
    payload = read_payload()
    edits = collect_edited_paths(payload)
    if not edits:
        return 0

    # All edited files in a single hook invocation share a project, so the
    # first one is enough to locate the root.
    project_root = find_project_root(edits[0]) or find_project_root(os.getcwd())
    if project_root is None:
        return 0

    res_paths = [p for p in (to_res_path(project_root, f) for f in edits) if p]
    if not res_paths:
        return 0

    post_reload(project_root, res_paths)
    return 0


if __name__ == "__main__":
    sys.exit(main())
