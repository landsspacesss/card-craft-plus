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


def serial_text(api, sid):
    """The whole serial buffer. Read from zero every poll rather than tracking an
    offset: the buffer is small, a reset rewinds it, and an offset that outlives
    a reset is one more thing that can be wrong."""
    resp = _request(api, "GET", f"/sessions/{sid}/serial?offset=0&limit=200000")
    data = resp.get("data", "")
    if isinstance(data, list):
        data = "".join(data)
    return data


def wait_for(api, sid, needle, timeout, start=0):
    """Wait until the serial buffer contains `needle` at or after `start`."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = serial_text(api, sid)
        at = text.find(needle, start)
        if at >= 0:
            return text, at
        time.sleep(0.2)
    raise SystemExit(f"timed out waiting for {needle!r}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://127.0.0.1:8798")
    ap.add_argument("--bin", required=True, help="merged image to run")
    ap.add_argument("--out", default="docs/emu")
    ap.add_argument("--settle", type=float, default=1.4,
                    help="seconds to let a shot render before the frame is taken")
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
        text, _ = wait_for(args.api, sid, "TOUR 0", timeout=120)
        deadline = time.time() + 180
        while len(captured) < len(SHOTS) and time.time() < deadline:
            # Which shots have been announced so far?
            for i, name in enumerate(SHOTS):
                if name in captured:
                    continue
                if f"TOUR {i} {name}" not in text:
                    continue
                # Announce comes before the shot's own first frame, so let the
                # state machine run for a moment: the page has to be rebuilt and
                # the world redrawn behind it.
                time.sleep(args.settle)
                png = _request(args.api, "GET", f"/sessions/{sid}/frame.png")
                path = out / f"{name}.png"
                path.write_bytes(png)
                captured[name] = path
                print(f"  {name}: {len(png)} bytes -> {path}", flush=True)
            text = serial_text(args.api, sid)
            time.sleep(0.3)

        missing = [n for n in SHOTS if n not in captured]
        if missing:
            raise SystemExit(f"shots not captured: {missing}")

        # NVS persistence: the tour saved brightness and FOV through the same
        # saveSettings() the card does, so a reset has to bring them back.
        #
        # The boot line has already been printed once, so this waits for a
        # SECOND one rather than for the string: the serial buffer is appended
        # to across a QMP system_reset, and matching the first line would pass
        # without the guest ever having rebooted.
        before = serial_text(args.api, sid)
        seen = before.count("settings:")
        _request(args.api, "POST", f"/sessions/{sid}/reset", {})
        line = ""
        deadline = time.time() + 120
        while time.time() < deadline:
            text = serial_text(args.api, sid)
            fresh = text.count("settings:") > seen
            rewound = len(text) < len(before) and text.count("settings:") >= 1
            if fresh or rewound:
                at = text.rfind("settings:")
                end = text.find("\n", at)
                if end < 0:
                    time.sleep(0.2)   # half a line is not a reading
                    continue
                line = text[at:end].strip()
                break
            time.sleep(0.3)
        if not line:
            raise SystemExit("no settings line after the reset")
        print(f"after reset: {line}", flush=True)
        (out / "nvs-persistence.txt").write_text(
            "Settings survive a reset (the tour wrote them through the card's own\n"
            "saveSettings(), then the session was reset and the boot line read back):\n\n"
            f"  {line}\n", encoding="utf-8")
        if "fov=90" not in line:
            raise SystemExit(f"FOV did not survive the reset: {line}")
    finally:
        try:
            _request(args.api, "DELETE", f"/sessions/{sid}")
            print("session deleted", flush=True)
        except SystemExit:
            print(f"WARNING: could not delete session {sid}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
