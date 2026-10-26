# GitHub 首次源码发布记录

2026-10-26，开发阶段仍为 **0.1.0-dev**。

- 官方仓库：https://github.com/fengyuan0601-prog/yanvy-ime
- 目标分支：`main`。推送前 Git 与 GitHub API 均确认仓库没有分支或既有提交。
- 首次提交：`fccdbc27633a1f110b4475caee3aacba915cf91c`。
- 89 个文件已在仓库根目录发布，包含原生核心、图标 PNG/ICO、固定词库、独立许可证、文档及 `.github` 工作流。
- 已核对远程 Git tree 和本地提交。没有上传外层 ZIP、本机构建产物、运行数据或凭据。
- 上传前受信配置和第三方清单哈希检查通过；常见凭据模式扫描没有命中。
- GitHub 连接写入返回 403；维护者在 Git Credential Manager 的官方浏览器流程登录后，使用 Git HTTPS 正常推送成功。没有强制推送。
- [首轮 GitHub Actions](https://github.com/fengyuan0601-prog/yanvy-ime/actions/runs/40308008954)：`linux-core` 成功，真实 librime 1.10.0 构建、42 项集成及策略反例检查全部通过。

该结果证明本次开发源码已发布及列明的 Linux 检查成功，不证明 Windows TSF、真实应用输入、安装包或手机平台已经通过。后续 Windows 实现与实测另行记录；原始交接文档中的未上传、403 和 Linux 报告是之前的历史记录。
