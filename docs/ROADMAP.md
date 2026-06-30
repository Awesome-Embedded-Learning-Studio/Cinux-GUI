# Cinux-GUI 路线图(2026-06-30 重启)

> 本仓 = CinuxOS 的 GUI 系统,独立开发。**本文是 2026-06-30 重启的共识与路线图,单一事实源。**
> 架构背景(七层架构 + Host ABI 设计)详见 CinuxOS `document/notes/2026-06-21-f13-visor-*.md`,以后择机迁入本仓。

## 0. 一句话定位

**Cinux-GUI 是一个完整、独立、跨平台的 GUI 系统**:核心(pump / swraster / region / event)+ 桌面层(窗口管理 / 终端 / 控件)+ 多个 host(Linux 先,Cinux 内核适配器后)。

**Host ABI 那张函数指针表(`core/host.hpp`)是唯一的缝**——桌面层住表**上面**,对 host 一无所知,只调表里的 `flush` / `poll_event` / `now_ms` / `spawn`;换 host = 换表填充,核心不变。CinuxOS 只剩一个薄薄的 host 适配器。

## 1. 现状速览(我们站在哪)

### 已经有的(本仓 `core/`)

- **Host ABI 表**(`core/host.hpp`)——唯一硬边界:`HostCore`(flush / flush_complete / poll_event / dispatch_event / render_frame / now_ms / alloc / free / log)+ `HostDesktop`(spawn,可选)+ `Frame` 脏帧契约 + `PixelFormat{kXrgb8888, kArgb8888}`。每个解引用的回调都 NULL-check,部分填表也安全。
- **pump**(`core/pump.*`,~75 行)——排空 `poll_event` → `dispatch_event` → `render_frame` → 按脏 rect 逐个 `flush`;idle 帧 0 写屏。`kMaxDirtyRects=64`。
- **region**(`core/region.*`)——一等 Rect 代数:intersect / union / subtract(4-strip)/ translate,`kMaxRects=32`,**容量溢出坍缩为包围盒——永不欠覆盖(欠覆盖 = stale-pixel bug)**。
- **swraster**(`core/swraster.*`)——5 个纯 CPU 整数原语:fill_rect / blit / blit_blend(Q8.8 定点 alpha)/ glyph_blit(1-bpp mask → 实色)/ draw_line(Bresenham 全象限)。仅 XRGB8888。
- **event ABI**(`core/event*.hpp`)——定长、版本化、跨特权稳定:`EventHeader` 8B + `PointerPayload`(18B)/ `KeycodePayload`(3B)。`kEncoder`/`kTouch` 事件码已留位,payload 未定义。
- **abi_check + fake_host_main**——编译期布局自检 + host 中立性证明(standalone ctest 绿)。

### 关键的「没有」(本仓)

- **没有控件、没有窗口管理器、没有合成器、没有 SDL/X11/Wayland 真 host。** README 里全是 TODO。
- **更要命:swraster 根本没接进 pump。** pump 把整个渲染甩给 host 的 `render_frame` → 在 CinuxOS 里回到 `wm.composite()` → 用 legacy `Canvas::draw_*`。**本仓的绘制能力是悬着的、没上线。** 这是重启第一刀要解决的(`swraster.hpp` 注释自承 "NOT wired into pump yet")。

### CinuxOS 侧(不在本仓,但要知道)

真正在跑的「桌面」住内核里(`kernel/gui/`,12 个 TU:window_manager / terminal / desktop / host_cinux 适配器 / SPSC 事件队列),run-kernel-test **928/0** + GUI 冒烟零 panic,能拖拽 / Z 序 / 多终端跑真 musl shell / 脏区 flush / usb-tablet 绝对鼠标。**这套好使,但跟内核 API(Canvas / Pipes / TaskBuilder / launch_user_program)缠绕。**

## 2. 战略方向(2026-06-30 用户拍板)

1. **极致独立**:GUI 所有细节尽可能进本仓,最大化解耦。本仓 = 完整 GUI 系统。
2. **Host ABI 唯一缝**:桌面层住表上面、对 host 无知,只调表里的 `flush` / `poll_event` / `now_ms` / `spawn`。
3. **CinuxOS 只剩薄 host 适配器**:CinuxOS `kernel/gui/` 的 WM/terminal/desktop 最终搬上表、住进本仓(「搬上去」是主要工作量)。**长期看 CinuxOS 是减代码,不是加负担。**
4. **Linux 先跑,依赖越底层越好**:选 fbdev(`/dev/fb0` mmap)+ evdev(`/dev/input/event*`),零外部依赖,跟 Cinux VBE+flush 同海拔 1:1 对照,QEMU 可跑,好调试。SDL/X11/Wayland 是高层 host,以后再加,**不用于调试核心**。

## 3. 第一刀:探针(Probe)

**目标**:证明核心(pump + swraster + region)能画真像素、被真输入驱动、standalone 跑(零 CinuxOS 依赖)。最小但端到端。

### 前置必做(绕不开):把 swraster 接进 pump(visor-02 §4c)

改 pump 渲染路径:核心拥有一块 staging `Surface` → swraster 把场景画进去 → region 算脏区 → 脏 rect 经 `flush` 推 host。
第一步先标准化「staging Surface + 脏区」契约——`render_frame` 给 host 一块 `Surface` + 当前脏区,host 把场景画进这块 Surface,pump 算脏 + flush。这样 swraster + region 真正上线;CinuxOS 以后把 `wm.composite` 改成画进这块 staging 即可(搬迁里程碑,不在探针)。

### 两级探针(先 0 后 1)

| 级 | 形态 | 输入 | 显示 | 跑哪 |
|---|---|---|---|---|
| **Probe-0** | 无头/离屏,**终极可调试** | 极简文本回放(`+100 move 100 200` / `+200 down`) | malloc 影子缓冲 → dump PNG | WSL2 / 任何 Linux,零硬件 |
| **Probe-1** | 真 fbdev + evdev | `/dev/input/event*` | `/dev/fb0` mmap | QEMU(现成 Alpine 镜像 + VNC,**不编译 Linux**)/ 带显示真机 |

- **Probe-0 是验证基建本身**:确定性回放 + dump 帧 = 金帧对比 + 脏区断言。WSL2 直接跑,出 bug 最易定位(纯软件、可 diff)。
- **Probe-1 证明第二个真 host**:跟 Cinux 同海拔,证明解耦缝是干净的。用现成镜像,绝不 buildroot。
- **边际说明**:Cinux 自己就是「真 fbdev 级」host(VBE+flush 同海拔)。Probe-0 把核心验稳后,Probe-1 的独有价值仅是「第二个真 host 证清缝」,可择机;甚至可跳过直接接 Cinux。

### 最小场景

一个带边框的窗口 + 可用鼠标/回放事件拖动的光标 + 文字(内核 PSF 字体搬进本仓,用 `glyph_blit` 渲个 "Hello",让管线通没通一眼明白)。

### 验收

- Probe-0:回放事件,dump 的帧序列光标/窗口动、脏区只刷该刷的 rect(非全屏);金帧对比过。
- Probe-1:QEMU 真鼠标动,屏幕光标跟动;不崩、fbdev 打开、事件读到。
- **全程零 CinuxOS 依赖**——本仓 standalone 构建 + 跑。

## 4. 验证哲学

- **能确定的,全部自动化进本仓 ctest**:渲染对不对 = 金帧对比;脏区对不对 = 断言 `flush` 了哪些 rect;崩不崩 / ASAN / UBSAN 干不干净 = 自动。
- **不能确定的,别假装能自动化**:「好看 / 跟手」老实人工眼检。金帧 + 结构断言 + 人工看,这个组合。
- **不占 CinuxOS CI**:本仓有自己的 standalone 构建 + ctest。等 bump pin 回 Cinux,再用 `make run` GUI 冒烟验真路径 + `run-kernel-test-all` 防回归。

## 5. 里程碑树(本仓自有编号,重启)

| 阶段 | 内容 | 状态 |
|---|---|---|
| **P0** | 探针 Probe-0:无头跑通核心渲染管线(swraster 接 pump + offscreen host + 事件回放 + 极简场景 + 金帧验证) | 🔜 下一刀 |
| **P1** | 探针 Probe-1:真 fbdev + evdev(第二个真 host,证明解耦缝) | 待启动 |
| **P2** | 渲染收敛核心:swraster 正式接管,compositor dirty-region(只重画脏区,省 composite 本身) | 待启动 |
| **P3** | 控件工具箱:Button / Label / Slider / Container + 布局 + 事件路由 | 待启动 |
| **P4** | 桌面迁入仓库:把 CinuxOS `kernel/gui/` 的 WM/terminal/desktop 搬上表、host-neutral;CinuxOS 只剩 `host_cinux` | 待启动 |
| **P5** | 字体/文本(PSF → 更全)、主题 | 待启动 |
| **P6** | GPU texture compositor(有真 GPU 目标后;非 primitive draw-list) | 远期 |
| **P7** | 多进程 Surface 协议(attach/damage/commit/release;ring-3 桌面 server) | 远期 |
| **MCU 线** | visor 嵌入式(STM32F1 等):独立 micro renderer,推迟到真板 RAM<20KB 实测后 | 长弧推迟 |

> **CinuxOS 侧 F13 文档待同步**(CinuxOS 在疯狂开发,不急,择机):把 ROADMAP F13 行改成指向本仓;清理 `document/todo/README.md` 里 stale 的 3-milestone 模型;visor-02 §9 risk 表已过时(PIT 反转等项已落地)需对齐 git 现状。

## 6. CinuxOS 侧契约(两仓关系)

- **探针阶段(P0/P1):CinuxOS 内核零改动。** 活儿全在本仓。CinuxOS 仍指向旧 pin(`4c84eb2`),照常编 / 跑 / 过 `run-kernel-test`。
- **P2 起,核心 Host ABI 若变**:CinuxOS bump pin + 改一处 `kernel/gui/host_cinux.cpp`(`render_frame` 改画 staging Surface)。**这是 CinuxOS 侧唯一的对接点。**
- **P4 后**:CinuxOS `kernel/gui/` 的 WM/terminal/desktop 搬走,**CinuxOS GUI 代码净减少**,只剩 `host_cinux` + framebuffer handoff。
- **push / PR**:Claude 在 feature 分支 commit(绿才提交);**push 与 PR 由用户控制**。

## 7. 开发约定

- C++17,**no exception / no RTTI**;核心 host-neutral(stdint/stddef only,ZERO host includes)。
- **standalone 双构建**:同一份 `CMakeLists.txt` 既是本仓独立项目(含 harness + ctest)也是 `add_subdirectory` 子目录(只出 `cinux-gui` STATIC lib)。
- 文件 500 行硬限;标识符 + 注释英文,私有成员 `_`,常量/枚举值 `k` 前缀;提交前 clang-format。
- **每批一 commit 一验证**(standalone ctest 绿才提交),写笔记到 `docs/notes/`。
- 提交信息 `<type>(<scope>): <中文简述>`,**不带 Co-Authored-By / AI 署名**。

---

*本文为 2026-06-30 重启共识。后续修订直接改本文;里程碑状态变更同步本表。*
