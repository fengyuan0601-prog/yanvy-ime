# 0.1.6 安装启动权限修复

日期：2026-10-31。

另一台 Windows 11 x64 设备的 0.1.4 安装器报告 `phase=unknown`、`result=0x000002E4`、`category=unclassified`。用户说明是普通双击。740 是 Windows 的 ERROR_ELEVATION_REQUIRED；结合该版本的普通用户数据 worker 入口，管理员权限继承是可能原因，但现有诊断不能确定另一台设备的账户、兼容性标志或 UAC 状态。不得把普通双击解释为用户主动选择了管理员运行，也不把更换安装目录作为修复。

0.1.6 在任何安装写入前核验实际 token。若向导意外继承管理员或非普通权限，尝试通过同一会话的普通 Windows 桌面重新打开当前安装包。使用 Explorer 的 Shell Automation，而不是改造管理员 token：标准账户的 UAC 凭据可能属于另一位管理员，原始交互用户应由桌面确定。转交参数仅有固定 UI 标记，不转交任何 worker、目录或命令；子进程再次核验 token，且不会循环重启。系统组件仍只由用户正常 UAC 授权的受保护 worker 安装。

如果 UAC 已关闭而整个管理员桌面均为高权限，Windows 不提供该普通桌面，软件保持拒绝以管理员身份处理用户自选词库，并在写入之前显示明确说明。这是可识别的环境限制；软件不会修改 UAC、兼容性选项或其他 Windows 安全设置，也不会声称已修复目标设备。

诊断继续仅在本机显示，不自动上传；新增固定枚举的 process_token、desktop_token、uac_setting，明确 startup/data-prepare/machine-install/user-configuration 阶段，以及 740、取消授权、拒绝访问的错误类别。不包括账户名、SID、路径、输入内容或任意异常正文。

沿用 0.1.5 视觉与输入行为；为升级保留旧服务的正常加载与设置转发入口，六代身份纳入安全退役和失败回退。

本机 Windows 11 x64 开发验证：x64/x86 真实 Explorer 普通权限子进程与 token 边界、最小诊断、六代注册退役和失败保留、注册修复 29 项通过；x64 包完整性与重用所有权 55 项、旧设置入口恢复与转发 36 项、16 项路径及无 E 盘合成回退通过。6 个隔离 NSIS 场景通过（成功、中文空格路径、注册失败、回退失败、同版改位置、同版注册失败）；数据载荷测试通过，含不携带 EXE/DLL、未知文件保留、提权或未知 token 拒绝、链接拒绝与失败清理。以上均不执行正式 TSF 注册或改变现用输入法。

已打开实际编译的原生预览窗口，检查 0.1.6 排版、控件和中文空格路径编辑，实际截图留存本机维护目录。最终安装包初始窗口检查和 CI 记录另在发行验证摘要补充。本机测试源进程为普通权限；实际管理员源进程、跨管理员凭据转交、另一台电脑的实际安装及该电脑是否关闭 UAC 尚未验证。本机没有关闭 UAC、切换内置管理员账户或代替用户确认系统授权。低完整性和 AppContainer 等受限进程直接拒绝，不尝试向桌面转交。

依据：[Microsoft 错误码](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--500-999-)；[通过 Explorer 启动普通用户进程](https://devblogs.microsoft.com/oldnewthing/20131118-00/?p=2643)；[关闭 UAC 的限制](https://devblogs.microsoft.com/oldnewthing/20150618-00/?p=45351)。
