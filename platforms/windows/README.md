# Windows 原生开发测试

版本：0.1.0-rc.3，Windows 10/11 x64 的 64 位桌面应用测试目标。**未签名候选版，不是稳定日用版。** 维护者此前已有真实输入试用反馈；本次新安装器和多方案输入的完整应用/升级卸载矩阵仍待完成。见 `docs/RC3-READINESS.md`；以前候选版记录保留。

当前只构建并注册 x64 TSF DLL，**暂不支持 32 位宿主应用**（包括 32 位 Office/WPS）；这需要后续补充 x86 前端，不能仅靠测试或重新安装解决。

已有真实实现：TSF COM DLL、组合串编辑与取消、焦点 sink、跟随文本位置的 Win32 候选窗、数字选词/翻页、Ctrl+Space 中英切换、Ctrl+Shift+F12 五笔/全拼切换、受限字号/配色设置、单一 librime 引擎进程及 NSIS 安装卸载源码。

rc.1 增加全拼、小鹤双拼、自然码双拼、微软双拼的可见选择。安装页面可以直接选默认方案，设置分“输入方案 / 外观 / 关于”，候选窗方案名可点击进入设置；不要求记快捷键。固定方案来源、许可与哈希均保留。

引擎沿用现有核心和固定词库，创建会话前的策略门没有移除。TSF 复用及适配来源在 `third_party/weasel/PROVENANCE.md`，Windows 前端组合按 GPL-3.0-or-later。官方 Rime SDK 的额外插件及组合许可在 `third_party/windows-runtime/README.md`，产品不请求加载这些插件。

## 构建

需要 C++17 编译器、CMake、Python 3、Windows API 头文件和 NSIS。当前本机使用 E 盘已有 MinGW-w64/UCRT64 GCC 16.2.0、CMake 4.4.3 和 NSIS；CI 另用 MSVC。所有本地依赖、缓存、临时文件应放在 E 盘。

```powershell
$env:TEMP = 'E:\Codex\Temp'
$env:TMP = 'E:\Codex\Temp'
$env:PYTHONUTF8 = '1'
python scripts/fetch_windows_sdk.py --cache-dir E:/Codex/Cache/yanvy --sdk-dir E:/Codex/Programs/YANVY-Rime-1.10.0-clang
cmake -S platforms/windows -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DRIME_INCLUDE_DIR=E:/Codex/Programs/YANVY-Rime-1.10.0-clang/dist/include -DRIME_LIBRARY=E:/Codex/Programs/YANVY-Rime-1.10.0-clang/dist/lib/rime.lib
cmake --build build/windows
```

下载脚本只在开发时显式使用，校验固定归档和 DLL 的 SHA-256，产品不使用该脚本。

将 SDK 的 `rime.dll` 放到 `YANVY-Server.exe` 旁；诊断 CLI 的 DLL 放在其旁。用 `scripts/stage_data.py` 准备全新的 data/profile，再启动 `YANVY-Server.exe --serve`。`--stop` 只请求本用户当前登录会话的引擎退出。`YANVY-Setup.exe --com-smoke` 检查 COM ABI，不等于已注册系统输入法。

## 打包

先提交经过检查的源码。打包脚本拒绝脏工作树、非空目标目录或哈希不符的引擎；从受信源重新准备数据，不复制开发时的 profile、日志或缓存。

```powershell
python scripts/package_windows.py --build build/windows --rime-sdk E:/Codex/Programs/YANVY-Rime-1.10.0-clang --stage E:/Codex/Temp/yanvy-stage-unique
E:/Codex/Programs/LocalMedia-NSIS/makensis.exe /DSTAGE=E:\Codex\Temp\yanvy-stage-unique /DOUTPUT=E:\Codex\Temp\yanvy-payload.exe platforms/windows/installer/yanvy.nsi
cmake -S platforms/windows -B build/windows -DNSIS_PAYLOAD=E:/Codex/Temp/yanvy-payload.exe
cmake --build build/windows --target YANVY-Installer
```

安装包必须与对应源码、第三方组合源码及许可证一并提供。对应源码包含现有词库原文件，以及该 SDK 的 librime、插件、依赖和构建脚本；保留各自原许可。

对外提供原生 `YANVY-Installer.exe` 入口，不能直接分发内部 NSIS 载荷。主界面以普通权限打开，用户点击安装/卸载后才通过正常 UAC 启动嵌入式工作进程；工作进程将 TEMP/TMP 设为 E 盘，并在新的受保护目录提取载荷。NSIS 执行期间原生 UI 保持响应，完成状态来自真实退出结果，未显示虚构百分比。此包要求目标机器有可写的 E 盘。NSIS 编译参数使用绝对 Windows 反斜线路径。

版本号来自根目录 `version.json`，同时进入 CMake、PE 元数据、安装器和对应源码清单，避免仅修改外层文件名。

TSF 全局注册需要正常的 Windows 管理员授权，涉及 Windows 管理的系统注册表。程序、数据和缓存默认安装到 E 盘。不强改默认输入法，不通过全局键盘钩子、剪贴板或模拟粘贴提交文字。安装后先从开始菜单启动引擎，再通过系统输入法切换器选择言舟。

## 实测边界

历史 dev.1 本机 Windows 核心 38 项通过、网络追踪 1 项跳过；本地管道及真实引擎 26 项通过。rc.1 扩展为核心 65 PASS/1 SKIP、管道 35 PASS；含真实双拼输入及每个新增方案的无学习源配置与缓存反例。COM ABI、配置原子保存夹具、目录边界和 NSIS 事务夹具通过；夹具不调用真实系统注册。不要将这些当作 M1/M2/M3 完整验收。

dev.2 使用原生候选窗和设置页，增加圆角、清楚的选中项/序号、字号与深浅色即时预览。设置页已实际检查 18/32 字号、切换主题、保存、重开与恢复默认；96/144/192 DPI 的实际绘制器渲染已检查，跨屏与真实文本位置尚待实测。详见 `docs/UI-DEV2.md`。

升级默认安装到新的版本目录，迁移旧安装的合法字号、配色和方案偏好，并以安装界面确认的默认方案为准。不复制旧引擎 profile 或缓存；v2 端点隔离旧版进程。旧目录保留以便恢复，完成后重开应用或注销再登录以加载新 DLL。请使用 Windows 已安装应用中的当前卸载入口，**不要运行 dev.1 旧目录中的卸载程序**，它可能注销当前共享的输入法注册。

测试操作见 `TESTING.txt`。32 位应用、ARM64、UWP、安全桌面、休眠、升级以及微信/Office/WPS 兼容性目前没有通过记录。
