# Card Craft 设置页 + 陀螺仪视角 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Card Craft 里加一个设置页（亮度 / FOV / 陀螺仪开关 / 灵敏度 / 校准），并让 BMI270 陀螺仪驱动左右 yaw 与上下 pitch 的视角，全部落 NVS。

**Architecture:** 三个新单元 + 三处改造。(1) `src/settings.{h,cpp}` 纯逻辑（默认值、夹取、亮度映射、FOV→plane/proj），可在主机上编译；(2) `src/motion.h` + `src/motion.cpp`（真机驱动：M5Unified IMU → BMI270 显式地址 → 失败）+ `src/motion_math.cpp`（纯数学：死区、灵敏度、° /s → rad/s 与 rows/s），驱动在 adv env 编、纯数学在 native env 编；(3) 陀螺仪积分不直接改相机，而是通过 `game::Input` 的两个新字段进入 `game::tick`，因此主机测试能整段验。FOV 从编译期常量改为运行期值，`raycast::planeLen()/proj()` 取代 `PLANE_LEN/PROJ`，并在设置变更时刷新一次相机。

**Tech Stack:** Arduino core 2.0.16、M5Unified 0.2.29（pin）、M5GFX 0.2.36（pin）、PlatformIO、Unity host tests、QEMU（端口 8798）。

**Spec:** `docs/superpowers/specs/2026-11-15-settings-gyro-design.md`

## Global Constraints

- 库必须继续 pin 版本目录：`lib/M5Unified@0.2.29`、`lib/M5GFX@0.2.36`。**无版本目录（0.3.0）会黑屏**（`Keyboard: Unsupported board type: 136`）。
- 构建一律 `export PATH=/home/landspace/breezybox/stage/pico8/venv/bin:$PATH` 后用 `pio run -e adv` / `pio run -e adv-tour` / `pio test -e native` / `test/host_sd/run.sh`，工作目录 `/home/landspace/card-craft-src`。
- 新代码 0 warning（`-Wall`）。全量已有的 5 条来自上游/库（raycast.cpp 3 条 `-Wmisleading-indentation`、render.cpp 1 条 `-Wclass-memaccess`、M5GFX 1 条 unused），不得新增。
- `game::State` 是 `trivially_copyable` 并由存档快照按 `sizeof(State)` 存取：**不得给 `State` 加字段**（加字段会让旧存档失效）。`game::Input` 不是存档内容，可以加字段。
- NVS 键固定在 `cardcraft` 命名空间：`br`、`fov`、`gyr`、`gs`。
- 设置页按键只准用现有 hal 映射：`;`/`.` → `navUp/navDown`，`,`/`/` → `navLeft/navRight`，ENTER → `confirmEdge`，`` ` `` → `cancelEdge`。**不改 hal 的键位**。
- 真机 / esptool / Launcher 一律不碰。仿真只用端口 8798 + `/home/landspace/espemu-state-cardcraft`，单实例，跑完 `DELETE /sessions/<id>` 并 `pkill -f "serve.py 8798"`。不碰 8790/8791/8795/8796/8797/8799。
- 交付：app-only `firmware.bin` 复制到 `/home/landspace/openpod/dist/`，只改 `/home/landspace/openpod/dist/UPLOAD-REPORT.md` 里 cardcraft 那一行。

## 已核对的代码事实（实现时直接用，不必重查）

- `src/raycast.h`：`VIEW_W 240`、`VIEW_H 135`、`PLANE_LEN 0.75f`、`PROJ 160.0f`、`TILT 30`、`HORIZON 37`、`PITCH_UP 310`、`PITCH_DOWN 250`、`AIM_SLOPE = TILT/PROJ`、`crosshairRow()` 是 `constexpr`。
- `PROJ` 的引用点只有：`raycast.cpp:258,283,372`、`render.cpp:609,610,1301,1566,1654`、`test_raycast.cpp`（多处）。`PLANE_LEN` 只在 `raycast.cpp:50`（`setAngle`）和注释里用。
- `crosshairRow()` 数学上恒等于 `VIEW_H/2`（`HORIZON + TILT`），与 PROJ 无关，现有测试 `test_raycast.cpp:377` 已经这么断言。
- `textures::TEX_N == 16`；`raycast.cpp:372` 的 `static_assert` 里 `PROJ` 换成最坏情况（FOV 90 → PROJ 120）后：`16*4096*17/120 = 9284 < 65536`，成立。
- 亮度：`src/hal/hal_cardputer.cpp:72` 固定 `M5Cardputer.Display.setBrightness(160)`（在 `hal::begin()` 里，显示初始化之后）。
- 视角：`game::tick` 每 tick `raycast::setAngle(s.cam, s.angle)`（`game.cpp:1967`），pitch 由 `s.pitch`（horizon 行号，float）经同一段代码 + `setPitch` 重建，限幅 `HORIZON±PITCH_UP/DOWN`（`game.cpp:1977-1987`）。`TICK_HZ 60`、`PITCH_SPEED 320` rows/s、`TURN_SPEED 2.7` rad/s。
- 暂停菜单：`main.cpp:317` `PAUSE_ROWS = 7`、`openPauseMenu()` 写 `s_items[0..6]`、`case SCR_PAUSE` 里 `switch (s_menu.index())` 逐个 `case 0..5` + `default`。
- `Screen` 枚举：`main.cpp:36`（`SCR_TITLE, SCR_PLAY, SCR_PAUSE, SCR_CRAFT, SCR_RECIPES, SCR_CONTROLS, SCR_SLOTS, SCR_DEAD`），`main.cpp:1082` 有 `static_assert(sizeof(kName)/sizeof(kName[0]) == SCR_DEAD + 1)` —— **新增 `SCR_SETTINGS` 必须同步那里的 `kName[]`**，否则构建失败（这是好事）。
- `ui::Menu`：`open/Move/index/draw`，`MAX_ROWS 4` 且自动滚动，`MenuItem{label, detail, enabled}`，`enabled=false` 会置灰整行。`render::present()` 是公开的。
- M5Unified 0.2.29 认识 `board_M5CardputerADV`：`In_I2C` 的 In-SCL=GPIO9、In-SDA=GPIO8；`M5.begin()` 里 `if (cfg.internal_imu && In_I2C.isEnabled()) M5.Imu.begin(&M5.In_I2C, M5.getBoard())`；`BMI270_Class` 先探测 0x68、失败换 0x69（默认地址常量是 0x69）；`BMI270_Class::getGyro` 已经返回 **°/s**（±2000 dps，`2000/32768`）；`M5.Imu.sleep()/wakeup()` 存在。
- 主机测试用 Unity，`pio test -e native` 直接编 `world.cpp raycast.cpp game.cpp`（**不含** render/ui/hal/motion.cpp）。

## Review Focus

1. **NVS 里是脏值**（旧固件写的、上次写到一半断电）：`br=0`、`fov=255`、`gs=9` 都不能让开机黑屏或 FOV 变 NaN。→ Task 1 的 `settings::decode` 测试。
2. **改了 FOV 但活着的相机没重建**：屏幕上的方块变扁、准星与命中点错位。→ Task 2 的 `setFovDeg` + `setPitch/setAngle` 测试。
3. **陀螺仪开着但没有传感器**（I2C 探测失败）：视角不能自己乱转或卡死。→ Task 4 的 `available()` 与 Task 5 的 `gyroYaw/gyroPitch == 0` 时状态不变测试。
4. **极快手速**（±2000 °/s）：`s.pitch` 必须被限幅在 `HORIZON±PITCH_UP/DOWN`，`s.angle` 不能溢出成 NaN/inf，相机方向向量保持单位长度。→ Task 5 的 clamp/NaN 测试。
5. **亮度调到 10%**：设置页本身还要能读（黑屏等于回不去）。→ Task 1 的亮度下限测试与 Task 7 的 10%/100% 对比截图。

---

### Task 1: 设置值的纯逻辑（默认 / 夹取 / 亮度 / FOV 映射）

**Files:**
- Create: `src/settings.h`, `src/settings.cpp`
- Test: `test/test_settings/test_settings.cpp`
- Modify: `platformio.ini`（`[env:native]` 的 `build_src_filter` 加 `+<settings.cpp>`，`build_flags` 加 `-I test/shim` 不需要，本文件不碰 Arduino）

**Interfaces:**
- Consumes: 无。
- Produces:
  - `enum class settings::Sens : uint8_t { LOW = 0, MED = 1, HIGH = 2 };`
  - `struct settings::Settings { uint8_t brightnessPct = 60; uint8_t fovDeg = 75; bool gyroOn = false; uint8_t gyroSens = 1; };`
  - `constexpr uint8_t settings::kBrightnessMin = 10, kBrightnessMax = 100, kBrightnessStep = 10, kFovMin = 60, kFovMax = 90, kFovStep = 5, kSensCount = 3;`
  - `void settings::sanitize(Settings& s);` —— 把任意值夹到合法域：`brightnessPct` 夹到 10..100 且向下取整到 10 的倍数；`fovDeg` 夹到 60..90 再就近取 5 的倍数；`gyroSens` >2 归 1。
  - `Settings settings::decode(uint8_t br, uint8_t fov, uint8_t gyr, uint8_t gs);` —— 直接产出 `sanitize` 过的值（`gyr != 0` 即开）。
  - `uint8_t settings::brightnessDuty(const Settings& s);` —— `pct*255/100`，夹 0..255。
  - `float settings::fovPlaneLen(uint8_t deg);` —— `tanf(deg*0.5f*PI/180)`。
  - `float settings::fovProj(uint8_t deg);` —— `(raycast::VIEW_W/2) / fovPlaneLen(deg)`（`settings.cpp` 只 include `raycast.h`，不 include Arduino）。

- [ ] **Step 1: 写失败的测试** `test/test_settings/test_settings.cpp`

```cpp
#include <math.h>
#include <unity.h>

#include "settings.h"

void test_defaults_are_the_shipped_ones(void) {
  settings::Settings s;
  TEST_ASSERT_EQUAL_UINT8(60, s.brightnessPct);
  TEST_ASSERT_EQUAL_UINT8(75, s.fovDeg);
  TEST_ASSERT_FALSE(s.gyroOn);
  TEST_ASSERT_EQUAL_UINT8(1, s.gyroSens);   // MED
}

void test_default_brightness_matches_the_old_fixed_160(void) {
  settings::Settings s;
  TEST_ASSERT_EQUAL_UINT8(153, settings::brightnessDuty(s));   // 60*255/100
}

// Review Focus 1: a card that has run an older build (or lost power mid-write)
// hands back whatever is in NVS. None of it may reach the panel unclamped.
void test_decode_clamps_garbage_to_something_usable(void) {
  settings::Settings low = settings::decode(0, 255, 1, 9);
  TEST_ASSERT_EQUAL_UINT8(10, low.brightnessPct);
  TEST_ASSERT_EQUAL_UINT8(90, low.fovDeg);
  TEST_ASSERT_EQUAL_UINT8(1, low.gyroSens);
  TEST_ASSERT_TRUE(low.gyroOn);

  settings::Settings high = settings::decode(255, 0, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(100, high.brightnessPct);
  TEST_ASSERT_EQUAL_UINT8(60, high.fovDeg);
  TEST_ASSERT_FALSE(high.gyroOn);
}

void test_brightness_and_fov_are_quantised(void) {
  settings::Settings s = settings::decode(64, 77, 0, 2);
  TEST_ASSERT_EQUAL_UINT8(60, s.brightnessPct);   // floor to a 10% step
  TEST_ASSERT_EQUAL_UINT8(75, s.fovDeg);          // nearest 5 degrees
}

void test_fov_keeps_squares_square(void) {
  for (uint8_t f = settings::kFovMin; f <= settings::kFovMax; f += settings::kFovStep) {
    const float plane = settings::fovPlaneLen(f);
    const float proj  = settings::fovProj(f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, plane * proj / 120.0f);  // VIEW_W/2 == 120
    TEST_ASSERT_TRUE(plane > 0.5f && plane < 1.05f);
  }
  // 75 degrees is a touch wider than the 0.75 the game shipped with, and the
  // projection follows it instead of staying at 160.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.767f, settings::fovPlaneLen(75));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 156.4f, settings::fovProj(75));
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_settings`
Expected: FAIL — `settings.h: No such file or directory`。

- [ ] **Step 3: 实现 `src/settings.h` / `src/settings.cpp`**

头文件里写 struct + 常量 + 声明，`.cpp` 里 `#include <math.h>` 与 `#include "raycast.h"`。`sanitize` 的算法：`brightnessPct = (clamp(br,10,100)/10)*10`；`fovDeg` 先 clamp 到 60..90，再 `((v - 60 + 2) / 5) * 5 + 60` 后二次 clamp；`gyroSens = gs <= 2 ? gs : 1`。`decode` 一次做完四件事并返回。

- [ ] **Step 4: 把 `+<settings.cpp>` 加进 `[env:native]` 的 `build_src_filter`，再跑**

Run: `pio test -e native -f test_settings`
Expected: 5 tests, 0 failures。

- [ ] **Step 5: 提交**

```bash
git add src/settings.h src/settings.cpp test/test_settings platformio.ini
git commit -m "settings: pure settings values, clamping and the FOV projection mapping"
```

---

### Task 2: FOV 从编译期常量改成运行期值

**Files:**
- Modify: `src/raycast.h`, `src/raycast.cpp`, `src/render.cpp`（5 处）、`src/game.cpp`（仅 2 条注释）
- Test: `test/test_raycast/test_raycast.cpp`（符号改名 + 新增 2 例）

**Interfaces:**
- Consumes: `settings::fovPlaneLen` 不在此用（raycast 不依赖 settings，避免环）。
- Produces:
  - `constexpr float raycast::PLANE_LEN_DEFAULT = 0.75f;`（保留旧值作注释性默认，不再被使用）
  - `constexpr float raycast::PROJ_DEFAULT = 160.0f;`
  - `constexpr float raycast::PLANE_LEN_MAX = 1.0f;`、`constexpr float raycast::PROJ_MIN = 120.0f;`
  - `float raycast::planeLen();` —— 当前半宽
  - `float raycast::proj();` —— 当前 `(VIEW_W/2)/planeLen()`
  - `uint8_t raycast::fovDeg();`
  - `void raycast::setFovDeg(uint8_t deg);` —— clamp 60..90，置 `planeLen = tan(deg/2)`；**不碰任何 Camera**（调用方负责刷新）
  - `constexpr int raycast::crosshairRow() { return VIEW_H / 2; }`
  - `constexpr float raycast::AIM_SLOPE = (float)TILT / PROJ_DEFAULT;`（只是 Camera 的静止默认值）

- [ ] **Step 1: 先写失败测试**（追加到 `test/test_raycast/test_raycast.cpp` 尾部；`setUp` 里没有钩子，所以每个用例自己 `setFovDeg(75)`）

```cpp
// The FOV is now a runtime value, so every fixture that cares has to say which
// one it wants. 75 is the shipped default and what the old 160 was derived from.
static void test_setFovDeg_updates_plane_and_proj_together(void) {
  setFovDeg(60);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5774f, planeLen());        // tan(30)
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 207.8f, proj());             // 120/0.5774
  setFovDeg(90);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, planeLen());           // tan(45)
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 120.0f, proj());
  setFovDeg(75);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.7673f, planeLen());
  // Out of range is clamped, not accepted: a stored 0 would make proj infinite.
  setFovDeg(0);
  TEST_ASSERT_EQUAL_UINT8(60, fovDeg());
  setFovDeg(255);
  TEST_ASSERT_EQUAL_UINT8(90, fovDeg());
  setFovDeg(75);
}

// Review Focus 2: the camera copies its plane out of the current FOV, so a FOV
// change only reaches the screen after setAngle. This pins that the pair moves
// together and that the crosshair stays on the centre row.
static void test_camera_plane_follows_the_current_fov(void) {
  setFovDeg(60);
  Camera c;
  c.px = 8.5f; c.py = 8.5f; c.z = 3.2f;
  setAngle(c, 0.7f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, planeLen(), sqrtf(c.planeX * c.planeX + c.planeY * c.planeY));
  setPitch(c, HORIZON);
  TEST_ASSERT_EQUAL_INT(VIEW_H / 2, crosshairRow());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)crosshairRow(), (float)c.horizon + c.aimSlope * proj());
  setFovDeg(90);
  setAngle(c, 0.7f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, sqrtf(c.planeX * c.planeX + c.planeY * c.planeY));
  setFovDeg(75);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_raycast`
Expected: 编译失败，`setFovDeg/planeLen/proj/fovDeg` 未声明。

- [ ] **Step 3: 改 `raycast.h` 与 `raycast.cpp`**

`raycast.h`：按 Interfaces 加常量、函数声明；`crosshairRow()` 改成 `VIEW_H / 2`；`Camera::aimSlope` 默认值仍是 `AIM_SLOPE`。`raycast.cpp`：文件作用域 `static float s_planeLen = PLANE_LEN_DEFAULT;`，实现 `planeLen()/proj()/fovDeg()/setFovDeg()`（`proj()` 现算，不缓存 —— 它只在每列/每 span 用一次，且缓存两份状态是 bug 的来源）；`setAngle` 用 `planeLen()`；`invNear`/`invFar` 改成读一次 `const float proj = proj();` 的局部变量；`static_assert` 的 `PROJ` 换成 `PROJ_MIN`。

- [ ] **Step 4: 改 `render.cpp` 的 5 处**

`raycast::PROJ` → 每处函数/循环前取一次 `const float proj = raycast::proj();`（`:609,610` 同一个 `proj`；`:1301,1566,1654` 各自在 `invD` 前取）。保持 `-Werror` 下无 unused 变量。

- [ ] **Step 5: 更新现有测试的符号引用并跑全量**

`test/test_raycast.cpp` 里 `PROJ` → `proj()`（允许先 `setFovDeg(75)` 再断言），`AIM_SLOPE` 保留。`test/test_game.cpp` 不用改（它不引 PROJ）。

Run: `pio test -e native`
Expected: 原 202 例 + 新增全部通过。

- [ ] **Step 6: 提交**

```bash
git add src/raycast.h src/raycast.cpp src/render.cpp src/game.cpp test/test_raycast/test_raycast.cpp
git commit -m "raycast: make the field of view a runtime value instead of two constants"
```

---

### Task 3: 陀螺仪纯数学（死区、灵敏度、单位换算、零偏）

**Files:**
- Create: `src/motion.h`, `src/motion_math.cpp`
- Test: `test/test_motion/test_motion.cpp`
- Modify: `platformio.ini`（native 加 `+<motion_math.cpp>`）

**Interfaces:**
- Consumes: `raycast::proj()`。
- Produces（全在 `namespace motion`，声明在 `motion.h`，不 include Arduino）：
  - `constexpr float DEADBAND_DPS = 2.0f;`
  - `constexpr float YAW_SIGN = 1.0f, PITCH_SIGN = 1.0f;`（真机联调时可翻，只此一处）
  - `float deadbandDps(float dps);` —— `|d| <= 2` 返回 0，否则 `sign(d) * (|d| - 2)`
  - `float sensFactor(uint8_t level);` —— 0→0.5、1→1.0、2→1.8、其它→1.0
  - `float yawRateRad(float dps, uint8_t level);` —— `deadbandDps * sensFactor * π/180 * YAW_SIGN`
  - `float pitchRateRows(float dps, uint8_t level);` —— `deadbandDps * sensFactor * π/180 * raycast::proj() * PITCH_SIGN`
  - `float averageBias(const float* samples, int n);` —— `n<=0` 返回 0
  - `float applyBias(float dps, float bias);` —— 减法

- [ ] **Step 1: 写失败的测试**

```cpp
#include <math.h>
#include <unity.h>

#include "motion.h"
#include "raycast.h"

void test_deadband_swallows_resting_noise_and_keeps_the_rest(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::deadbandDps(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::deadbandDps(2.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::deadbandDps(-2.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, motion::deadbandDps(2.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, motion::deadbandDps(-2.5f));
}

void test_sensitivity_levels_are_the_documented_ratios(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.5f, motion::sensFactor(0));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::sensFactor(1));
  TEST_ASSERT_EQUAL_FLOAT(1.8f, motion::sensFactor(2));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::sensFactor(9));   // junk level: MED
}

void test_rates_convert_to_the_units_the_simulation_wants(void) {
  raycast::setFovDeg(75);
  // 100 deg/s at MED is 1:1 with the hand: 1.745 rad/s.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.745f, motion::yawRateRad(100.0f, 1));
  // ...and 100 deg/s of pitch covers proj() rows per radian.
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 1.745f * raycast::proj(), motion::pitchRateRows(100.0f, 1));
  // Deadbanded input stays dead.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::yawRateRad(1.5f, 2));
}

// Review Focus (calibration): a stationary sensor averages to zero residual.
void test_average_bias_removes_a_constant_offset(void) {
  const float still[] = { 1.4f, 1.2f, 1.6f, 1.3f, 1.5f };
  const float bias = motion::averageBias(still, 5);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.4f, bias);
  for (float v : still) TEST_ASSERT_TRUE(fabsf(motion::applyBias(v, bias)) <= motion::DEADBAND_DPS);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::averageBias(still, 0));
}
```

- [ ] **Step 2: 跑测试确认失败** — `pio test -e native -f test_motion` → `motion.h` 不存在。

- [ ] **Step 3: 实现 `motion.h` + `motion_math.cpp`**，并把 `+<motion_math.cpp>` 加进 native 的 `build_src_filter`。

- [ ] **Step 4: 跑测试确认通过** — `pio test -e native -f test_motion` → 4 tests, 0 failures。

- [ ] **Step 5: 提交**

```bash
git add src/motion.h src/motion_math.cpp test/test_motion platformio.ini
git commit -m "motion: deadband, sensitivity and unit conversion, testable on the host"
```

---

### Task 4: BMI270 驱动（M5Unified 优先，失败显式地址）

**Files:**
- Modify: `src/motion.h`（追加驱动声明）, Create: `src/motion.cpp`
- Test: 无主机测试（真机硬件）；本任务的验证是 `pio run -e adv` 编译通过 + 0 warning。

**Interfaces:**
- Consumes: `motion::*` 纯数学（Task 3）。
- Produces:
  - `bool motion::begin();` —— 三级尝试，成功返回 true 并记录来源
  - `bool motion::available();`
  - `void motion::end();` —— `M5.Imu.sleep()`，之后 `available()` 仍为 true 但 `readRates` 不读（关开关时省电、不再占 I2C）
  - `void motion::calibrate();` —— 阻塞约 0.5 s 采样零偏，存 RAM
  - `bool motion::readRates(float& yawDps, float& pitchDps);` —— 已减零偏；不可用返回 false
  - `void motion::setSensitivity(uint8_t level);` —— 仅存值，供 `readRates` 使用？**不**：灵敏度在 main.cpp 换算时用，驱动只出原始 dps。此函数不实现（YAGNI）。
  - `#ifdef CRAFTCRAFT_GYRO_FAKE`：`void motion::fakeSet(float yawDps, float pitchDps);`

- [ ] **Step 1: 实现 `src/motion.cpp`**

三级探测，每级成功都 `Serial.printf("motion: imu via %s\n", ...)`：
1. `M5.Imu.isEnabled()` 已为真（`M5.begin()` 在 ADV 上已经用 In_I2C 起过）；
2. `M5.Imu.begin(&M5.In_I2C, M5.getBoard())` 后 `isEnabled()`；
3. `#include <utility/imu/BMI270_Class.hpp>`，`new m5::BMI270_Class(0x68, 400000, &M5.In_I2C)`（构造后 `begin(nullptr)`），失败换 0x69。**不**引入原始寄存器实现：0.2.29 的 `BMI270_Class` 已经包含 8 KB 的 BMI270 初始化 blob，自己再写一份是重复。
`readRates` 用 `s_imu->getGyro(&gx,&gy,&gz)`（第三级）/ `M5.Imu.getGyro(...)`（前两级），取 **gz→yaw、gx→pitch**，减零偏。两个轴的身份是真机联调项，写清楚注释与 `YAW_AXIS/PITCH_AXIS` 常量。
`calibrate()`：读 32 次、每次 `delay(15)`，分别对 yaw/pitch 求 `averageBias`。`CRAFTCRAFT_GYRO_FAKE` 下 `calibrate()` 直接成功、零偏 0。
`begin()` 在 `CRAFTCRAFT_GYRO_FAKE` 下不发任何 I2C 就返回 true（QEMU 没有 BMI270，真实探测会拖慢或失败）。

- [ ] **Step 2: 构建验证**

Run: `pio run -e adv 2>&1 | tail -20`
Expected: `SUCCESS`，`src/motion.cpp` 0 warning，flash 占比仍在 ~25%。

- [ ] **Step 3: 提交**

```bash
git add src/motion.h src/motion.cpp
git commit -m "motion: BMI270 gyro through M5Unified, falling back to an explicit address"
```

---

### Task 5: 陀螺仪进 `game::Input`，在 tick 里积分

**Files:**
- Modify: `src/game.h`（`Input` 加 2 字段）, `src/game.cpp`（tick 里应用）
- Test: `test/test_game/test_game.cpp`（新增 4 例）

**Interfaces:**
- Consumes: 无。
- Produces: `game::Input::gyroYaw`（rad/s，正 = 向左？见下）、`game::Input::gyroPitch`（rows/s，正 = 抬头）。语义：**每 tick 按 `1/TICK_HZ` 积分**，与现有 `in.left/right/lookUp/lookDown` 叠加。

- [ ] **Step 1: 写失败的测试**

```cpp
// Gyro rates ride in on Input rather than being integrated at the frame level,
// so the whole path -- rate, dt, clamp, camera -- is a host test.
void test_gyro_yaw_turns_the_camera_at_the_given_rate(void) {
  State s = fresh();
  Input in; in.gyroYaw = 1.0f;                  // rad/s
  for (int i = 0; i < 60; ++i) tick(s, in);     // one second
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, s.angle);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, s.cam.dx * s.cam.dx + s.cam.dy * s.cam.dy);
}

void test_gyro_pitch_is_clamped_at_the_same_limit_the_keys_are(void) {
  State s = fresh();
  Input in; in.gyroPitch = 320.0f;              // rows/s, as fast as the keys
  for (int i = 0; i < 600; ++i) tick(s, in);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(raycast::HORIZON + raycast::PITCH_UP), s.pitch);
  in.gyroPitch = -320.0f;
  for (int i = 0; i < 1200; ++i) tick(s, in);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(raycast::HORIZON - raycast::PITCH_DOWN), s.pitch);
}

// Review Focus 3: no sensor, or the gyro switched off, is a no-op -- not a slow
// drift and not a NaN.
void test_zero_gyro_rates_leave_the_view_exactly_where_it_was(void) {
  State s = fresh();
  s.angle = 0.25f; s.pitch = 40.0f;
  raycast::setAngle(s.cam, s.angle); raycast::setPitch(s.cam, 40);
  Input in;
  for (int i = 0; i < 120; ++i) tick(s, in);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, s.angle);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, s.pitch);
}

// Review Focus 4: a flick at the sensor's full scale must not poison the state.
void test_a_full_scale_flick_stays_finite_and_normalised(void) {
  State s = fresh();
  Input in; in.gyroYaw = 2000.0f * 3.14159f / 180.0f;   // 2000 deg/s
            in.gyroPitch = 5000.0f;
  for (int i = 0; i < 600; ++i) tick(s, in);
  TEST_ASSERT_TRUE(isfinite(s.angle));
  TEST_ASSERT_TRUE(isfinite(s.pitch));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, s.cam.dx * s.cam.dx + s.cam.dy * s.cam.dy);
  TEST_ASSERT_TRUE(s.pitch >= (float)(raycast::HORIZON - raycast::PITCH_DOWN) - 0.01f);
  TEST_ASSERT_TRUE(s.pitch <= (float)(raycast::HORIZON + raycast::PITCH_UP) + 0.01f);
}
```

- [ ] **Step 2: 跑测试确认失败** — `pio test -e native -f test_game` → `Input` 没有 `gyroYaw`。

- [ ] **Step 3: 实现**

`game.h` 的 `Input` 末尾加：
```cpp
  // Gyroscope rates, already deadbanded and scaled by the caller. rad/s and
  // horizon rows/s; integrated here so one path covers keyboard and gyro.
  float gyroYaw = 0.0f, gyroPitch = 0.0f;
```
`game.cpp` tick 的 look 段：`s.angle += in.gyroYaw / (float)TICK_HZ;` 放进现有 `if (in.left)/if (in.right)` 之后、`setAngle` 之前；pitch 段 `h += in.gyroPitch / (float)TICK_HZ;` 放在按键分支**之前**（这样 `lookCentre` 仍然能一键回正），限幅逻辑不动。

- [ ] **Step 4: 跑全量** — `pio test -e native` → 全绿（原 202 + Task1 5 + Task2 2 + Task3 4 + 本任务 4）。

- [ ] **Step 5: 提交**

```bash
git add src/game.h src/game.cpp test/test_game/test_game.cpp
git commit -m "game: integrate gyro rates in tick so the whole look path is host-testable"
```

---

### Task 6: 亮度走 HAL

**Files:**
- Modify: `src/hal/hal.h`（加声明）, `src/hal/hal_cardputer.cpp`（实现 + 底部）、`src/main.cpp`（setup 应用）

**Interfaces:**
- Consumes: 无。
- Produces: `void hal::setBrightness(uint8_t duty);`（0..255，>0 之外不做业务判断；调用方保证 >= 25）

- [ ] **Step 1: 实现**

`hal_cardputer.cpp` 新增：
```cpp
void setBrightness(uint8_t duty) { M5Cardputer.Display.setBrightness(duty); }
```
`begin()` 里保留 `setBrightness(160)` 作为显示起来后、读 NVS 之前的初始值（NVS 在读 Preferences 之后才可用，而 `hal::begin()` 在 `s_prefs.begin` 之前）。

- [ ] **Step 2: 构建** — `pio run -e adv 2>&1 | tail -5` → SUCCESS。

- [ ] **Step 3: 提交** — `git commit -m "hal: brightness is a board concern, so route it through hal"`。

---

### Task 7: 设置页 UI（暂停菜单第 8 行 + `SCR_SETTINGS`）

**Files:**
- Modify: `src/main.cpp`
- Test: 无主机测试（UI 在设备上）；验证靠 Task 9 的仿真截图与构建。

**Interfaces:**
- Consumes: `settings::*`、`raycast::setFovDeg/planeLen/proj`、`hal::setBrightness`、`motion::*`。
- Produces: `SCR_SETTINGS` 页面；`s_cfg`（`settings::Settings`）；`applySettings()`（把 s_cfg 推给 hal/raycast/motion）；`loadSettings()/saveSettings()`。

- [ ] **Step 1: 全局状态与 NVS 读写**

`main.cpp` 匿名 namespace 里加 `settings::Settings s_cfg;`、`ui::Menu s_setMenu;`、`ui::MenuItem s_setItems[5];`、5 个 `char` 标签缓冲（各 24 B）、`int s_setCursor` 不需要（Menu 自带）、`uint8_t s_calib`（0 空闲 / 1 待校准 / 2 OK / 3 FAIL）+ `int s_calibFrames`。
`loadSettings()`：`settings::decode(s_prefs.getUChar("br",60), getUChar("fov",75), getUChar("gyr",0), getUChar("gs",1))` → `s_cfg`。`saveSettings()`：四个 `putUChar`。
`applySettings(bool refreshCam)`：`hal::setBrightness(settings::brightnessDuty(s_cfg))`；`raycast::setFovDeg(s_cfg.fovDeg)`；若 `refreshCam` 则 `raycast::setPitch(s_game.cam, (int)(s_game.pitch + 0.5f))`（FOV 变了必须重建，否则 `aimSlope` 还是旧 proj 的）；`gyroOn` 变化时 `motion::begin()` / `motion::end()`。
在 `setup()` 里 `s_prefs.begin` 之后、`s_lastUs = micros()` 之前调 `loadSettings(); applySettings(false);`，并打一行 `Serial.printf("settings: br=%u fov=%u gyro=%u sens=%u\n", ...)`（仿真里持久化验证就靠它）。

- [ ] **Step 2: 暂停菜单加一行**

`PAUSE_ROWS` 7 → 8，`s_items[8]`；行序：RESUME / CONTROLS / SOUND / FPS / **SETTINGS** / SAVE GAME / NEW WORLD / QUIT；`case SCR_PAUSE` 的 switch 变成 `case 0..6` + `default`（旧 `case 4,5` 各后移一位，`default` 仍是 QUIT），`case 4` 进设置页：`openSettings(); s_scr = SCR_SETTINGS; s_lockout = LOCKOUT_FRAMES;`。

- [ ] **Step 3: 设置页本身**

`openSettings()` 重建 5 行标签（`s_setItems[i].enabled` 按可用性设置）：
```
BRIGHTNESS: 60%   detail "10-100%, step 10"
FOV: 75           detail "60-90 deg, step 5"
GYRO: OFF         detail "tilt to look"
GYRO SENS: MED    detail "low / med / high"      disabled: !s_cfg.gyroOn
CALIBRATE         detail "hold still, half a sec" disabled: !s_cfg.gyroOn
```
`openSettings()` 调 `s_setMenu.open("SETTINGS", s_setItems, 5)`。陀螺仪开着但 `!motion::available()` 时 GYRO 行显示 `GYRO: NO SENSOR`（`enabled=false`）。
`case SCR_SETTINGS`：背景 `drawScene(); ui::hud(s_game);`（和暂停页一致，世界留在后面），`s_setMenu.draw(setFoot())`；`!s_lockout` 时：
- `b.navUp/navDown` → `s_setMenu.move(∓1)` + `sfx::kMenuMove`
- `b.navLeft/b.navRight` → 改当前行（亮度 ∓10、FOV ∓5、灵敏度 ‑/＋ 循环、GYRO/SENS/CALIBRATE 无操作），改完 `saveSettings(); applySettings(true); openSettings();`
- `confirm` → 行 2 切 GYRO 开关（开→`applySettings`），行 4 启动校准
- `cancel` → `s_scr = SCR_PAUSE; openPauseMenu();`（并 `s_lockout = LOCKOUT_FRAMES`）
按键边沿处理沿用现有约定（`navUp` 等是边沿；`navLeft/navRight` 也是边沿，逐次 ±1 档）。
`setFoot()`：`snprintf(foot, sizeof(foot), "%s MOVE  %s CHG  %s BACK", hal::caps().kMove, hal::caps().kTurn, hal::caps().kBack)`（31 字符，与 `menuFoot()` 同量级）。
校准：`confirm` 在 CALIBRATE 行 → `s_calib = 1`；下一帧先画页面 + `render::present()` 让 "HOLD STILL" 真的上屏，再 `motion::calibrate()`，置 `s_calib = ok?2:3; s_calibFrames = 90;`，行标签临时改成 `CALIBRATE: OK`/`CALIBRATE: FAIL`，90 帧后恢复。

- [ ] **Step 4: `kName[]` 同步**

`main.cpp:1082` 附近那张 `kName[]`（DEV_SERIAL 的屏幕名表）加 `SCR_SETTINGS` 对应项，`static_assert` 自然满足。

- [ ] **Step 5: 构建** — `pio run -e adv 2>&1 | tail -20` → SUCCESS、0 新 warning、flash < 30%。

- [ ] **Step 6: 提交** — `git commit -m "ui: a settings page for brightness, FOV and the gyro"`。

---

### Task 8: 把陀螺仪接进主循环

**Files:**
- Modify: `src/main.cpp`（`case SCR_PLAY`）

**Interfaces:**
- Consumes: `motion::readRates`、`motion::yawRateRad/pitchRateRows`、`game::Input::gyroYaw/gyroPitch`。

- [ ] **Step 1: 实现**

`case SCR_PLAY` 里构造 `Input in{}` 之后、喂给 `game::tick` 之前：
```cpp
float yawDps = 0.0f, pitchDps = 0.0f;
if (s_cfg.gyroOn && s_calib != 1 && motion::readRates(yawDps, pitchDps)) {
  in.gyroYaw   = motion::yawRateRad(yawDps, s_cfg.gyroSens);
  in.gyroPitch = motion::pitchRateRows(pitchDps, s_cfg.gyroSens);
}
```
一帧只读一次率（不是每 tick），符合"每帧读取"。

- [ ] **Step 2: 构建** — `pio run -e adv 2>&1 | tail -5` → SUCCESS。

- [ ] **Step 3: 提交** — `git commit -m "main: feed the gyro into the simulation each frame"`。

---

### Task 9: 仿真证据（设置页 / 亮度 / FOV / 假陀螺仪 / NVS）

**Files:**
- Modify: `platformio.ini`（新增 `[env:adv-tour]`）, `src/main.cpp`（`#ifdef CRAFTCRAFT_EMU_TOUR` 的巡演块）
- Create: `tools/capture-tour.py`（写进仓库，别人能复现）
- Output: `docs/emu/settings-page.png`、`bright-10.png`、`bright-100.png`、`fov-60.png`、`fov-90.png`、`gyro-yaw.png`、`gyro-pitch.png`

**Interfaces:**
- Consumes: 全部前面任务。
- Produces: `[env:adv-tour]` = `[env:adv]` + `-DCRAFTCRAFT_GYRO_FAKE -DCRAFTCRAFT_EMU_TOUR`。巡演块在 `loop()` 开头：累计帧数，每 120 帧切到下一"景"，并 `Serial.printf("TOUR %d %s\n", n, name)`；每景设定 `s_cfg` 字段、`motion::fakeSet(yaw,pitch)`、`applySettings(true)`，需要页面时 `openSettings(); s_scr = SCR_SETTINGS;`，需要世界时 `startRun()`。景序固定：`settings`（br 60/fov 75/gyro off）、`bright-10`、`bright-100`（都在设置页）、`fov-60`、`fov-90`（设置页 + 世界背景）、`gyro-yaw`（PLAY，fake yaw 60 dps）、`gyro-pitch`（PLAY，fake pitch 45 dps）。

- [ ] **Step 1: 加 env 与巡演块**，`pio run -e adv-tour 2>&1 | tail -5` → SUCCESS。
- [ ] **Step 2: 起仿真（单实例 8798）**

```bash
cd /tmp/espemu && M5_OPEN_BROWSER=0 M5_SERVICE_MIN_FREE_BYTES=$((256*1024*1024)) \
  nohup python3 platform/tools/serve.py 8798 --state-dir /home/landspace/espemu-state-cardcraft \
  > /tmp/cc-emu8798.log 2>&1 &
```
`python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 merge_bin -o /tmp/cc-tour.bin --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 .pio/build/adv-tour/bootloader.bin 0x8000 .pio/build/adv-tour/partitions.bin 0x10000 .pio/build/adv-tour/firmware.bin`
上传 artifact → firmware(kind=merged) → session → 按 `TOUR` 行的时间点抓 `frame.png`。

- [ ] **Step 3: NVS 持久化**：在设置页把 FOV 从 75 改到 90（巡演里用一次 `s_cfg.fovDeg=90; saveSettings();` 即可模拟"改设置"），`POST /sessions/<id>/reset`，读新 boot 的 serial，断言 `settings: br=... fov=90 ...`。把日志尾存进 `docs/emu/nvs-persistence.txt`。
- [ ] **Step 4: 收尾** — `DELETE /sessions/<id>`、`pkill -f "serve.py 8798"`，确认没有残留 `qemu-system-xtensa`。
- [ ] **Step 5: 看图**，确认设置页 5 行都在、10% 仍可读、FOV 60/90 明显不同、假陀螺仪两张图的视角与静止图不同；不合格就回去改再拍。
- [ ] **Step 6: 提交** — `git add -A && git commit -m "emu: a tour build that photographs the settings page, the FOV extremes and the fake gyro"`。

---

### Task 10: 交付

**Files:**
- Modify: `/home/landspace/openpod/dist/UPLOAD-REPORT.md`（只改 cardcraft 行）
- Copy: `.pio/build/adv/firmware.bin` → `/home/landspace/openpod/dist/cardcraft-settings.bin`

- [ ] **Step 1: 出正式产物** — `pio run -e adv`，确认 `firmware.bin` 是 app-only：`xxd -s 32 -l 4` = `32 54 cd ab`（0xABCD5432）且 chip id 9；`md5sum` 记下大小与 md5。
- [ ] **Step 2: 主机测试全过** — `pio test -e native` + `test/host_sd/run.sh`，记下条数。
- [ ] **Step 3: 复制 + 更新 UPLOAD-REPORT 行**（新字节数 / md5 / 证据：主机测试条数、7 张仿真截图、NVS 持久化日志）。
- [ ] **Step 4: 提交并通知** — 仓库内 `git commit`；发 `[UPLOAD-REPORT] DONE cardcraft-settings` 给统领（mesh，agentId `f9d22b30-8e5f-4cbd-b84a-642b074f82f1`，host `srv_WQkf5QAnULpj`），附待真机验证清单（陀螺仪轴与符号、校准漂移、亮度 10 档、FOV 60-90 不变形、重启后设置保留）。

---

## 自审记录

- **规格覆盖**：§4.1→Task 1/7，§4.2→Task 2，§4.3→Task 3/4/8，§4.4→Task 7，§4.5→Task 8，§5.1→Task 1/2/3/5，§5.2→Task 9，§5.3→Task 10 的真机清单，§8→Task 10。
- **与规格的偏差（有意，写在这里以免执行者困惑）**：
  1. NVS 的读写留在 `main.cpp`（`loadSettings/saveSettings`），`settings.cpp` 只放能在主机上编译的纯逻辑 —— `Preferences` 在 native env 里不存在，而 `settings.cpp` 必须能被主机测试编。规格 §4.1 的函数拆分因此是 `settings::decode/sanitize` + main 里的四行读写。
  2. 陀螺仪积分放在 `game::tick` 而不是在 `main.cpp` 里改相机：`s.pitch` 是模拟的权威状态，tick 每步都会用它重建 `cam.horizon`，在外面改相机会被下一步覆盖。积分进 `Input` 也让它可被主机测试。
  3. `PITCH_UP/PITCH_DOWN` 保持像素常量（±280 px shear），不随 FOV 缩放：于是俯仰角上限在 FOV 60 时约 67°、90 时约 49°，与规格的"沿用现有限幅"一致，代价是上限角度随 FOV 轻微变化。
  4. 陀螺仪"关闭时释放"实现为 `M5.Imu.sleep()` + 停止读取；BMI270 是共享 I2C 上的常驻芯片，完全断电需要额外 GPIO，不在本规格内。
- **类型一致性**：`settings::Settings.brightnessPct/fovDeg/gyroOn/gyroSens`、`motion::readRates(float&,float&)`、`game::Input::gyroYaw/gyroPitch`、`raycast::planeLen()/proj()/setFovDeg()` 在各任务间同名同型。
