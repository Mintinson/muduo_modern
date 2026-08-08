# chaoxi 优化清单

本目录记录对 `chaoxi/` 当前实现的性能、正确性、可读性和易用性审阅结果。

主文档：

- [performance-optimization-checklist.md](performance-optimization-checklist.md)：按优先级排列的详细优化清单、实施步骤、基准方案和验收标准。

阅读建议：

1. 先看主文档的“结论摘要”和 P0 项。P0 中有几项是性能测试可信之前必须修复的正确性问题。
2. 再按 Phase 0～4 推进，不建议直接从无锁队列、ET 或 IOCP 等高风险改造开始。
3. 每完成一项，都先保留基线数据和变更后的同环境数据；不能只凭微基准或平均值判断收益。

