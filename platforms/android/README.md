# Android 后续阶段

状态：尚未实现 Android App。

采用 InputMethodService / InputConnection 和原生键盘，复用输入核心与审核过的词库。最终 APK 不声明 INTERNET，核对合并后的 Manifest；处理密码类型与不允许个性化学习的标志，排除个人词库备份/迁移。首阶段五笔/拼音26键，九宫格独立开发验收。

若基于 Trime 二次开发，保留上游 GPL 条款。初次启动指导用户在系统设置启用键盘，不自动替用户改变系统默认输入法。

参考：https://developer.android.com/develop/ui/views/touch-and-input/creating-input-method
