# Cinux-GUI — Claude Code 工作指引

CinuxOS 的 GUI 系统,**独立开发**(本仓 = 完整 GUI 系统,与 CinuxOS 解耦)。Host-neutral core + 多 host;`core/host.hpp` 函数指针表(Host ABI)是唯一硬缝。完整路线图见 `docs/ROADMAP.md`(单一事实源)。

## 文档分层(按耐久度,按需读)
- `docs/ROADMAP.md` — 里程碑树 P0-P7 + 状态 + 战略 + 第一刀探针(**单一事实源**)
- `docs/notes/<date>-<phase>-<batch>.md` — 工作记录(完成一批立即补一篇,参考既有风格;非 diff 复述)
- 架构背景:CinuxOS `document/notes/2026-06-21-f13-visor-*.md`(七层架构 + Host ABI,以后择机迁入)

## 始终遵守(每条便宜,违规代价大)
- **验证默认用 standalone ctest**(不是 `run-kernel-test`——那是 CinuxOS 的):
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
  host 单测 push 前开 ASAN 自验:`-DCMAKE_CXX_FLAGS="-fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"`(CinuxOS 学到的教训:本地默认 ctest 不开 ASAN 会漏判)。Probe-1 真 fbdev 路径:QEMU-Linux 手动冒烟(非 CI,结果记笔记)。
- **本仓与 CinuxOS 解耦**:开发都在本仓;CinuxOS 经 submodule pin 追踪。GUI 改动**不动 CinuxOS 内核**。
- 一批一 commit 一验证;ctest 绿才提交。
- 提交信息 `<type>(<scope>): <中文简述>`(纯描述),**不带 Co-Authored-By / AI 署名**。**push / PR 由用户控制**,Claude 不做。
- 改前查牵连(grep 引用方,限 `core/ host/`)。

## 架构不变量(铁律)
- **Host ABI 是唯一硬缝**(`core/host.hpp`):core 对 host 一无所知——**zero host includes**,只 stdint/stddef。换 host = 换表填充,core 不变。
- **C++17,no exception / no RTTI**;swraster 纯整数(Q8.8 定点),不引浮点。
- **flush 显示模型**:host 提供 `flush(area, pixels, stride, fmt)`;core 拥 staging buffer,按脏 rect 推。非 `begin_frame→pointer`。
- **Region 永不欠覆盖**:容量溢出坍缩为包围盒(过覆盖 = 性能成本;欠覆盖 = stale-pixel bug)。
- **单 GUI 线程**:输入走 SPSC 队列(ISR/其他上下文只 push,GUI 线程 pump 只 pop),core 不持锁、不 busy-wait。
- **optional 功能归 CMake,不归源码 `#ifdef`**(CinuxOS DIRECTIVES §14 精神:开关属 CMake,调用点零 #ifdef)。
- 文件 **500 行硬限**;标识符 + 注释英文,私有成员 `_`,常量/枚举值 `k` 前缀;提交前 clang-format。

## 命令(`.claude/commands/`)
战术:`/resume` `/status` `/next [阶段]` `/done`
战略:`/roadmap` `/milestone [阶段]` `/audit`

## 两仓关系
CinuxOS pin 本仓 submodule;P0/P1 阶段 CinuxOS 内核零改动。CinuxOS 侧唯一对接点 = `kernel/gui/host_cinux.cpp`(Host ABI 表填充)。详见 `docs/ROADMAP.md` §两仓契约。
