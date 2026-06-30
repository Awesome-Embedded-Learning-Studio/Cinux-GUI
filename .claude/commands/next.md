---
description: 脚手架化一批(默认 ROADMAP 的 🔜 阶段;或 $1 指定)
argument-hint: "[阶段或批次]"
allowed-tools: Bash(grep:*), Bash(ls:*), Bash(git log:*), Read, Grep, Glob
---
@docs/ROADMAP.md
为「$1」(默认 🔜 NEXT 阶段)产出脚手架：①范围 ②触及文件(grep 定位,给绝对路径,限 `core/ host/ docs/`;阶段不存在则报错停) ③API/签名草案 ④完成门(standalone ctest 全绿;真 host 路径加 QEMU 手动冒烟) ⑤提交草案(无 Co-Auth) ⑥相关 gotcha。停下等确认,不开改。
定位示例：!`grep -rn "flush\|render_frame" core/ host/ 2>/dev/null | head -30`
