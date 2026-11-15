#!/usr/bin/env python3
"""Drive the Cardputer-ADV emulator through the adv-tour build and photograph it.

Why this exists
---------------
QEMU's Cardputer-ADV keyboard delivers no character keys to the guest -- `, ;, .
and the digits all vanish -- so the settings card cannot be opened or changed
there the way a player does it, and the real BMI270 is not modelled at all. The
`adv-tour` build (`-DCRAFTCRAFT_EMU_TOUR -DCRAFTCRAFT_GYRO_FAKE`) therefore walks
a fixed shot list itself and prints `TOUR <n> <name>` on the serial line as it
enters each one. This script waits for those lines, grabs a frame after each, and
then resets the session to prove the settings came back out of NVS.

Usage
-----
    python3 tools/capture-tour.py --bin /tmp/cc-tour.bin --out docs/emu \
        --api http://127.0.0.1:8798

The service is the emulator's Device Service on the port it was started on; the
session is deleted at the end either way.
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

SHOTS = ["settings", "bright-10", "bright-100", "fov-60", "fov-90",
         "play-rest", "gyro-yaw", "gyro-pitch"]

# The tour holds each shot for SHOT_SECONDS of the guest's wall clock (see
# TOUR_SHOT_MS in src/main.cpp), and QEMU's virtual clock tracks real time even
# when the guest only manages a frame every second or two -- so the capture is
# scheduled in seconds and samples the middle of each shot. Frame counting would
# drift with the machine's load, which is the one thing that is not stable here.
SHOT_SECONDS = 12.0


def _request(api, method, path, body=None, content_type="application/json"):
    url = f"{api}/api/v1{path}"
    data = None
    headers = {}
    if body is not None:
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")[:400]
        raise SystemExit(f"{method} {path} -> {exc.code}: {detail}") from exc
    if path.endswith("frame.png"):
        return raw
    return json.loads(raw) if raw else {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://127.0.0.1:8798")
    ap.add_argument("--bin", required=True, help="merged image to run")
    ap.add_argument("--out", default="docs/emu")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    image = Path(args.bin).read_bytes()
    artifact = _request(args.api, "POST", "/artifacts", image,
                        "application/octet-stream")
    firmware = _request(args.api, "POST", "/firmware",
                        {"device_id": "cardputer-adv", "kind": "merged",
                         "artifact_id": artifact["artifact_id"]})
    session = _request(args.api, "POST", "/sessions",
                       {"device_id": "cardputer-adv",
                        "firmware_id": firmware["firmware_id"],
                        "ttl_seconds": 900})
    sid = session["session_id"]
    print(f"session {sid} on {args.api}", flush=True)

    captured = {}
    try:
        # The tour starts on the first guest frame, so the schedule is anchored
        # to the first frame that is not the boot screen. Everything after that
        # is fixed offsets: sample the middle of each six-second shot.
        # The tour's first act, before anything is drawn, is to enter shot 0 --
        # so the first frame that is not the blank panel already IS the settings
        # card. A blank 240x135 PNG compresses to a kilobyte or two; the card
        # does not.
        # A blank 240x135 PNG is a few hundred bytes; anything past 1 KB has
        # pixels in it. If nothing but blank arrives for 45 s the guest died on
        # the way up -- the Arduino core's clock switch trips the interrupt
        # watchdog under a loaded host, which is a QEMU artefact and not this
        # firmware -- and the whole session is worth starting again.
        deadline = time.time() + 45
        while time.time() < deadline:
            try:
                frame = _request(args.api, "GET", f"/sessions/{sid}/frame.png")
            except SystemExit:
                time.sleep(0.4)      # no framebuffer yet
                continue
            if len(frame) > 1000:
                break
            time.sleep(0.5)
        else:
            raise SystemExit("guest never drew a frame (boot watchdog)")
        t0 = time.time()
        print(f"tour started (first frame after boot at {time.strftime('%H:%M:%S')})",
              flush=True)

        for i, name in enumerate(SHOTS):
            target = t0 + i * SHOT_SECONDS + SHOT_SECONDS * 0.5
            time.sleep(max(0.0, target - time.time()))
            png = _request(args.api, "GET", f"/sessions/{sid}/frame.png")
            path = out / f"{name}.png"
            path.write_bytes(png)
            captured[name] = path
            print(f"  {name}: {len(png)} bytes", flush=True)

        # Persistence, in the picture rather than on a serial line: the tour
        # saved brightness and FOV through the card's own saveSettings(), and
        # shot 0 reads NVS back. After a reset the card has to show 100% / 90.
        _request(args.api, "POST", f"/sessions/{sid}/reset", {})
        time.sleep(3.0 * SHOT_SECONDS)   # the tour's one flash write stalls boot
        png = _request(args.api, "GET", f"/sessions/{sid}/frame.png")
        path = out / "nvs-after-reset.png"
        path.write_bytes(png)
        print(f"  nvs-after-reset: {len(png)} bytes", flush=True)
    finally:
        try:
            _request(args.api, "DELETE", f"/sessions/{sid}")
            print("session deleted", flush=True)
        except SystemExit:
            print(f"WARNING: could not delete session {sid}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
