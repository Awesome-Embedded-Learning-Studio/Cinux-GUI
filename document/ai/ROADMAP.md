# Cinux-GUI ROADMAP — 里程碑全树 + 状态

> 里程碑级（比 DIRECTIVES 易变，比 PLAN 稳）。**当前焦点批级进度见 [PLAN.md](PLAN.md)。**
> 架构背景（七层 + Host ABI）详见 CinuxOS `document/notes/2026-06-21-f13-visor-*.md`，以后择机迁入本仓。

## 定位

**Cinux-GUI = 完整、独立、跨平台的 GUI 系统**：核心（pump / swraster / region / event）+ 桌面层（WM / 终端 / 控件）+ 多 host（Linux 先，Cinux 内核适配器后）。**Host ABI 表（`core/host.hpp`）是唯一缝**——桌面层住表上面、对 host 无知，换 host = 换表填充，核心不变。CinuxOS 只剩一个薄 host 适配器。

## 战略方向（2026-06-30 用户拍板）

1. **极致独立**：GUI 所有细节尽可能进本仓，最大化解耦。本仓 = 完整 GUI 系统。
2. **Host ABI 唯一缝**：桌面层只调表里的 `flush` / `poll_event` / `now_ms` / `spawn`。
3. **CinuxOS 只剩薄 host 适配器**：`kernel/gui/` 的 WM/terminal/desktop 最终搬上表、住进本仓。**长期看 CinuxOS 是减代码。**
4. **Linux 先跑，依赖越底层越好**：fbdev（`/dev/fb0` mmap）+ evdev（`/dev/input/event*`），零外部依赖，跟 Cinux VBE+flush 同海拔 1:1 对照，好调试。SDL/X11/Wayland 是高层 host，以后再加，**不用于调试核心**。

## 里程碑树（本仓自有编号）

| 阶段 | 内容 | 状态 |
|---|---|---|
| **P0** | 探针 Probe-0：无头跑通核心渲染管线（swraster 接 pump + offscreen host + 事件回放 + 极简场景 + 金帧验证） | 🔜 下一刀 |
| **P1** | 探针 Probe-1：真 fbdev + evdev（第二个真 host，证明解耦缝） | 进行中：代码✅ + 单测（4 ctest 绿）；QEMU 真 fbdev 冒烟待手动 |
| **P2** | 渲染收敛核心：swraster 正式接管，compositor dirty-region（只重画脏区，省 composite 本身） | 待启动 |
| **P3** | 控件工具箱：Button / Label / Slider / Container + 布局 + 事件路由 | 待启动 |
| **P4** | 桌面迁入仓库：CinuxOS `kernel/gui/` 的 WM/terminal/desktop 搬上表、host-neutral；CinuxOS 只剩 `host_cinux` | 待启动 |
| **P5** | 字体/文本（PSF → 更全）、主题 | 待启动 |
| **P6** | GPU texture compositor（有真 GPU 目标后；非 primitive draw-list） | 远期 |
| **P7** | 多进程 Surface 协议（attach/damage/commit/release；ring-3 桌面 server） | 远期 |
| **MCU 线** | visor 嵌入式（STM32F1 等）：独立 micro renderer，推迟到真板 RAM<20KB 实测后 | 长弧推迟 |

## 两仓契约（Cinux-GUI ↔ CinuxOS）

- **探针阶段（P0/P1）：CinuxOS 内核零改动。** CinuxOS 仍 pin `4c84eb2`，照常编 / 跑 / 过 `run-kernel-test`。
- **P2 起 Host ABI 若变**：CinuxOS bump pin + 改一处 `kernel/gui/host_cinux.cpp`（`render_frame` 改画 staging Surface）。**这是 CinuxOS 侧唯一对接点。**
- **P4 后**：CinuxOS `kernel/gui/` 的 WM/terminal/desktop 搬走，**CinuxOS GUI 代码净减少**，只剩 `host_cinux` + framebuffer handoff。
- **push / PR**：Claude 在 feature 分支 commit（绿才提交）；**push 与 PR 由用户控制**。

> **CinuxOS 侧 F13 文档待同步**（CinuxOS 在疯狂开发，不急，择机）：把 ROADMAP F13 行改成指向本仓；清理 `document/todo/README.md` 里 stale 的 3-milestone 模型；visor-02 §9 risk 表对齐 git 现状（PIT 反转等项已落地）。
