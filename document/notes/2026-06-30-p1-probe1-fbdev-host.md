# 2026-06-30 · P1 Probe-1 · Linux fbdev+evdev host（第二个真 host）

> P0 standalone 跑通后，P1 用真 Linux fbdev + evdev 当第二个真 host，证明 Host ABI 解耦缝干净——同一个 core（pump/staging/swraster/region）只换表填充，就从 offscreen/replay 换成真屏+真鼠标。**代码 + 单测完成；真 QEMU 冒烟待手动。**

## 背景

P0 的 offscreen/replay host 是「假 host」（无真屏/真鼠标，事件 scripted）。P1 要「真 host」：mmap `/dev/fb0` 当屏、读 `/dev/input/event*` 当鼠标，跑在 QEMU Linux 上，真鼠标动→光标跟。这是「证解耦缝」的临门一脚。

## 目标

1. fbdev host（fbdev mmap + evdev + Host 表），复用 b2/b3 场景 + 拖拽状态机。
2. evdev `input_event` 流 → `PointerPayload` 的转换可单测（不依赖真设备）。
3. WSL2 编译验证 + 单测；真 QEMU 冒烟给步骤（手动）。

## 设计

### `host/fbdev_device.{hpp,cpp}` — fbdev 后端

`open("/dev/fb0")` → `ioctl(FBIOGET_VSCREENINFO/FIXEDSCREENINFO)` 拿 xres/yres/line_length → `mmap`。`blit_rect(x,y,w,h,src,src_stride)` 把 core staging 的脏 rect memcpy 进 framebuffer（按 fb stride 定位行，不是 staging stride）。32bpp XRGB8888 路径；其他布局 `ready()==false`。

### `host/evdev_device.{hpp,cpp}` — evdev 后端 + 纯累加器

**`EvdevAccumulator`（纯函数，单测核心）**：evdev 一次 `read` 给的是 `input_event` 记录流，一个完整 pointer frame = 两次 `EV_SYN` 之间的变化集。`feed(type,code,value)` 累积 ABS_X/ABS_Y/EV_KEY（BTN_LEFT/RIGHT/MIDDLE），`finish()` 在 SYN 时产出一帧（位置 + buttons + kind：button 边沿→down/up，否则 move）并复位。touched 标志区分 idle SYN。

**`EvdevDevice`**：`open` 抢占设备（`EVIOCGRAB`，避免 console 也收）+ `O_NONBLOCK`；`read_frame()` 非阻塞 drain，喂 accumulator，遇 SYN 返回一帧。

### `host/linux_fbdev_main.cpp` — host main

组装 Host 表：`poll_event`=evdev→Event，`dispatch_event`=拖拽状态机（同 b3），`render_frame`=场景+真脏区（同 b3），`flush`=`fb.blit_rect`。主循环 `for(;;) core.pump(); usleep(16000)`（~60fps 节流；idle 帧 flush 0）。`timeout 40` 外部杀。

### `test/test_evdev.cpp` — accumulator 单测

喂合成 `input_event` 三元组，验：idle SYN / ABS move / BTN 边沿 down/up / 按住移动（无新边沿）/ 同值 idle / 多键位。**不碰 fd**，纯逻辑。

## 决策

- **EvdevAccumulator 纯函数**：evdev→pointer 转换是 P1 的复杂点；抽成无 fd 的状态机，单测喂 fake 流，不依赖 `/dev/input/event*`。真设备只影响 `EvdevDevice::read_frame`（把 fd 字节喂给 accumulator）。
- **SYN_DROPPED 重置**：内核丢事件时，P1 简化重置 accumulator（位置归零）。后续可 `EVIOCGABS` 查当前绝对轴值恢复——P1 冒烟够用。
- **`usleep(16000)` 节流**：P1 简单（轻忙等，idle 不写屏）。真 `poll(fd)` 阻塞等输入留后续优化。
- **组织：`test/` vs `host/`**：纯单测（不填 Host 表）进 `test/`；host 适配器 + harness main（填 Host 表，含 fake/offscreen/replay/linux-fbdev）留 `host/`。已写进 CODING-TASTE §2。

## 陷阱

- **evdev 不是一 read 一事件**：一帧鼠标 = 多个 `input_event`（X/Y/button 各一条）+ `EV_SYN`。必须 accumulator 同步。
- **`poll_event` 必须非阻塞**（`O_NONBLOCK`）：pump 的 `while(poll_event)` 会 drain 到空，阻塞 read 会卡死整个 GUI。
- **EVIOCGRAB**：抢占设备避免 console TTY 也收事件（否则鼠标同时移动光标 + 切 console）。
- **fbdev stride ≠ width×4**：按 `finfo.line_length` 定位行（VESA fb 可能行尾有 padding）。
- **部署 libc 兼容**：Alpine 是 musl、Debian 是 glibc——WSL2 编的 glibc 动态二进制不能直接跑 Alpine。静态编译或镜像内原生编译（见 runbook）。

## 验证

- **4 ctest 全绿**：cinux-gui-smoke + offscreen-dump + replay-dump + **evdev-test**（accumulator 7 断言）。
- **ASAN 干净**：evdev-test 直接 run exit 0。
- **fbdev-host 编译通过**（32KB 二进制；WSL2 无 /dev/fb0 不能本机跑）。
- **真 QEMU 冒烟待手动**（见 runbook）。

## QEMU 冒烟 runbook（手动）

### 1. 准备一个带 fbdev 的 Linux 镜像
任一带 vesafb 的轻量 Linux QCOW2（Alpine 或 Debian cloud）。fbdev 要出现 `/dev/fb0`（`-vga std` → vesafb）。

### 2. 部署 fbdev-host 二进制
两种（任选）：
- **镜像内原生编译（最简，无 libc 兼容坑）**：拷本仓源码进镜像 → `apk add cmake g++`（Alpine）或 `apt install cmake g++`（Debian）→ `cmake -S . -B build && cmake --build build -j$(nproc) fbdev-host`。
- **WSL2 静态编译 + 拷二进制**：`cmake -S . -B build -DCMAKE_EXE_LINKER_FLAGS=-static ...` → 拷 `build/fbdev-host` 进镜像（Alpine 要 musl-gcc 静态；Debian glibc 静态）。

### 3. QEMU 启动（fbdev + usb-tablet 鼠标 + VNC）
```
qemu-system-x86_64 -m 512 \
  -drive file=linux.qcow2,format=qcow2 \
  -vga std \
  -usb -device usb-tablet \
  -display vnc=:0
```
- `-vga std`：vesafb，镜像内 `/dev/fb0`
- `-usb -device usb-tablet`：USB tablet 鼠标（绝对坐标 → evdev `/dev/input/event*`）
- `-display vnc=:0`：宿主 `vncviewer :0` 看屏

### 4. 冒烟
镜像内（root 或 video/input 组）：
```
# 确认鼠标的 event 设备号（fbdev-host 默认 event0，按实际改 linux_fbdev_main.cpp）
cat /proc/bus/input/devices
timeout 40 ./fbdev-host
```
**预期**：VNC 屏幕出现窗口（蓝色标题栏 "Cinux" + "Hello / Cinux-GUI"）+ 白光标；移动鼠标→光标跟；按住拖标题栏→窗口跟动；松开→窗口停。`timeout 40` 40 秒后自动退。

### 5. 记结果
回填本笔记「真跑结果」+ ROADMAP P1 状态。

## 后续

- **真跑反馈**：用户跑完回填结果（光标跟动 / 拖拽 / 是否崩）。若 evdev 设备号/event 解析有问题，调 `linux_fbdev_main.cpp`。
- **P2 渲染收敛**：swraster 正式接管，compositor dirty-region 双缓冲差分（b3/P1 的「整屏重画 staging」优化成真差分）。
- **CinuxOS 零改动**（P1 standalone；CinuxOS 自己的 host_cinux 是另一个真 host，本仓不动它）。
