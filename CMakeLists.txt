#
# CMakeLists.txt
#
# @brief Cinux-GUI -- the host-neutral GUI core (the ONLY hard seam to any host)
#
# `cinux-gui` is a STATIC library of pure C++17: stdint/stddef only, ZERO host
# includes. The host adapter (e.g. the Cinux kernel's kernel/gui/host_cinux.cpp)
# is the single host-specific TU and is NOT part of this library -- the core
# never depends on it, only the other way around.
#
# This single file builds the core TWO ways:
#
#   1. As a subdirectory of a parent project (add_subdirectory(third_party/Cinux-GUI)):
#      the parent supplies the toolchain + flags; `cinux-gui` is linked into the
#      host (kernel / SDL / X11 / Wayland). No harness.
#   2. Standalone -- this dir is the build root (`cmake -S . -B build`): it
#      declares its own project under a standard hosted compiler and ALSO builds
#      host/fake_host_main.cpp, a smoke harness that drives cinux::gui::pump()
#      with a hand-filled table (no host, no framebuffer). If that links and
#      runs green, the core is provably host-neutral -- and that harness is the
#      seed of the SDL / X11 / Wayland host adapters.

cmake_minimum_required(VERSION 3.20)

# Declare a project only when this directory IS the build root. As a subdirectory
# the parent project is reused and we stay target-only.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cinux-gui LANGUAGES C CXX)
    set(CINUX_GUI_STANDALONE ON)
else()
    set(CINUX_GUI_STANDALONE OFF)
endif()

# ============================================================
# host-neutral core library (built in BOTH contexts)
# ============================================================
add_library(cinux-gui STATIC
    core/font.cpp
    core/gui_core.cpp
    core/region.cpp
    core/scene.cpp
    core/swraster.cpp
    core/abi_check.cpp
)
# core/ is PUBLIC so consumers (a host adapter, the harness) resolve the bare
# same-dir core includes via the target.
target_include_directories(cinux-gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_compile_features(cinux-gui PUBLIC cxx_std_17)

# ============================================================
# standalone-only: host-neutrality smoke harness
# ============================================================
if(CINUX_GUI_STANDALONE)
    # Zero-warning discipline so the lift set is self-policing; subdirectory
    # builds inherit this from the parent.
    add_compile_options(-Wall -Wextra)
    enable_testing()
    add_executable(cinux-gui-smoke host/fake_host_main.cpp)
    target_link_libraries(cinux-gui-smoke PRIVATE cinux-gui)
    add_test(NAME cinux-gui-smoke COMMAND cinux-gui-smoke)

    # Offscreen host: paints a static scene + dumps one PPM frame (P0-b2). The
    # first "you can see it" probe of the render pipeline.
    add_executable(offscreen-dump host/offscreen_host_main.cpp host/ppm_writer.cpp)
    target_link_libraries(offscreen-dump PRIVATE cinux-gui)
    add_test(NAME offscreen-dump COMMAND offscreen-dump)

    # Replay host: scripted pointer events drive a draggable scene, asserts
    # end-state geometry + dirty-region discipline (P0-b3).
    add_executable(replay-dump host/replay_host_main.cpp host/ppm_writer.cpp)
    target_link_libraries(replay-dump PRIVATE cinux-gui)
    add_test(NAME replay-dump COMMAND replay-dump)

    # P1 Probe-1: evdev accumulator unit test (pure logic, no device). IS a ctest.
    add_executable(evdev-test test/test_evdev.cpp host/evdev_device.cpp)
    target_include_directories(evdev-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/host)
    target_link_libraries(evdev-test PRIVATE cinux-gui)
    add_test(NAME evdev-test COMMAND evdev-test)

    # P2-a: Scene data-model unit test (pure POD geometry/capacity/text).
    add_executable(scene-test test/test_scene.cpp)
    target_link_libraries(scene-test PRIVATE cinux-gui)
    add_test(NAME scene-test COMMAND scene-test)

    # P1 Probe-1: real Linux fbdev+evdev host. Manual smoke under QEMU only
    # (needs /dev/fb0 + /dev/input/event*); NOT a ctest.
    add_executable(fbdev-host host/linux_fbdev_main.cpp host/fbdev_device.cpp
                          host/evdev_device.cpp)
    target_link_libraries(fbdev-host PRIVATE cinux-gui)
endif()
