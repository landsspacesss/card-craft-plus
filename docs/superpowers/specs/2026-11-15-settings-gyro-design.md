# Card Craft 设置页 + 陀螺仪视角 — 设计规格

- 日期：2026-11-15
- 状态：已批准，待实现
- 目标设备：M5Stack Cardputer ADV（ESP32-S3FN8，8 MB flash，320 KB RAM，无 PSRAM，240×135）
- 分工：统领只写规格与协调；实现由 cardcraft 实现 agent 全权负责

## 1. 目标

在 Card Craft 中加入一个新的设置页，并提供陀螺仪四向视角控制：

- 退出到暂停菜单 → `SETTINGS` → 新页面
- 新增项：`BRIGHTNESS`、`FOV`、`GYRO` 开关、`GYRO SENS`、`CALIBRATE`
- 现有 `SOUND` / `FPS` 继续留在暂停菜单，不搬
- 陀螺仪控制视角的**左右（yaw）与上下（pitch）**，键盘视角照常可用
- 所有设置持久化到 NVS

## 2. 现有代码事实（实现前先核对）

- 亮度：`src/hal/hal_cardputer.cpp:72` 固定 `M5Cardputer.Display.setBrightness(160)`。
- FOV：`src/raycast.h` 中 `PLANE_LEN = 0.75f`（约 73°）、`PROJ = 160.0f`；两者在 `raycast.cpp` 的相机与投影计算中广泛使用，是编译期常量。
- Pitch：`raycast::setPitch(Camera&, int horizon)` 已存在，限幅 `HORIZON ± PITCH_UP/DOWN`（约 ±60° 的 y-shear）；`Camera::horizon`、`aimSlope` 已支持。键盘 pitch 走 `game.cpp` 的 `s.pitch` 与 `setPitch`。
- 暂停菜单：`main.cpp` 的 `openPauseMenu()`，`s_items[7]`、`PAUSE_ROWS = 7`，`ui::Menu` 支持滚动。
- NVS：`Preferences s_prefs("cardcraft")`，已有 `best/snd2/fps1/splash/sdtest` 等键。
- 构建：Arduino core 2.0.16；库必须 pin `M5Unified@0.2.29` / `M5GFX@0.2.36`（0.3.0 黑屏）。参考 `tools/link-local-libs.sh`。

## 3. 硬件事实（BMI270）

- Cardputer ADV 板载 **Bosch BMI270** 六轴 IMU（加速度计 + 陀螺仪）。
- I2C：SDA/SCL 在 **GPIO 8/9**；地址尝试 **0x68，再 0x69**（不同来源不一致，需运行时探测）。
- 陀螺仪默认关闭；开启后约 **±2000 °/s、100 Hz**。静止零偏约 **0.5–9 °/s**，必须校准。
- M5Unified 0.2.29 自带 BMI270 驱动，但其板级自动配置未必覆盖 ADV；优先尝试 M5Unified 的 IMU 接口，失败则直接 I2C 读 BMI270 寄存器。
- QEMU 的 `m5cardputeradv` **不模拟 BMI270**；仿真验证必须用编译开关注入假角速度，真机再验真实传感器。

## 4. 设计

### 4.1 设置模块（`src/settings.h/.cpp`）

运行期结构：

```cpp
struct Settings {
  uint8_t brightnessPct;  // 10..100, step 10
  uint8_t fovDeg;         // 60..90, step 5
  bool    gyroOn;         // default off
  uint8_t gyroSens;       // 0 low, 1 med, 2 high
};
```

- 默认：亮度 60%（≈现有 160/255）、FOV 75°、陀螺仪关、灵敏度 MED。
- NVS 键：`br`、`fov`、`gyr`、`gs`（沿用 `cardcraft` 命名空间）。
- `settings_load()` 在 `setup()` 里、显示初始化后尽早调用并应用亮度/FOV。
- `settings_save()` 在每次修改后写 NVS（或退出设置页时统一写，需防抖；第一版直接写即可）。
- 亮度映射：`pct * 255 / 100`，夹到 0..255；改值立即 `setBrightness()`。
- FOV：`PLANE_LEN = tan(fovDeg/2 * π/180)`；`PROJ = (VIEW_W/2) / PLANE_LEN`，保证方块不变形。

### 4.2 FOV 运行期化（`raycast.h/.cpp`）

- 把 `PLANE_LEN`、`PROJ` 从 `constexpr` 改为运行期可设置值（保留原值作为默认）。
- 提供 `raycast::setFovDeg(uint8_t)`（或等价接口），内部同时更新 plane/proj。
- 检查 `raycast.cpp`/`game.cpp`/`main.cpp` 中所有引用，确保没有残留编译期常量；主机测试同步更新。
- 不做逐帧重新计算；只在设置变化时更新。

### 4.3 运动模块（`src/motion.h/.cpp`）

接口建议：

```cpp
namespace motion {
bool begin();                 // 探测 BMI270（M5Unified 优先，失败走 I2C 0x68/0x69）
bool available();
void calibrate();             // 静止采样 ~0.5s，记录零偏；期间显示状态
bool readRates(float& yawRate, float& pitchRate);  // °/s，已减零偏
void setSensitivity(uint8_t level);
}
```

- 仅在 `settings.gyroOn` 时 `begin()`；关闭时释放/停止，避免约 1 mA 功耗。
- 每帧（PLAY 状态）读取角速度：过 ~2 °/s 死区，乘灵敏度系数（LOW/MED/HIGH，例如 0.5/1.0/1.8），按 `dt` 积分。
- yaw：更新 `cam.dx/dy` 并归一化；pitch：`setPitch(cam, cam.horizon - pitchRate * dt * k)`，沿用现有 ±60° 限幅。
- 校准结果保存在 RAM；第一版不必入 NVS（每次开机静止校准一次）。
- 编译开关 `CRAFTCRAFT_GYRO_FAKE`：在仿真里注入固定/可配角速度，用于 UI 与积分验证；真机不走该分支。

### 4.4 设置页 UI（`main.cpp` / `ui.cpp`）

- 暂停菜单新增 `SETTINGS` 行（建议在 `FPS` 之后、`SAVE GAME` 之前），`PAUSE_ROWS` 与 `s_items` 数组同步扩到 8。
- 新 `SCR_SETTINGS` 页面，行：
  - `BRIGHTNESS: NN%`
  - `FOV: NN°`
  - `GYRO: ON/OFF`
  - `GYRO SENS: LOW/MED/HIGH`
  - `CALIBRATE`（进入后提示 "HOLD STILL"，约 0.5s 后显示 OK/FAIL）
- 按键：
  - `;` / `.` 上下移动
  - `,` / `/` 调整当前行（亮度 ±10%、FOV ±5°、灵敏度循环、GYRO 切换）
  - `Enter` = 切换/执行（GYRO 开关、CALIBRATE）
  - `` ` `` 返回暂停菜单
- 改亮度/FOV 立即生效；页面底部显示当前选中项的提示（可选）。
- 陀螺仪关闭时 `GYRO SENS`/`CALIBRATE` 置灰或跳过。

### 4.5 主循环接入

- `SCR_PLAY` 每帧：若 `gyroOn && available()`，读 rates，积分 yaw/pitch；键盘输入照旧叠加。
- 校准期间暂停积分。
- 不改动游戏物理/碰撞；只改相机朝向与 horizon。

## 5. 验收

### 5.1 主机测试（`pio test -e native` / 现有 host 测试）
- FOV：`setFovDeg(60/75/90)` 后 `PLANE_LEN`、`PROJ` 与方块比例正确。
- 积分：给定角速度序列、dt、死区、灵敏度、限幅，yaw/pitch 结果符合预期。
- 校准：静止样本平均后，读数为 0（在死区内）。
- 设置：序列化/反序列化与默认值；现有 202 例不回归。

### 5.2 仿真（端口 8798，state dir `espemu-state-cardcraft`）
- 设置页截图；亮度 10%/100% 对比截图；FOV 60°/90° 对比截图。
- `CRAFTCRAFT_GYRO_FAKE` 下：注入 yaw/pitch 角速度，截图显示视角变化（左右/上下各一张）。
- NVS 持久化：改设置 → 保存 virtual-flash → 新会话加载，值保留。
- 注意：QEMU 不模拟 BMI270，真实传感器路径无法在仿真验证。

### 5.3 真机（用户执行）
- 陀螺仪四向控制：左右倾斜转身、上下倾斜看天/看地，方向与幅度正确。
- 校准：静止 0.5s 后无明显漂移；长时间静止漂移可接受。
- 亮度 10 档可辨；FOV 60–90° 不变形。
- 重启后设置保留；陀螺仪关闭时无额外功耗/无漂移。

## 6. 非目标

- 加速度计玩法（走路、晃动、手势）
- 外部 IMU / Grove 扩展
- 设置页分标签
- 把 SOUND/FPS 搬进设置页
- 改动物理、碰撞、渲染算法

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| BMI270 初始化/寄存器不熟 | 先试 M5Unified IMU 接口；失败再用最小 I2C 实现；提供 `available()` 与失败提示 |
| QEMU 不模拟 IMU | `CRAFTCRAFT_GYRO_FAKE` 只用于仿真；真机单独验 |
| 陀螺仪漂移 | 开机静止校准；死区；必要时提供“重新校准” |
| FOV 运行期化影响性能 | 只在设置变化时更新；不逐帧重算 |
| 亮度调到很低看不清 | 最低 10%；UI 提示 |
| 库版本黑屏 | 继续 pin M5Unified 0.2.29 / M5GFX 0.2.36 |

## 8. 交付与协作

- 产物：app-only `firmware.bin`，目标槽 `mp3200` 或 `macplu`（用户选择安装，统领不安装）。
- 完成后复制到 `/home/landspace/openpod/dist/`，更新 `/home/landspace/openpod/dist/UPLOAD-REPORT.md` 的 cardcraft 行（新字节数/md5/验证证据），并发 `[UPLOAD-REPORT] DONE cardcraft-settings`。
- 仿真只用端口 **8798** + `espemu-state-cardcraft`，单实例，用完即停；不要碰 8790/8791/8795/8796/8797/8799。
- 禁止对真机执行 esptool/Launcher 安装。
