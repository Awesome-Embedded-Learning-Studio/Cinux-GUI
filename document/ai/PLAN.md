# Cinux-GUI PLAN — 当前焦点批级进度

> 批级（最易变）。**里程碑全树见 [ROADMAP.md](ROADMAP.md)。**
> **P0 Probe-0 完成 ✅**：核心渲染管线 standalone 跑通——pump→core.staging→swraster→glyph_blit→flush，事件回放驱动拖拽，脏区纪律断言（idle 0 / 首帧全屏 / 后续局部）。3 个 ctest（smoke + offscreen + replay）全绿 + ASAN 干净。
> **P1 Probe-1 完成 ✅**：fbdev+evdev 真 host（`host/linux_fbdev_main.cpp`）+ evdev accumulator 单测（`test/test_evdev.cpp`，4 ctest 绿 + ASAN）+ **真 QEMU 冒烟 PASS**（自构建 x86_64 kernel + busybox initramfs，`fb0:bochs-drmdrmfb` + usb-tablet + fbdev-host 跑满 40s 不崩，像素采样验场景画对）。core 一行没动，只换 host 表——解耦缝证毕。详见 `document/notes/2026-06-30-p1-probe1-fbdev-host.md`。
> **P2 渲染收敛核心 ✅ 完成**：host-neutral **Scene（数据）+ Compositor（渲染器）** 收敛三 host 重复画法 + frame-to-frame dirty diff（只 composite 变化区，省 CPU 不止省 flush）。三 host 已切 Compositor，fbdev QEMU 冒烟 PASS（像素眼检无误）。**Host ABI 零改动**（CinuxOS 不 bump pin）。4 批 a/b/c/d 详见批表 + 笔记 `notes/2026-07-01-p2-*.md`。
> **P3 控件工具箱 🔄 启动中**：Widget 树 + 布局 + 事件路由 + **Material 风格**（纯整数 flat 子集：配色 + 圆角 + 色差层次，**无**波纹/动效）。**控件树 PaintList 取代 P2 Scene** 作场景源。Label/Button/Slider/Container + HBox/VBox。CinuxOS 零改动。批表见下。

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
- ~~**swraster 没接进 pump**~~ → **P0-a 已接进渲染路径**：core 拥有 staging Surface（新增 `GuiCore`），pump 预填 staging → host 经 `render_frame` 画进去 + 报脏区 → region 收脏 → flush。但 **host 还没画真场景**（窗口/光标/文字 = P0-b 的 offscreen host），目前 fake_host 只验证所有权链路。

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
| **P0-a** | staging Surface 契约 + swraster 接进 pump（`render_frame` 改画 staging + region 算脏） | ✅ | `9e71e3f` | ctest 绿 + ASAN 干净 |
| **P0-b1** | 字体基建：PSF2 字体搬进本仓（constexpr C 数组）+ 解析器 + glyph_blit 渲字单测 | 🔜 NEXT | — | ctest 绿（parse 8×16/256 + 'A' 非空） |
| **P0-b2** | offscreen host + PPM dump + 静态场景（窗口 + 标题栏 + 多行文字 + 光标）一帧 | ✅ | `f08abbf` | ctest 绿 + ASAN + 视觉确认 |
| **P0-b3** | 事件回放（文本→Event）+ 拖拽交互 + 多帧 dump + 金帧/脏区断言 | ✅ | `f4c95ca` | ctest 绿 + ASAN + 几何/脏区断言 |
| **P0-c** | 金帧 / 脏区断言进 ctest + host 单测 ASAN 自验 | ✅ 被吸收 | — | 脏区断言进 ctest（b3）+ 金帧用结构断言代替二进制 fixture（更鲁棒）+ 每批 ASAN |

## 批表（P2 渲染收敛核心）

> 决策点已定（用户拍板）：① Scene/Compositor 放 `core/`（host-neutral 库本位；GuiCore「对场景无知」指 pump 驱动器，非禁 core/ 有 Scene 类型）② `draw_text`/`draw_rect_outline` 留 Compositor 内部（保 swraster 纯原语层，不反向依赖 font）③ 帧间 dirty 由 Compositor 持 prev Scene 处理（窗口移动露背景 = old_rect∪new_rect 进 Region）。**Host ABI 零改动，CinuxOS 不 bump pin**。

| 批 | 范围 | 状态 | commit | 测试 |
|---|---|---|---|---|
| **P2-a** | Scene 数据模型（`core/scene.*`：Window 栈 + Cursor + 文本 run，纯 POD，零 swraster/host include） | ✅ | `2fe37af` | 新 `test_scene.cpp`：构造/几何/截断/容量断言绿 + ASAN |
| **P2-b** | Compositor 接管绘制（`core/compositor.*`：`compose(staging, scene)`，收敛三 host 重复 paint） | ✅ | `f18414e` | 新 `test_compositor.cpp`：像素结构 == 旧 offscreen（零回归） |
| **P2-c** | 帧间 dirty diff（Compositor 持 prev Scene，只 composite 变化区，返回 dirty Region） | ✅ | `4310e64` | compositor-test dirty 段：首帧全屏 / idle / cursor 移 / window 移（+露 bg）/ bg 变 |
| **P2-d** | 三 host 切 Compositor + fbdev QEMU 冒烟验无回归 | ✅ | `59aa964` | ctest 全绿 + ASAN + QEMU 冒烟 PASS（像素眼检无误） |

## 批表（P3 控件工具箱 · Material 风格）

> 决策点已定（用户拍板）：① **控件树 PaintList 取代 P2 Scene** 作场景源（干净但要迁移三 host，长痛不如短痛）② **flat Material 首批**：配色 + 圆角 + 色差层次（不用 blur 阴影）；**不做**波纹/动画/过渡（纯整数算不起，留 P5+ 动画系统）③ 布局 HBox/VBox 线性首批（padding/margin/align，不做 flex）；事件 hit-test **点谁谁处理**（不冒泡）。**Host ABI 零改动，CinuxOS 不 bump pin**。

| 批 | 范围 | 状态 | commit | 测试 |
|---|---|---|---|---|
| **P3-a** | 控件模型 + PaintList + 事件路由：Widget 基类（rect/children/hit_test/on_pointer/paint_to_list）+ PaintList（原语序列 fill_rect/text/clip）+ Desktop（dispatch + render）+ Compositor::execute(PaintList) | ✅ | — | 新 test_widget：hit-test 命中 + dispatch + PaintList→像素 |
| **P3-b** | swraster 圆角 + Material Theme：`fill_rounded_rect`（整数角 mask 预计算）+ `core/theme.*`（Material 配色 primary/surface/on-surface + 圆角半径 + 8dp 网格） | 🔜 NEXT | — | 圆角像素（角不溢出）+ theme 配色 |
| **P3-c** | 基础控件 + 布局：Label / Button（down/hover/press 状态）/ Container + HBox/VBox | ⏳ | — | 控件 paint + 布局几何 + Button press |
| **P3-d** | Slider + host demo + P2 Scene 退役：Slider（拖动取值）+ replay/fbdev 控件 demo（Material 外观）+ 三 host 切控件树、Scene 退役 | ⏳ | — | ctest + ASAN + QEMU 冒烟（像素眼检 Material） |

## 验证哲学

- **能确定的，全部自动化进本仓 ctest**：渲染对不对 = 金帧对比；脏区对不对 = 断言 `flush` 了哪些 rect；崩不崩 / ASAN / UBSAN = 自动。
- **不能确定的，别假装能自动化**：「好看 / 跟手」老实人工眼检。金帧 + 结构断言 + 人工看，这个组合。
- **不占 CinuxOS CI**：本仓 standalone ctest。bump pin 回 Cinux 时，再用 `make run` GUI 冒烟 + `run-kernel-test-all` 防回归。
