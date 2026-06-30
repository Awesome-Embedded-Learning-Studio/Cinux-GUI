---
description: 验证当前批 — standalone ctest 绿则更新 ROADMAP 并起草提交(无 Co-Auth),红则只报
allowed-tools: Bash(cmake:*), Bash(ctest:*), Bash(git:*), Edit
---
跑验证(首次/改 CMake 先 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`):
!`cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
判定:ctest 全 passed、0 failed = 绿。
- 红:列失败项,不改 ROADMAP、不提交,给方向后停。
- 绿:① 更新 `docs/ROADMAP.md`(阶段标✅+commit 短 hash;挪 🔜) ② 起草提交 `<type>(<scope>): <简述>`,**不带 Co-Authored-By/AI 署名**,不自动提交 ③ **写 `docs/notes/<date>-<phase>-<batch>.md`**(背景/目标/设计/决策/陷阱/验证,参考 `docs/notes/` 既有风格;非 diff 复述) ④ 报告改动+测试+建议 git 命令。
真 host 路径(Probe-1 fbdev/evdev)的 QEMU 手动冒烟不进 CI,但须在笔记记录结果。
