# Windows 首发接入

状态：规划与工程入口；本批没有 Windows 安装包或已实测 TSF 组件。

继续使用 `core/ime_core.hpp` 的真实输入核心，采用 Windows TSF 系统接入。先建立独立进程或清晰生命周期的核心服务，再完成组合文本、候选窗、焦点切换、按键消费、取消与提交。需处理 32/64 位宿主、多 DPI、快捷键和异常恢复。

可以评估基于 Weasel 的成熟前端二次开发，但必须保留实际 GPL 许可、明确修改范围并裁剪其更新/下载/日志等非目标能力；不能仅导入本项目配置后就把上游发行版称为已核验的 YANVY。

首发验收：记事本、Edge/Chrome、微信、Word/Excel/WPS 中输入与取消正确；安装、切换、升级、卸载可复现；正式组件不产生输入日志或 IP 联网。禁止用全局键盘钩子模拟完整输入法。

参考：https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editors
