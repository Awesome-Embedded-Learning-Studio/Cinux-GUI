# Cinux-GUI PLAN — 当前焦点批级进度

> 批级（最易变）。**里程碑全树见 [ROADMAP.md](ROADMAP.md)。** 当前焦点：**P0 = Probe-0**（🔜）。

## 现状速览（我们站在哪）

### 已有的（本仓 `core/`）

- **Host ABI 表**（`core/host.hpp`）——唯一硬边界：`HostCore`（flush / flush_complete / poll_event / dispatch_event / render_frame / now_ms / alloc / free / log）+ `HostDesktop`（spawn，可选）+ `Frame` 脏帧契约 + `PixelFormat{kXrgb8888, kArgb8888}`。回调全 NULL-check，部分填表也安全。
- **pump**（`core/pump.*`，~75 行）——排空 `poll_event` → `dispatch_event` → `render_frame` → 按脏 rect 逐个 `flush`；idle 帧 0 写屏。`kMaxDirtyRects=64`。
- **region**（`core/region.*`）——Rect 代数 intersect / union / subtract（4-strip）/ translate，`kMaxRects=32`，**容量溢出坍缩包围盒，永不欠覆盖**。
- **swraster**（`core/swraster.*`）——5 个纯 CPU 整数原语（fill_rect / blit / blit_blend Q8.8 定点 / glyph_blit 1-bpp mask / draw_line Bresenham），仅 XRGB8888。
- **event ABI**（`core/event*.hpp`）——定长、版本化：`EventHeader` 8B + `PointerPayload`(18B) / `KeycodePayload`(3B)。`kEncoder`/`kTouch` 留位未定义。
- **abi_check + fake_host_main**——编译期布局自检 + host 中立性证明（standalone ctest 绿）。

### 关键的「没有」（本仓）

- 没有控件 / WM / 合成器 / SDL/X11/Wayland 真 host（README 全 TODO）。
- **swraster 没接进 pump**：pump 把渲染甩给 host 的 `render_frame` → 在 CinuxOS 里回到 `wm.composite()` → legacy `Canvas::draw_*`。**绘制能力悬着没上线**（`swraster.hpp` 自承 "NOT wired into pump yet"）。

### CinuxOS 侧（不在本仓，但要知道）

真正在跑的「桌面」住内核 `kernel/gui/`（12 TU：WM / terminal / desktop / host_cinux 适配器 / SPSC 事件队列），run-kernel-test **928/0** + GUI 冒烟零 panic，能拖拽 / Z 序 / 多终端跑真 musl shell / 脏区 flush / usb-tablet 绝对鼠标。**好使，但跟内核 API（Canvas / Pipes / TaskBuilder / launch_user_program）缠绕。**

## 当前焦点：P0 探针 Probe-0

**目标**：证明核心（pump + swraster + region）能画真像素、被输入驱动、standalone 跑（零 CinuxOS 依赖）。最小但端到端。

### 前置必做（绕不开）：把 swraster 接进 pump（visor-02 §4c）

改 pump 渲染路径：核心拥有一块 staging `Surface` → swraster 把场景画进去 → region 算脏区 → 脏 rect 经 `flush` 推 host。第一步先标准化「staging Surface + 脏区」契约——`render_frame` 给 host 一块 `Surface` + 当前脏区，host 把场景画进这块 Surface，pump 算脏 + flush。

### 两级探针（先 0 后 1）

| 级 | 形态 | 输入 | 显示 | 跑哪 |
|---|---|---|---|---|
| **Probe-0** | 无头 / 离屏，**终极可调试** | 极简文本回放（`+100 move 100 200` / `+200 down`） | malloc 影子缓冲 → dump PNG | WSL2 / 任何 Linux，零硬件 |
| **Probe-1** | 真 fbdev + evdev | `/dev/input/event*` | `/dev/fb0` mmap | QEMU（现成 Alpine 镜像 + VNC，**不编译 Linux**）/ 带显示真机 |

- **Probe-0 是验证基建本身**：确定性回放 + dump 帧 = 金帧对比 + 脏区断言。WSL2 直接跑，出 bug 最易定位。
- **Probe-1 证明第二个真 host**：跟 Cinux 同海拔，证明解耦缝干净。现成镜像，绝不 buildroot。
- **边际**：Cinux 自己就是「真 fbdev 级」host。Probe-0 验稳后，Probe-1 独有价值仅是「第二个真 host 证缝」，可择机。

### 最小场景

带边框的窗口 + 可用鼠标/回放事件拖动的光标 + 文字（内核 PSF 字体搬进本仓，`glyph_blit` 渲个 "Hello"，管线通没通一眼明白）。

### 验收

- Probe-0：回放事件，dump 的帧序列光标/窗口动、脏区只刷该刷的 rect（非全屏）；金帧对比过。
- Probe-1：QEMU 真鼠标动，屏幕光标跟动；不崩、fbdev 打开、事件读到。
- **全程零 CinuxOS 依赖**——本仓 standalone 构建 + 跑。

## 批表（P0）

| 批 | 范围 | 状态 | commit | 测试 |
|---|---|---|---|---|
| **P0-a** | staging Surface 契约 + swraster 接进 pump（`render_frame` 改画 staging + region 算脏） | 🔜 NEXT | — | standalone ctest |
| **P0-b** | offscreen host（影子缓冲 + PNG dump + 事件回放）+ 极简场景（窗口 + 光标 + 文字） | 待启动 | — | 金帧对比 + 脏区断言 |
| **P0-c** | 金帧 / 脏区断言进 ctest + host 单测 ASAN 自验 | 待启动 | — | ctest 绿 + ASAN 干净 |

## 验证哲学

- **能确定的，全部自动化进本仓 ctest**：渲染对不对 = 金帧对比；脏区对不对 = 断言 `flush` 了哪些 rect；崩不崩 / ASAN / UBSAN = 自动。
- **不能确定的，别假装能自动化**：「好看 / 跟手」老实人工眼检。金帧 + 结构断言 + 人工看，这个组合。
- **不占 CinuxOS CI**：本仓 standalone ctest。bump pin 回 Cinux 时，再用 `make run` GUI 冒烟 + `run-kernel-test-all` 防回归。
