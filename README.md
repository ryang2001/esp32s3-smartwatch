# Blink — ESP32-S3 Smartwatch Firmware

基于 ESP32-S3 的智能手表固件：LVGL 9 触摸界面（Apple 风格深色主题）、双核 FreeRTOS 任务编排、A/B 双槽 OTA 升级（自检 + 崩溃回滚），并移植了接入智谱 GLM 的 AI 对话助手。

## 功能特性

- **双核任务编排** — LVGL 渲染独占 Core 1，业务任务全在 Core 0，渲染帧率不受网络/AI/OTA 抖动影响
- **高速显示** — ST7789 SPI + DMA @ 80 MHz，partial refresh 只重绘脏区域
- **电容触摸** — CST816S I2C，INT 中断唤醒 + 轮询混合模型，带幻触发抑制
- **A/B 双槽 OTA** — manifest + semver 比对 + 流式下载 + SHA-256 回读校验；15 秒开机自检（LVGL 心跳 + 堆水位双判据）与崩溃自动回滚，真机完成多版本连续升级
- **内存布局** — 大栈任务与 LVGL 绘制缓冲驻 8MB PSRAM（借 S3 EDMA 直访刷屏），稀缺内部 RAM 留给协议栈与 DMA
- **应用框架** — App 生命周期管理、页面导航栈、后台保留池、手势导航、状态栏 / 通知中心 / 快捷设置
- **AI 对话** — 移植 MimiClaw 接入 GLM-5.1，48KB PSRAM 栈任务按需创建、用完自毁
- **全量中文** — GB2312 自定义 CJK 字体（6864 字形，微软雅黑）

<!-- TODO: 在此处补充实机照片 / 演示 GIF（建议放 docs/ 目录，宽度 ~400px 两列排布）
## 演示
![表盘](docs/demo-watchface.jpg)
![AI 对话](docs/demo-chat.jpg)
-->

## 硬件规格

| 参数 | 值 |
|------|-----|
| MCU | ESP32-S3 双核 240 MHz |
| Flash | 16 MB QIO 80 MHz |
| PSRAM | 8 MB Octal 80 MHz |
| 显示屏 | 240×280 ST7789 SPI LCD, 80 MHz |
| 触摸 | CST816S I2C 电容触摸 |
| 背光 | LEDC PWM 10-bit, 5 kHz |
| LED | 4× GPIO 指示灯 (GPIO 38-41) |

### 引脚定义

| 功能 | GPIO | | 功能 | GPIO |
|------|------|---|------|------|
| LCD MOSI | 13 | | TP SDA | 11 |
| LCD CLK | 14 | | TP SCL | 12 |
| LCD DC | 21 | | TP RST | 9 |
| LCD RST | 10 | | TP INT | 3 |
| LCD BCKL | 8 | | | |

## 系统架构

### 组件分层

```
┌ 应用层 ──────────────────────────────────────────────────────────
│  main/app_main.c     开机时序编排：外设→框架→OTA→WiFi→任务创建
│  apps/               launcher · clock · stopwatch · touch_test
│                       settings(含固件升级页) · mimiclaw(AI 对话 UI)
├ 应用框架层 ──────────────────────────────────────────────────────
│  app_framework/      app_manager(生命周期) · app_registry(注册表)
│                       gesture(手势导航) · nav_stack(页面返回栈)
│                       sys_ui(状态栏/通知) · theme(深色主题)
├ 中间件（不依赖硬件与 UI，可整体复用）────────────────────────────
│  ota_update/         manifest · semver · 流式下载 · 双校验 · 自检回滚
│  mywifi/             STA 连接 · AP 扫描 · NVS 凭据
│  mimiclaw_core/      AI 对话引擎：HTTPS → GLM
├ 硬件抽象层 HAL ─────────────────────────────────────────────────
│  platform/           display.c(ST7789) touch.c(CST816S)
│                       backlight.c(LEDC) · board.h(引脚集中) · fonts/
├ 第三方组件 ─────────────────────────────────────────────────────
│  lvgl 9.5 · esp_lvgl_port · esp_lcd_touch(+cst816s) · cjson
├ ESP-IDF v6.0 ───────────────────────────────────────────────────
│  FreeRTOS(双核) · esp_lcd · spi/i2c/gpio/ledc · esp_wifi+lwIP
│  esp_https_ota + app_update + mbedtls · NVS · bootloader
└──────────────────────────────────────────────────────────────────
```

依赖方向由各组件 `CMakeLists.txt` 的 REQUIRES 固化：`mimiclaw_core` / `mywifi` / `ota_update` 三个中间件不依赖任何硬件或 UI 组件；`main` 是唯一认识全部组件的组合根。

### 运行时：双核任务视图

```
        CPU 1                              CPU 0
  ┌──────────────────┐   INT(GPIO3)  ┌─────────────────────────────
  │ taskLVGL  prio 4 │ ←───────────  │ main      LED 心跳          │
  │ 渲染 + 输入轮询   │  CST816S 抬手  │ sntp      NTP 对时          │
  └──────────────────┘  唤醒 LVGL     │ mem       内存水位监控       │
         ↑ lvgl_port_lock             │ + 按需: mimiclaw / ota /    │
         └── 所有上层访问 UI 过同一把锁  │         ota_chk / wifi_scan │
                                      └─────────────────────────────
```

常驻 4 任务 + 4 类按需任务（详见下方任务表），日常并发 4、峰值 6。

### Flash 布局

```
0x9000      0xd000        0xf000       0x10000       0x410000      0x810000
┌─────────┬─────────────┬──────────┬─────────────┬─────────────┬──────────┐
│  nvs    │   otadata   │  phy     │  ota_0 4MB  │  ota_1 4MB  │ assets   │
│ 凭据/计数│ seq大者胜+状态│ 射频校准  │   slot A    │   slot B    │  7.9MB   │
└─────────┴─────────────┴──────────┴─────────────┴─────────────┴──────────┘
```

## 目录结构

```
blink/
├── main/
│   └── app_main.c              # 入口：硬件初始化、OTA、WiFi、任务创建
├── components/
│   ├── platform/               # 硬件抽象层
│   │   ├── src/display.c       # SPI+DMA 显示驱动，ST7789，80MHz
│   │   ├── src/touch.c         # CST816S I2C 触摸，INT 唤醒 + 幻触发抑制
│   │   ├── src/backlight.c     # LEDC PWM 背光控制
│   │   ├── fonts/              # GB2312 CJK 字体 (14px, 16px)
│   │   └── include/board.h     # 引脚、分辨率、常量定义
│   ├── app_framework/          # 应用框架
│   │   ├── src/app_manager.c   # 生命周期管理，后台保留池
│   │   ├── src/app_registry.c  # 应用注册表 (最多 16 个)
│   │   ├── src/nav_stack.c     # 导航栈 (最大深度 8)
│   │   ├── src/gesture.c       # 手势识别 (边缘滑动、点击、长按)
│   │   ├── src/theme.c         # Apple 风格主题 + CJK fallback
│   │   └── src/sys_ui.c        # 状态栏、通知面板、快捷设置
│   ├── apps/                   # 内置应用 (见下方列表)
│   ├── mimiclaw_core/          # AI 聊天引擎 (GLM-5.1，纯中间件)
│   ├── mywifi/                 # WiFi STA，扫描，NVS 凭据
│   └── ota_update/             # A/B 双槽 OTA + 自检回滚 (纯中间件)
├── ota_server/                 # 本地升级服务器样例 (manifest.json)
├── tools/
│   └── boot_flow.html          # 交互式启动流程可视化 (浏览器打开)
├── partitions.csv              # 分区表：A/B 双槽 4MB×2 + assets
├── sdkconfig.defaults          # 编译默认配置
└── DESIGN.md                   # Apple 设计系统参考
```

## 内置应用

| 应用 | 说明 | 类别 |
|------|------|------|
| **Clock** | 模拟时钟，带秒针和日期显示。点击中心打开启动器 | System |
| **Launcher** | 已安装应用图标网格 | System |
| **AI Chat** (MimicLaw) | 基于 GLM-5.1 的 AI 聊天，支持中文对话 | Tool |
| **Settings** | WiFi 扫描/连接、亮度调节、固件升级、关于、性能叠加层 | Tool |
| **Stopwatch** | 秒表，开始/停止/计圈/重置 | Tool |
| **Touch Test** | 触摸坐标显示，网格背景 | Tool |

## 应用框架

### 生命周期

```
create() → show() → [RUNNING] → hide() → [PAUSED/RETAINED]
                                    ↓
                              show() (恢复)
                                    ↓
                              destroy() (释放)
```

每个应用实现以下回调：

```c
typedef struct {
    const char *name;
    void *(*create)(lv_obj_t *parent);    // 创建 UI，返回上下文
    void (*show)(void *ctx);              // 前台恢复
    void (*hide)(void *ctx);              // 后台暂停
    void (*destroy)(void *ctx);           // 销毁释放
    bool (*handle_event)(void *ctx, const app_event_t *event);
    void (*tick)(void *ctx);              // 每秒调用
} app_descriptor_t;
```

### 导航与后台

- **导航栈**：最大深度 8，支持 `launch`/`go_back`/`go_home`
- **后台保留池**：`go_back`/`go_home` 不销毁应用，移入保留池（最多 6 个）
- **重新打开**：从保留池恢复实例，保持之前的状态（聊天记录、秒表计时等）
- **溢出淘汰**：保留池满时，销毁最早进入的应用

### 手势系统

| 手势 | 触发条件 | 系统行为 |
|------|----------|----------|
| 从左边缘右滑 | start_x < 20px | 返回上一页 |
| 从顶部下滑 | start_y < 28px | 打开通知面板 |
| 从底部上滑 | start_y > 252px | 打开快捷设置 |
| 中心点击 | 其余区域 | 转发给当前应用 |
| 长按 | > 800ms | 转发给当前应用 |

## 系统界面

- **状态栏**：28px 高，显示时间（左）和 WiFi 图标（右），半透明背景
- **通知面板**：从顶部滑入，点击关闭；OTA 完成 / 升级成功通知走这里
- **快捷设置**：从底部滑入，包含亮度滑块
- **性能叠加层**：右上角显示 FPS 和 CPU%，通过 Settings → Performance 开关

## 主题

采用 Apple 风格深色主题：

| 颜色 | 用途 |
|------|------|
| `#000000` | 背景 |
| `#FFFFFF` | 前景文字 |
| `#0071E3` | 主强调色 (按钮、链接) |
| `#1D1D1F` | 表面色 |
| `#2A2A2D` | 提升表面色 |
| `#CCCCCC` | 次要文字 |
| `#7A7A7A` | 暗淡文字 |
| `#FF3B30` | 危险操作色 |

字体使用 Montserrat 14/20/28px 作为主字体，自动 fallback 到 GB2312 自定义 CJK 字体（微软雅黑，6864 字形），覆盖全部简体中文常用字。

## 性能优化

| 配置 | 值 | 说明 |
|------|-----|------|
| CPU 频率 | 240 MHz | 最高主频 |
| 绑核 | LVGL→Core1，业务→Core0 | 渲染不受业务抖动影响 |
| 绘制缓冲 | PSRAM 双缓冲 | 借 S3 EDMA 直访，省内部 RAM |
| 显示模式 | Partial refresh | 只重绘脏区域 |
| SPI DMA | 9600B transfer | 减少 DMA 中断次数 |
| 手势轮询 | 50ms | 降低空转开销 |

## FreeRTOS 任务

常驻任务：

| 任务 | 核心 | 优先级 | 栈 | 说明 |
|------|------|--------|-----|------|
| main | 0 | 1 | 3.5 KB | 初始化 + LED 心跳 |
| taskLVGL | **1** | 4 | 16 KB | 渲染、输入、动画 |
| sntp | any | 3 | 16 KB (PSRAM) | NTP 对时，之后低频保活 |
| mem | any | 1 | 12 KB (PSRAM) | 每 10s 打印堆水位 |

按需任务（用完自毁）：

| 任务 | 核心 | 优先级 | 栈 | 触发 |
|------|------|--------|-----|------|
| mimiclaw | 0 | 5 | 48 KB (PSRAM) | 每次 AI 对话 |
| ota | 0 | 4 | 12 KB (**内部 RAM**) | 每次升级（见踩坑 #3） |
| ota_chk | any | 3 | 3 KB | 升级后 15s 自检窗口 |
| wifi_scan | any | 5 | 16 KB | 每次 AP 扫描 |

> 栈深按字节标注。大栈任务迁 PSRAM 以保留内部 RAM，唯一例外是 OTA 任务——见踩坑记录。

## OTA 固件升级

固件采用 A/B 双 slot（`ota_0`/`ota_1` 各 4MB）+ 崩溃自动回滚，组件为 `components/ota_update/`，支持断电/崩溃安全的升级。

### 升级流程

1. Settings → Firmware → **Check for Updates**：拉取 manifest（Kconfig 配置 URL）并与当前版本做 semver 比较
2. **Download Update**：`esp_https_ota` 流式下载到空闲 slot（3MB 固件边下边写，无需整体缓存）；下载完成后分块读回 flash、重算 SHA-256 与 manifest 比对（防"合法但配错"的镜像）
3. **Reboot to Apply**（两击确认）：切换 boot slot 重启
4. 新镜像首次开机进入 15s 自检窗口（LVGL 心跳 tick 增量 + 内部堆水位双判据），通过后 `esp_ota_mark_app_valid_cancel_rollback()` 转正；未确认即崩溃 → bootloader 下次启动将其标记 ABORTED 并自动回退旧槽；应用层另有 NVS 启动计数（≥3 次立即回滚）作防御纵深
5. WiFi 未连上属软判据：只告警不回滚——回滚会让旧版本同样无法联网，反而锁死升级通道

### 更新服务器契约

manifest JSON（部署时设置 `Cache-Control: no-cache`）：

```json
{
  "version": "0.9.1",
  "url": "https://ota.example.com/watch/bin/blink-0.9.1.bin",
  "sha256": "<64 hex，对 .bin 本身，可选但强烈建议>",
  "size": 3237472,
  "notes": "更新说明（支持中文）"
}
```

### 发版步骤

```bash
# 1. 升版本号（根 CMakeLists.txt → PROJECT_VER，写入 esp_app_desc_t）
# 2. 编译
idf.py build
# 3. 计算 sha256
python -c "import hashlib;print(hashlib.sha256(open('build/blink.bin','rb').read()).hexdigest())"
# 4. 上传 blink.bin（版本化 URL，不可变）并更新 manifest.json 的 version/url/sha256/size/notes
```

### LAN 调试

```bash
# PC 上起本地服务器，放好 manifest.json 与 blink.bin
cd ota_server && python -m http.server 8000
# menuconfig: OTA Update → OTA manifest URL = http://<PC-IP>:8000/manifest.json
# 并临时开启 CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y（仅调试，生产必须 HTTPS）
```

### 注意

- 从旧单 factory 分区表迁移需 `idf.py erase-flash` 全量重刷一次（NVS 中的 WiFi 凭据会丢失）
- 量产前建议开启 Secure Boot V2 + Flash 加密 + 防回滚（烧 efuse 不可逆，留到最后）

## 编译与烧录

### 环境要求

- ESP-IDF v6.0
- Python 3.10+

### 配置密钥（clone 后必做）

仓库**不含任何密钥**。两个本地头文件被 git 忽略，源码通过 `__has_include` 自动加载，缺失时回退占位符：

```c
// components/mywifi/wifi_secrets.h            （WiFi 兜底凭据）
#pragma once
#define EXAMPLE_ESP_WIFI_SSID      "你的WiFi名"
#define EXAMPLE_ESP_WIFI_PASS      "你的WiFi密码"

// components/mimiclaw_core/include/mimiclaw_secrets.h   （智谱 API Key）
#pragma once
#define MIMICLAW_SECRET_API_KEY    "你的智谱APIKey"
```

> WiFi 更推荐直接在手表 **Settings → WiFi** 里连接（存 NVS，优先级高于编译期兜底）；
> API Key 同样支持运行时经 `mimiclaw_set_api_key()` 写入 NVS。

### 编译与烧录

```bash
idf.py build
idf.py -p COM端口 flash monitor
```

### 全量烧录命令

```bash
python -m esptool --chip esp32s3 -b 460800 \
  write-flash \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0xd000  build/ota_data_initial.bin \
  0x10000 build/blink.bin
```

## 踩坑记录

调试中踩过的硬件坑，记录在此供同类项目参考：

1. **ST7789 不亮 / 花屏**：这块屏的 SPI 拓扑必须用 **SPI mode 3 + sio_mode**（见 `display.c`），大多数例程默认 mode 0，直接照抄会黑屏。
2. **CST816S 幻触发**：抬手后每隔约 10 秒在上次坐标处冒出一次"幽灵触摸"——芯片空闲周期会把未被主机消费的旧报告重新断言。修复三件套：读坐标前**先读手势寄存器 0x01**（消费掉挂起报告）、写 `0xFA=0xFF` 关闭自动休眠、主机侧**两帧确认**才算按下（见 `touch.c`）。
3. **OTA 任务不能用 PSRAM 栈**：flash 写操作会暂停 cache，而 PSRAM 访问依赖 cache——自己把自己锁死。OTA worker 必须用内部 RAM 栈（`ota_config.h` 有注释）。
4. **LVGL 双缓冲放 PSRAM**：ESP32-S3 的 LCD 外设可经 EDMA 直接访问 PSRAM，绘制缓冲不必挤占内部 RAM（`lvgl_port_display_cfg_t.flags.buff_spiram`）。

## 启动流程可视化

[tools/boot_flow.html](tools/boot_flow.html) — 纯前端交互式动画，逐级演示从上电到应用启动的全流程：ROM 加载 bootloader → otadata 双槽选择 → 段表映射（IRAM/DRAM/XIP）→ 外设初始化 → 自检窗口，支持步进单步执行。浏览器直接打开即可。

## 添加新应用

1. 在 `components/apps/` 下创建目录，实现 app descriptor：

```c
#include "app_base.h"
#include "app_registry.h"

static void *my_app_create(lv_obj_t *parent) {
    // 创建 UI
    return ctx;
}

const app_descriptor_t app_descriptor_my_app = {
    .name         = "my_app",
    .display_name = "My App",
    .category     = APP_CAT_TOOL,
    .flags        = 0,
    .create       = my_app_create,
    .show         = my_app_show,
    .hide         = my_app_hide,
    .destroy      = my_app_destroy,
    .handle_event = my_app_handle_event,
    .tick         = NULL,
};
```

2. 在 `components/apps/apps_init.c` 中注册：

```c
app_registry_register(&app_descriptor_my_app);
```

3. 在 `components/apps/CMakeLists.txt` 中添加源文件。

## 重新生成 CJK 字体

项目包含 GB2312 全量汉字字体（6864 字形），如需修改：

```bash
# 安装字体转换工具
npm install -g lv_font_conv

# 生成字符列表
python -c "
chars = set()
for hi in range(0xB0, 0xF8):
    for lo in range(0xA1, 0xFF):
        try: chars.add(bytes([hi, lo]).decode('gb2312'))
        except: pass
with open('tools/gb2312.txt', 'w') as f: f.write(''.join(sorted(chars)))
"

# 生成 14px 字体
npx lv_font_conv --bpp 4 --size 14 --font msyh.ttf \
  -r 0x20-0x7F --symbols "$(cat tools/gb2312.txt)" \
  --format lvgl -o components/platform/fonts/font_cjk_14.c

# 生成 16px 字体 (同理，改 --size 16 和输出路径)
```

生成后需将文件中的 `#include "lvgl/lvgl.h"` 改为 `#include "lvgl.h"`。

## 依赖

| 组件 | 版本 | 来源 |
|------|------|------|
| ESP-IDF | 6.0.0 | 框架 |
| LVGL | 9.5.0 | managed_component |
| esp_lvgl_port | 2.7.2 | managed_component |
| esp_lcd_touch_cst816s | 1.1.1 | managed_component |
| cJSON | 1.7.19 | managed_component |

## 许可证

本项目仅供学习和研究用途。
