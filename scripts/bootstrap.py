#!/usr/bin/env python3
"""Fetch pinned build dependencies; refuses dirty/wrong existing checkouts."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "dependencies.lock.json").read_text())

def prepare(name):
    spec = LOCK[name]
    dest = ROOT / "third_party" / name
    if not dest.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--branch", spec["tag"], spec["repository"], str(dest)], check=True)
    actual = subprocess.check_output(["git", "-C", str(dest), "rev-parse", "HEAD"], text=True).strip()
    if actual != spec["commit"]:
        raise SystemExit(f"{name}: expected {spec['commit']}, got {actual}; use a clean dependency checkout")
    dirty = subprocess.check_output(["git", "-C", str(dest), "status", "--porcelain", "--untracked-files=no"], text=True)
    if dirty:
        raise SystemExit(f"{name}: dependency has local modifications")
    print(f"{name}: {actual}")

def game_data():
    spec = LOCK["game_data"]
    dest = ROOT / "android/app/src/main/assets/bootstrap/game-data.zip"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists():
        temp = dest.with_suffix(".part")
        try:
            with urllib.request.urlopen(spec["url"], timeout=60) as source, temp.open("wb") as out:
                while chunk := source.read(1024 * 1024):
                    out.write(chunk)
            temp.replace(dest)
        finally:
            temp.unlink(missing_ok=True)
    with dest.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != spec["sha256"]:
        raise SystemExit("Upstream nightly data changed. Do not silently update: review the new release and update dependencies.lock.json, or import a full PC ZIP in the launcher.")
    print(f"Game data verified: {digest}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--with-data", action="store_true")
    args = parser.parse_args()
    for name in ("SDL", "SDL_mixer"):
        prepare(name)
    if args.with_data:
        game_data()
