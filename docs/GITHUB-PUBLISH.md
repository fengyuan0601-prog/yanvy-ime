# GitHub 发布与维护

日期：2026-10-26。维护者已创建公开的[官方仓库](https://github.com/fengyuan0601-prog/yanvy-ime)，默认分支为 `main`，当前进入 `0.1.0-dev` 首批源码上传阶段。远程内容和CI结果分别以仓库实际提交与 [Actions运行记录](https://github.com/fengyuan0601-prog/yanvy-ime/actions) 为准，不预先声称上传完成或CI通过。

## 官方仓库

- 所有者：`fengyuan0601-prog`。
- 仓库名：`yanvy-ime`。
- 地址：<https://github.com/fengyuan0601-prog/yanvy-ime>。
- 可见性：Public，符合开源方向。
- 建议简介：`言舟 · YANVY — 免费、无广告、离线优先的五笔与拼音输入法。Windows 与手机端开发中。`
- 初次发布阶段：`0.1.0-dev`，明确只有真实输入核心，尚无 Windows / 手机安装包。

名称是当前开发方案；同名初筛与未完成的核查见 [NAMING.md](NAMING.md)。

## 本次上传检查结果

ChatGPT 的 GitHub 连接已确认仓库存在、公开、默认分支为 `main`。尝试创建首个 `.gitattributes` 文件时，GitHub 返回 `403: Resource not accessible by integration`；这次请求没有成功产生远程提交。当前环境没有已认证的 Git CLI 写入通道。

因此本批状态是**源码已准备、远程上传尚未成功**。OpenAI 当前标准 GitHub 连接说明列明读取能力，建议使用 Codex 推送代码；这不是让维护者反复登录或重建仓库就能保证解决的问题。接续步骤见 [PUBLISH-WITH-CODEX.md](PUBLISH-WITH-CODEX.md)。

依据：[OpenAI GitHub 连接说明](https://help.openai.com/en/articles/11145903-connecting-github-to-chatgpt)、[GitHub API 权限错误说明](https://docs.github.com/en/rest/using-the-rest-api/troubleshooting-the-rest-api#resource-not-accessible)。

## 首次上传

创建空仓库这一步已由维护者完成；首次上传开始前确认仓库尚无源码内容。以下是首次推送的Git客户端操作参考，不是已经执行或推送成功的记录；应先检查现有本地历史和远程状态，再使用本人已认证的客户端：

```bash
git init -b main
git add .
git commit -m "Start YANVY offline Wubi and Pinyin core"
git remote add origin https://github.com/fengyuan0601-prog/yanvy-ime.git
git push -u origin main
```

若工程已经保留 Git 历史，先检查 `git status` 和 `git remote -v`，沿用已有历史与正确远程，不重复初始化、不强制推送。不要把口令、访问令牌或个人输入数据写进仓库。

`.gitignore` 已排除本机构建产物、运行时用户词典、日志和常见凭据文件；源码包保留原始词库、版本锁及许可证。`.gitattributes` 固定文本文件为 LF，避免 Windows 自动换行转换改变受信文件哈希。

## 首次公开后的核对

1. 首页正确显示图标、当前能力、未完成平台和构建方法。
2. 仓库 Actions 实际完成 Linux 核心流程；已编写工作流不等于远程运行通过。
3. 独立重建一次核心，确认与报告中的测试范围相符。
4. 在仓库设置中按需启用安全漏洞私下报告；未启用前不要在文案中声称已经可用。
5. 以后每个可安装发行包绑定对应源代码提交、SHA-256、环境版本和验证记录；不要将当前源码包标为正式安装版。

## 自愿赞助与品牌入口

`.github/FUNDING.yml.example` 是未启用的示例。只有维护者实际确认可用的赞助账号或页面后，才创建真正的 `.github/FUNDING.yml`。不编造收款地址，不把赞助设成功能、隐私或更新的门槛。

开发者品牌、文档与其他项目链接可放在仓库、官网及合适的主程序关于页。候选栏和键盘不放广告、引流弹窗或追踪参数。

## 维护节奏

使用 Issue 模板记录问题与环境，用 Pull Request 审查代码、配置、词库与许可变更。对词库和隐私配置的改变，先审查，再更新受信清单并重跑受影响的功能和反例。日常维护重点是修复输入与兼容问题，避免一开始同时承担多个未稳定的平台发行承诺。

官方参考：[创建仓库](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-new-repository)、[赞助按钮](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/displaying-a-sponsor-button-in-your-repository)、[Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)。
