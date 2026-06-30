---
description: 当前 diff 对照本仓不变量自检(约定+架构)
allowed-tools: Bash(git diff:*), Bash(git status:*), Bash(git --no-pager diff:*), Read, Grep
---
当前改动：!`git --no-pager diff --stat`
!`git --no-pager diff`
对照 CLAUDE.md「架构不变量 + 始终遵守」逐条检查:Host ABI 唯一缝 / core host-neutral(zero host includes)/ no-exc no-RTTI / flush 模型 / region 不欠覆盖 / 整数绘制 / 500 行限 / 命名注释 / 提交格式(无 Co-Auth)。列违规+定位+建议修法。只报告,不自动改(除非要求)。
