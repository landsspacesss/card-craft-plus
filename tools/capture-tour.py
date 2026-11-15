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
         "play-rest", "gyro-yaw", "gyro-pitch", "gyro-raw"]

# The tour advances on ENTER and on nothing else (see tourTick in src/main.cpp):
# a wall clock cannot work here, because the panel endpoint serves the last frame
# the guest actually finished drawing and under load that can be many seconds
# behind. So this presses ENTER once per shot and photographs afterwards, waiting
# for the frame to change rather than assuming it has.
ENTER_KEYS = ("ret", "enter")


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


def _nvs_saved(state_dir, sid):
    """True when the tour's last shot has written its two keys.

    Read straight out of the emulated flash rather than off the screen: the NVS
    partition is at 0x9000, key names are stored as bytes, and "fov" is a
    substring no other page holds. This is what makes the last shot provable
    instead of assumed -- QEMU's keyboard drops presses, so the sequence can end
    one short and the save simply never happens."""
    path = Path(state_dir) / "sessions" / sid / "virtual-flash.bin"
    try:
        with path.open("rb") as fh:
            fh.seek(0x9000)
            nvs = fh.read(0x5000)
    except FileNotFoundError:
        return False
    return b"fov" in nvs and b"br" in nvs


def _get_frame(api, sid):
    try:
        return _request(api, "GET", f"/sessions/{sid}/frame.png")
    except SystemExit:
        return None


def _wait_for_drawing(api, sid, timeout=120):
    """Wait until the panel has more than a blank frame on it."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        frame = _get_frame(api, sid)
        if frame and len(frame) > 1000:
            return frame
        time.sleep(0.5)
    raise SystemExit("guest never drew a frame (boot watchdog under load?)")


def _wait_for_change(api, sid, previous, timeout=90):
    """Wait until the panel shows something other than `previous`."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        frame = _get_frame(api, sid)
        if frame and frame != previous and len(frame) > 1000:
            return frame
        time.sleep(0.5)
    raise SystemExit("the guest stopped redrawing; nothing to photograph")


def _press_enter(api, sid):
    """ENTER, under whichever name this service uses for it."""
    last_error = None
    for key in ENTER_KEYS:
        try:
            # hold_seconds is not decoration: without it the service takes a
            # different branch that queues nothing the guest ever sees, and a
            # short hold can fall entirely between two keyboard polls once the
            # world is being drawn. The service rejects anything over 5 s.
            _request(api, "POST", f"/sessions/{sid}/input",
                     {"kind": "key", "key": key, "action": "tap",
                      "hold_seconds": 4.5})
            return
        except SystemExit as exc:
            last_error = exc
    raise SystemExit(f"could not press ENTER: {last_error}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://127.0.0.1:8798")
    ap.add_argument("--bin", required=True, help="merged image to run")
    ap.add_argument("--out", default="docs/emu")
    ap.add_argument("--state-dir", default="/home/landspace/espemu-state-cardcraft",
                    help="the service's state dir; the flash image under it is the "
                         "ground truth for whether the last shot's save happened")
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
    last = b""
    try:
        blank = _wait_for_drawing(args.api, sid)
        print("guest is drawing", flush=True)
        last = blank

        for i, name in enumerate(SHOTS):
            if i == 0:
                # Shot 0 is already on the panel: the tour enters it before the
                # first frame is drawn, so there is nothing to change from.
                frame = last
            else:
                _press_enter(args.api, sid)
                frame = _wait_for_change(args.api, sid, last, timeout=45)
            if name.startswith("gyro-"):
                # A gyro shot is a rate, not a pose: photographing the first
                # frame after the change would show the view a few degrees from
                # where it started. Four seconds of 40 deg/s is most of a turn,
                # and four seconds of pitch is into the clamp.
                time.sleep(4.0)
            else:
                # One more frame, so the photograph is of a settled state rather
                # than of the frame the change happened on.
                time.sleep(1.0)
            frame = _get_frame(args.api, sid) or frame
            path = out / f"{name}.png"
            path.write_bytes(frame)
            captured[name] = path
            last = frame
            print(f"  {name}: {len(frame)} bytes", flush=True)

            if name == "gyro-raw":
                # The last shot is the one that saves 100% / 90 degrees (the
                # shot before it), and it is also the one that shows the live
                # sample, so by here the save must have happened. Make
                # sure it was actually reached -- a dropped ENTER leaves the tour
                # a shot short, and then there is nothing in flash to come back
                # to -- and photograph it again once it has.
                for attempt in range(6):
                    if _nvs_saved(args.state_dir, sid):
                        break
                    print(f"  (shot 7 not reached yet, pressing again: {attempt + 1})",
                          flush=True)
                    _press_enter(args.api, sid)
                    time.sleep(4.0)
                if not _nvs_saved(args.state_dir, sid):
                    raise SystemExit("the tour never saved; refusing to photograph a "
                                     "persistence claim that flash does not support")
                time.sleep(4.0)      # pitch, into the clamp
                frame = _get_frame(args.api, sid) or frame
                captured[name].write_bytes(frame)
                print(f"  {name}: re-photographed in its saved state", flush=True)

                # Persistence, and not circular: this shot is the one that saves
                # 100% / 90 degrees to NVS. A reset then boots the tour again,
                # shot 0 reads NVS, and the card has to show the saved values.
                _request(args.api, "POST", f"/sessions/{sid}/reset", {})
                # The panel keeps the last frame it was given, so right after a
                # reset the endpoint still serves shot 7: wait for the guest to
                # draw something different, then for it to hold still, which is
                # what the settings card does and the world behind it does not.
                _wait_for_change(args.api, sid, last, timeout=240)
                frame = last
                stable = 0
                deadline = time.time() + 240
                while stable < 3 and time.time() < deadline:
                    time.sleep(1.5)
                    fresh = _get_frame(args.api, sid)
                    if fresh and fresh == frame:
                        stable += 1
                    else:
                        stable = 0
                        frame = fresh or frame
                path = out / "nvs-after-reset.png"
                path.write_bytes(frame)
                captured["nvs-after-reset"] = path
                print(f"  nvs-after-reset: {len(frame)} bytes", flush=True)
    finally:
        try:
            _request(args.api, "DELETE", f"/sessions/{sid}")
            print("session deleted", flush=True)
        except SystemExit:
            print(f"WARNING: could not delete session {sid}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
