# Settings — brightness, field of view, and the gyroscope

A card of its own, reached from the pause menu: `ESC` → `SETTINGS`. It draws the
world behind it, because both of the things it changes are things you judge by
looking at what they change.

| Row | Keys | What it does |
|---|---|---|
| `BRIGHTNESS` | `<` `>` | 10–100% in tens. Applied to the panel immediately |
| `FOV` | `<` `>` | 60–90°, in fives. Applied immediately |
| `GYRO` | `ENTER` | Tilt the board to look around |
| `GYRO SENS` | `<` `>` | LOW / MED / HIGH = 0.5× / 1× / 1.8× |
| `CALIBRATE` | `ENTER` | Averages half a second of stillness into the zero-rate offset |

`^` `v` move the cursor, `<` `>` change the row, `ESC` goes back to the pause
card. The pause card's `SOUND` and `FPS` rows stay where they were: this card is
for things that change how the game *looks*, and those two change how it sounds
and reads.

## What is stored, and where

Four bytes in the `cardcraft` NVS namespace — the same namespace as `best`,
`snd2` and `fps1`:

| Key | Meaning | Default |
|---|---|---|
| `br` | brightness, percent | 60 (the old hardcoded 160/255) |
| `fov` | field of view, degrees | 75 (about the old 0.75 plane) |
| `gyr` | gyro enabled, 0/1 | 0 — off |
| `gs` | sensitivity, 0/1/2 | 1 (MED) |

Everything read out of NVS goes through `settings::decode()`, which clamps and
quantises. A card that ran an older build, or lost power mid-write, hands back
whatever it has; a `br` of 0 would be a black screen with no card on it to fix
that, so 10 is the floor and the value is never used unclamped. The host tests in
`test/test_settings` are mostly about that case.

## The gyroscope

The Cardputer ADV carries a Bosch BMI270 on the keyboard's I2C bus (SCL 9,
SDA 8). `src/motion.cpp` brings it up in three steps, cheapest first:

1. the IMU `M5.begin()` already started, if it did;
2. `M5.Imu.begin()` explicitly;
3. the same `m5::BMI270_Class` driver addressed directly at **0x68**, then
   **0x69** — the two addresses in circulation — bypassing board detection.

The third step is not a second driver: this library version already carries the
BMI270's 8 KB initialisation blob, and a second copy of it is how a gyro ends up
half-configured.

If all three fail, `GYRO` reads `NO SENSOR` and the two rows that depend on it
are greyed. The row itself still toggles, because that is the only way back to
`OFF`. The game plays exactly as before.

**Off means off.** `motion::end()` stops sampling and sleeps whatever is
running — deliberately not just what `begin()` started, because M5Unified's own
`M5.begin()` starts the IMU on this board and that sensor is the one worth
sleeping. (`m5::BMI270_Class` does not implement `sleep()` in this library
version, so the directly-addressed fallback can only stop being read.)

**Calibration.** A BMI270 at rest reads 0.5–9 °/s of zero-rate offset, which is
hundreds of pixels a second of drift at these sensitivities. `CALIBRATE` averages
32 samples over about half a second and subtracts the result. Below 2 °/s the
reading is thrown away entirely — the deadband — so a calibrated, still board
does not creep at all. The offset lives in RAM: every boot needs one calibration,
which is a press in the settings card.

## How a rate becomes a view

This is the part with the tests (`test/test_motion` and four cases in
`test/test_game`), because it is the part that cannot be checked by looking at
the sensor.

```
        gyro, deg/s
             |
   deadbandDps()          zero inside ±2 deg/s, and continuous at the edge
             |
     sensFactor()         0.5 / 1.0 / 1.8
             |
     +-------+--------+
     |                |
 turnRateRad()   tiltRateRows()
     |                |
  radians/s        horizon rows/s      (rows/rad = the current projection)
     |                |
     +--> game::Input::gyroYaw / gyroPitch
                     |
            game::tick(), once per TICK_HZ
```

**Which axis is which, and why it is a table.** The first device settled this
the hard way: a gyro's **Z** is the axis out of the package's top face, which on
this board is the axis a steering-wheel twist turns — so taking the turn from it
made a twist move the view and turning about the world's vertical do nothing.
The axis a player means by "look left" is the in-plane one lying along the
panel's short side, which is **Y**. So turn ← Y, tilt ← X, and the pair lives in
`settings::gyroTurn` / `settings::gyroTilt` as two bytes (`gt`, `gp`) rather than
as scattered signs: a board mounted differently is a value, not a patch.

The settings card's `GYRO` row shows the live sample as its second line —
`gx -12.3 gy +45.6 gz  +3.4` — so "which axis moved when I turned?" is answered
on the panel instead of in a conversation.

The rates go into `game::Input` and are integrated inside `game::tick()`, not
applied to the camera from the frame loop. Two reasons, both load-bearing:
`s.angle` and `s.pitch` are the simulation's own state and `tick()` rebuilds the
camera from them every step, so anything that moved the camera from outside would
be overwritten by the next tick; and it puts the whole path — rate, dt, deadband,
clamp, camera — under the host tests, where a sensor cannot be.

Pitch is clamped by the same `HORIZON ± PITCH_UP/DOWN` the keys are, and it is
clamped as **rows of shear**, so the angles it reaches depend a little on the
field of view (about ±67° at 60°, ±49° at 90°). `ENTER` still recentres, and wins
over the gyro for that frame.

## Field of view

`PLANE_LEN` and `PROJ` were compile-time constants that had to agree with each
other. They are one runtime value now: `raycast::setFovDeg(deg)` sets the
camera plane's half-width to `tan(fov/2)` and derives `proj()` from it as
`(VIEW_W/2) / planeLen()`, which is what keeps a block square on screen at any
width. `test/test_raycast` pins that the pair still agree.

`Camera` copies its plane out of the current FOV when `setAngle()` runs, so a
change only reaches a camera that is drawn afterwards. In the settings card that
is why `applySettings(true)` rebuilds both `setAngle` and `setPitch` on the game's
camera: while paused nothing is ticking it, and the world behind the card would
otherwise keep the old width until play resumed.

Nothing recomputes per frame — the walker reads `proj()` once per column and the
renderer once per pass.

## Emulator evidence

`pio run -e adv-tour` builds the game plus a shot list (`CRAFTCRAFT_EMU_TOUR`)
and a fake gyro (`CRAFTCRAFT_GYRO_FAKE`), because the emulator's keyboard cannot
open the card and QEMU does not model the BMI270. `tools/capture-tour.py` drives
that build against the Device Service and writes `docs/emu/`.

The tour advances on **ENTER**, not on a clock. A clock was tried twice and
cannot work: the panel endpoint serves the last frame the guest actually finished
drawing, and under load that is many seconds behind, so a schedule photographs
whichever shot the guest last completed. ENTER is the one key this emulator does
deliver (and it needs `hold_seconds` on the request — without it the service
takes a branch that queues nothing the guest ever sees).

| File | Shows |
|---|---|
| `settings.png` | the card at the shipped defaults (five rows; four fit, so CALIBRATE is behind the chevron) |
| `bright-10.png`, `bright-100.png` | the two ends of the backlight, **as the card reports them** — the emulator's frame is the framebuffer and a backlight is a register, so the pixels themselves cannot dim |
| `fov-60.png`, `fov-90.png` | the two ends of the field of view: the label and the world behind the card, which is drawn from the same camera |
| `play-rest.png` | the reference view, gyro off |
| `gyro-yaw.png` | the same place after a faked 40 °/s yaw: the view has turned, and nothing else on the panel changed |
| `gyro-pitch.png` | ...and after a faked 25 °/s pitch: looking up, clamped by the same rows the keys use |
| `nvs-after-reset.png` | the card after the session was reset — **100% / 90° read back out of NVS** |

The persistence shot is deliberately not circular: the tour's last shot is the
only one that calls `saveSettings()`, so the picture after the reset is either
the saved values (flash held them) or the 60% / 75° defaults (it did not). Shot 0
never writes.

## What still needs the real board

- **The turn direction.** The axis is settled (Y), but whether positive Y should
  turn left or right has not been checked against the board: if it is backwards,
  `settings::gyroTurn`'s default becomes `Y-` — one byte, no code change. The
  same holds for tilt and `X+`.
- **Drift after calibration** over a few minutes of standing still.
- **Ten distinguishable brightness steps**, and that 10% is still readable.
- **60–90° with no visible distortion** of a cube's proportions.
- **All four survive a reboot** on real NVS.
