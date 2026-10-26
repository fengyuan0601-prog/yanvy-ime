# Weasel TSF 来源与修改

- 上游：https://github.com/rime/weasel
- 固定版本：0.17.4
- 对应提交：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`。
- 原始作者和版权沿用上游；许可：GPL-3.0-or-later，保留 `LICENSE.txt`。
- `WeaselTSF/` 和 `include/` 中是未修改的原始参考文件；文件哈希列于 `manifest.json`。

`platforms/windows/src/edit_session.hpp` 从 `EditSession.h` 适配；`tsf.cpp` 从 Composition、ThreadMgrEventSink、TextEditSink 和 KeyEvent 中复用并适配了 TSF 编辑锁、组合范围、选择位置、焦点 sink 与虚拟键映射；`setup.cpp` 适配其注册顺序。这些衍生文件采用 GPL-3.0-or-later，**Windows 前端组合分发按 GPL-3.0-or-later 提供对应源码**，根目录 BSD 不覆盖这些文件。

适配使用 WRL COM 指针代替 ATL，使用现有 `ime::Engine` 的本地引擎进程代替 Weasel IPC/服务器，候选窗独立使用 Win32/GDI。没有包含或启动上游更新器、同步、Lua 配置加载、部署器 UI 或输入日志。维护前与会话创建前的固定方案策略仍由原有核心执行。

这只是复用部分的来源说明；不声称已经复现上游全部兼容性，也不把上游的测试结果作为本项目前端验收结果。
