# 许可证与来源索引

本文件记录当前组件的实际许可，不把整个仓库视作可以随意换许可证的单一作品。

| 组件 | 当前安排 |
| --- | --- |
| 原创 `core/`、`scripts/`、`tests/`、文档和配置补丁 | BSD-3-Clause；根目录 `LICENSE` |
| librime 引擎 | BSD-3-Clause；`third_party/librime/LICENSE`；运行库不随本源码包分发 |
| rime-wubi | 保留上游 LGPL-3.0 许可原文、作者与 commit |
| rime-pinyin-simp | 保留上游 Apache-2.0 许可原文、作者与 commit |
| rime-prelude、rime-stroke | 保留上游 LGPL-3.0 许可原文、作者与 commit |
| PicoSHA2 | MIT；`third_party/picosha2/LICENSE`，用于启动前校验固定输入文件的 SHA-256 |
| 品牌 PNG 与尺寸导出 | 本项目 AI 生成视觉素材；来源、用途和导出记录见 `assets/brand/PROVENANCE.md` |
| 未来 Weasel / Trime 衍生前端 | 若实际引入，按所用上游版本的 GPL 条款保留对应源码与许可；当前未引入其前端源码 |
| 未来 iOS 前端及词库组合 | 逐项核查其代码、数据、依赖和发行条件，尚未完成 App Store 许可审查 |

精确上游版本、文件 SHA-256 与本轮实测依赖包见 `third_party/manifest.lock.json`。原始上游文件不作静默修改；本项目修改放在明确的补丁或适配器中。

公开源码不表示已经完成应用商店审核、商标注册或对第三方素材的额外授权。目录分开也不能改变实际复制、链接或衍生关系。

官方版本坚持无广告、无输入上传、不接受追踪功能。这是官方产品和贡献原则，不向标准开源许可证附加“禁止商用”等用途限制。第三方不得把自己的修改版冒充本项目官方发行版。

参考原始来源：

- https://github.com/rime/librime
- https://github.com/rime/rime-wubi
- https://github.com/rime/rime-pinyin-simp
- https://github.com/rime/rime-prelude
- https://github.com/rime/rime-stroke
- https://github.com/okdshin/PicoSHA2
- https://github.com/rime/weasel
- https://github.com/osfans/trime
- https://opensource.org/osd
- https://developer.apple.com/support/terms/apple-developer-program-license-agreement/
