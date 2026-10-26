# 双拼方案来源

原始仓库：https://github.com/rime/rime-double-pinyin

固定提交：`01a13287cbd27819be1c34fa1ddc1b3643d5001b`。原始方案、作者与 GPL-3.0 许可逐字节保留，SHA-256 记在 `third_party/manifest.lock.json`。仅分发小鹤、自然码、微软三个方案，不使用搜狗的代码、图标或词库。

YANVY 在独立 `.custom.yaml` 补丁中将词典指定为已有且许可清楚的 `pinyin_simp`，移除简繁过滤组件并固定无学习参数；保留上游双拼键位、预编辑格式与笔画反查。默认配置增加这三个固定方案，不开放任意脚本或外部方案导入。
