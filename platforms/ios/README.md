# iPhone 后续阶段

状态：尚未实现 iOS App 或进行实机验证。

采用原生键盘扩展，目标 `RequestsOpenAccess=false`，不开“允许完全访问”也可完成基本五笔与拼音输入。主 App 写入配置和用户主动导入的公共词库，键盘只读共享容器；未来本地学习如实施，仅写键盘私有目录，并单独验证清除、备份与导出限制。

安全密码、特定电话输入框与拒绝第三方键盘的宿主应用遵从系统回退。正式上架前逐项核查原创前端、引擎、词库和组件的许可与 App Store 发行条件。此目录不表示这些条件已经验证。

参考：https://developer.apple.com/documentation/uikit/configuring-open-access-for-a-custom-keyboard
