#!/usr/bin/env python3
"""Animated GIF of a replay as the viewer draws it (tools/viewer/index.html), for the media of
docs/buildplan.md §13 (e.g. the hero GIF: a win over RHEA with the search's visit counts).

Each chosen frame is rendered by headless Chromium from the viewer (?src=&frame=&view=, served from a
temporary directory), then the screenshots are assembled with Pillow. GIFs must stay <= 8 MB
(buildplan §13.2): the script fails above --max-mb.

Usage: replay_gif.py <replay.json> <out.gif> [--turns A-B] [--tribe P] [--decisions-only]
                     [--view T] [--ms 700] [--width 960] [--max-mb 8]
  --turns A-B       frames whose turn (tick) is in A..B (default: all)
  --tribe P         only frames of tribe P's turns (start, its actions, end)
  --decisions-only  only frames that carry search visits (vc), plus each turn start
  --view T          view as tribe T (its fog); default: the true state
Chromium: $CHROME, else Playwright's chrome-headless-shell under ~/.cache/ms-playwright.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from PIL import Image

VIEWER = Path(__file__).resolve().parent / "viewer" / "index.html"
WINDOW = (1300, 760)


def parse_turns(spec: str | None) -> tuple[int, int] | None:
    """'A-B' -> (A, B) inclusive; None -> None. Raises ValueError on anything else."""
    if spec is None:
        return None
    a, sep, b = spec.partition("-")
    if not sep or not a.isdigit() or not b.isdigit() or int(a) > int(b):
        raise ValueError(f"--turns wants A-B with A <= B, got {spec!r}")
    return int(a), int(b)


def select_frames(
    frames: list[dict],
    turns: tuple[int, int] | None,
    tribe: int | None,
    decisions_only: bool,
) -> list[int]:
    """1-based viewer frame numbers to render, in order.

    Frames omit unchanged keys but always carry t (turn) and p (active tribe), as record.cpp writes them.
    """
    out = []
    for i, f in enumerate(frames):
        if turns is not None and not turns[0] <= f["t"] <= turns[1]:
            continue
        if tribe is not None and f["p"] != tribe:
            continue
        if decisions_only and "vc" not in f and f["e"] != "start":
            continue
        out.append(i + 1)
    return out


def find_chrome() -> str:
    if os.environ.get("CHROME"):
        return os.environ["CHROME"]
    found = sorted(
        Path.home().glob(
            ".cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell"
        )
    )
    if not found:
        sys.exit("replay_gif: no headless Chromium; set CHROME=<path>")
    return str(found[-1])


def serve(root: str) -> tuple[http.server.ThreadingHTTPServer, int]:
    handler = functools.partial(QuietHandler, directory=root)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, server.server_address[1]


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass


def shoot(chrome: str, url: str, png: Path) -> None:
    cmd = [
        chrome,
        "--headless",
        "--no-sandbox",
        "--hide-scrollbars",
        f"--window-size={WINDOW[0]},{WINDOW[1]}",
        "--virtual-time-budget=3000",
        f"--screenshot={png}",
        url,
    ]
    subprocess.run(cmd, check=True, capture_output=True, timeout=60)
    if not png.exists():
        raise RuntimeError(f"no screenshot for {url}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("replay")
    ap.add_argument("out")
    ap.add_argument("--turns")
    ap.add_argument("--tribe", type=int)
    ap.add_argument("--decisions-only", action="store_true")
    ap.add_argument("--view", type=int, default=-1)
    ap.add_argument("--ms", type=int, default=700)
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--max-mb", type=float, default=8.0)
    a = ap.parse_args()

    frames = json.loads(Path(a.replay).read_text())["frames"]
    chosen = select_frames(frames, parse_turns(a.turns), a.tribe, a.decisions_only)
    if not chosen:
        sys.exit("replay_gif: no frames selected")
    chrome = find_chrome()
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copy(VIEWER, Path(tmp) / "index.html")
        shutil.copy(a.replay, Path(tmp) / "replay.json")
        server, port = serve(tmp)
        try:
            images = []
            for k, n in enumerate(chosen):
                png = Path(tmp) / f"f{k:05d}.png"
                shoot(
                    chrome,
                    f"http://127.0.0.1:{port}/index.html?src=replay.json&frame={n}&view={a.view}",
                    png,
                )
                im = Image.open(png).convert("RGB")
                h = round(im.height * a.width / im.width)
                images.append(
                    # 256 colours, fast octree: keeps small swatches (tribe chips); median cut lost them
                    im.resize((a.width, h), Image.LANCZOS).quantize(
                        colors=256,
                        method=Image.Quantize.FASTOCTREE,
                        dither=Image.Dither.NONE,
                    )
                )
                print(f"\r{k + 1}/{len(chosen)} frames", end="", file=sys.stderr)
            print(file=sys.stderr)
        finally:
            server.shutdown()
    images[0].save(
        a.out,
        save_all=True,
        append_images=images[1:],
        duration=a.ms,
        loop=0,
        optimize=True,
    )
    mb = Path(a.out).stat().st_size / 1e6
    print(f"{a.out}: {len(images)} frames, {mb:.1f} MB")
    if mb > a.max_mb:
        print(
            f"replay_gif: {mb:.1f} MB is over --max-mb {a.max_mb} (buildplan §13.2): pick fewer frames",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
