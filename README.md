# Blink — ESP32-S3 Smartwatch Firmware

基于 ESP32-S3 的智能手表固件，使用 LVGL 9 构建触摸交互界面，采用 Apple 风格深色主题设计。

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

| 功能 | GPIO |
|------|------|
| LCD MOSI | 13 |
| LCD CLK | 14 |
| LCD DC | 21 |
| LCD RST | 10 |
| LCD BCKL | 8 |
| TP SDA | 11 |
| TP SCL | 12 |
| TP RST | 9 |
| TP INT | 3 |

## 软件架构

```
┌─────────────────────────────────────────────────┐
│                   Apps Layer                     │
│  Clock | Launcher | Settings | MimicLaw | ...   │
├─────────────────────────────────────────────────┤
│               App Framework                      │
│  Manager | Registry | Nav Stack | Gesture | UI  │
├─────────────────────────────────────────────────┤
│                Platform HAL                      │
│  Display (SPI+DMA) | Touch (I2C) | Backlight    │
├─────────────────────────────────────────────────┤
│  LVGL 9.5 | ESP-IDF v6.0 | FreeRTOS | WiFi      │
└─────────────────────────────────────────────────┘
```

### 目录结构

```
blink/
├── main/
│   └── app_main.c              # 入口：硬件初始化、WiFi、SNTP、LED 闪烁
├── components/
│   ├── platform/               # 硬件抽象层
│   │   ├── src/display.c       # SPI+DMA 显示驱动，ST7789
│   │   ├── src/touch.c         # CST816S I2C 触摸，中断唤醒
│   │   ├── src/backlight.c     # LEDC PWM 背光控制
│   │   ├── fonts/              # GB2312 CJK 字体 (14px, 16px)
│   │   └── include/board.h     # 引脚、分辨率、常量定义
│   ├── app_framework/          # 应用框架
│   │   ├── src/app_manager.c   # 生命周期管理，导航栈，后台保留池
│   │   ├── src/app_registry.c  # 应用注册表 (最多 16 个)
│   │   ├── src/nav_stack.c     # 导航栈 (最大深度 8)
│   │   ├── src/gesture.c       # 手势识别 (点击、滑动、长按)
│   │   ├── src/theme.c         # Apple 风格主题 + CJK fallback
│   │   └── src/sys_ui.c        # 状态栏、通知面板、快捷设置
│   ├── apps/                   # 内置应用
│   │   ├── clock/              # 模拟时钟 (秒针/分针/时针)
│   │   ├── launcher/           # 应用启动器网格
│   │   ├── settings/           # 设置 (WiFi/显示/关于/性能叠加层)
│   │   ├── stopwatch/          # 秒表 (30ms 精度)
│   │   ├── touch_test/         # 触摸调试工具
│   │   └── mimiclaw/           # AI 聊天界面
│   ├── mimiclaw_core/          # AI 聊天客户端 (GLM-5.1)
│   ├── mywifi/                 # WiFi STA，扫描，NVS 凭据存储
│   └── myui/                   # GUI Guider 旧版资源 (LVGL 8.x)
├── partitions.csv              # 分区表：factory 8MB
├── sdkconfig.defaults          # 编译默认配置
└── DESIGN.md                   # Apple 设计系统参考
```

## 内置应用

| 应用 | 说明 | 类别 |
|------|------|------|
| **Clock** | 模拟时钟，带秒针和日期显示。点击中心打开启动器 | System |
| **Launcher** | 已安装应用图标网格 | System |
| **AI Chat** (MimicLaw) | 基于 GLM-5.1 的 AI 聊天，支持中文对话 | Tool |
| **Settings** | WiFi 扫描/连接、亮度调节、关于、性能叠加层 | Tool |
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
- **通知面板**：从顶部滑入，点击关闭
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
| LVGL 渲染 | 2 draw units + FreeRTOS OS | 并行渲染 |
| I-Cache | 32 KB | 减少 Flash 缓存未命中 |
| D-Cache line | 64B | 提升 PSRAM DMA 带宽 |
| 显示模式 | Partial refresh | 只重绘脏区域 |
| SPI DMA | 9600B transfer | 减少 DMA 中断次数 |
| 手势轮询 | 50ms | 降低空转开销 |

## FreeRTOS 任务

| 任务 | 优先级 | 核心 | 栈大小 | 说明 |
|------|--------|------|--------|------|
| LVGL | 4 | Core 1 | 16 KB | UI 渲染主循环 |
| MimicLaw Chat | 5 | Core 0 | 12 KB (PSRAM) | AI HTTP 请求 |
| WiFi Scan | 5 | Any | 4 KB | 扫描任务 |
| SNTP | 3 | Any | 4 KB (PSRAM) | 时间同步 |
| Memory Monitor | 1 | Any | 3 KB (PSRAM) | 堆内存日志 |
| Main (LED) | 1 | Core 0 | 3.5 KB | LED 循环闪烁 |

## 编译与烧录

### 环境要求

- ESP-IDF v6.0
- Python 3.10+
- Node.js (用于 CJK 字体生成)

### 编译

```bash
# 在 ESP-IDF 终端中
idf.py build
```

### 烧录

```bash
idf.py -p COM端口 flash
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

## OTA 固件升级

固件采用 A/B 双 slot（`ota_0`/`ota_1` 各 4MB）+ 崩溃自动回滚，组件为 `components/ota_update/`。

### 升级流程

1. Settings → Firmware → **Check for Updates**：拉取 `OTA_MANIFEST_URL`（Kconfig 配置）并与当前版本比较
2. **Download Update**：`esp_https_ota` 流式下载到空闲 slot，可选回读 sha256 复核
3. **Reboot to Apply**（两击确认）：切换 boot slot 重启
4. 新镜像首次开机进入 15s 自检窗口（LVGL 心跳 + 内部堆余量），通过后 `esp_ota_mark_app_valid_cancel_rollback()`；窗口内崩溃累计 3 次自动回滚旧版本

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
python -m esptool --chip esp32s3 read-flash ...   # 或直接使用 build/blink.bin
# 3. 计算 sha256
python -c "import hashlib;print(hashlib.sha256(open('build/blink.bin','rb').read()).hexdigest())"
# 4. 上传 blink.bin（版本化 URL，不可变）并更新 manifest.json 的 version/url/sha256/size/notes
```

### LAN 调试

```bash
# PC 上起本地服务器，放好 manifest.json 与 blink.bin
python -m http.server 8000
# menuconfig: OTA Update → OTA manifest URL = http://<PC-IP>:8000/manifest.json
# 并临时开启 CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y（仅调试，生产必须 HTTPS）
```

### 注意

- 从旧单 factory 分区表迁移需 `idf.py erase-flash` 全量重刷一次（NVS 中的 WiFi 凭据会丢失）
- 量产前建议开启 Secure Boot V2 + Flash 加密 + 防回滚（烧 efuse 不可逆，留到最后）

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
