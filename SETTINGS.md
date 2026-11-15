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

If all three fail, `GYRO` reads `NO SENSOR`, the row cannot be toggled, and the
two rows that depend on it are greyed. The game plays exactly as before.

**Off means off.** `motion::end()` calls `M5.Imu.sleep()` and stops sampling, so
a player who never turns it on pays nothing at runtime. (`m5::BMI270_Class` does
not implement `sleep()` in this library version, so the directly-addressed path
can only stop being read.)

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
 yawRateRad()    pitchRateRows()
     |                |
  radians/s        horizon rows/s      (rows/rad = the current projection)
     |                |
     +--> game::Input::gyroYaw / gyroPitch
                     |
            game::tick(), once per TICK_HZ
```

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
that build against the Device Service and writes `docs/emu/`:

| File | Shows |
|---|---|
| `settings.png` | the card, five rows, at the shipped defaults |
| `bright-10.png`, `bright-100.png` | the two ends of the backlight |
| `fov-60.png`, `fov-90.png` | the two ends of the field of view |
| `play-rest.png` | the reference view, no gyro |
| `gyro-yaw.png` | the same place after a faked 40 °/s yaw |
| `gyro-pitch.png` | ...and after a faked 25 °/s pitch, looking up |
| `nvs-persistence.txt` | the boot line after a session reset, reading the settings back |

## What still needs the real board

- **Which axis is which.** Yaw is taken from the gyro's Z and pitch from its X
  (see `readRates()`), and whether positive is left or right, up or down, is a
  question about how the part is soldered down. `YAW_SIGN` and `PITCH_SIGN` in
  `src/motion.h` are the whole fix if the first test turns the wrong way.
- **Drift after calibration** over a few minutes of standing still.
- **Ten distinguishable brightness steps**, and that 10% is still readable.
- **60–90° with no visible distortion** of a cube's proportions.
- **All four survive a reboot** on real NVS.
