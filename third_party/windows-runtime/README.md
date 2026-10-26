# Windows 运行库及来源

本目录保存首次 Windows 测试包所需的静态依赖许可，`sources.json` 记录上游来源。原始词库许可仍在各自目录。

Rime 引擎版本为 1.10.0，提交 `295cb2ab68f89ee9d3237c7d4b8033bda3f3b635`。
使用的官方 x64 归档：

`https://github.com/rime/librime/releases/download/1.10.0/rime-295cb2a-Windows-clang.7z`

- 归档 SHA-256：`5ceffc5f93312829c66adfd10eda5160b1802af955aa66ae2bdfd88286d9d363`
- `rime.dll` SHA-256：`6f86dd496524684f195bb89f3fb13fdf61a6b8e551c15353fac4e5955c945c1a`
- 内置插件源码：librime-lua `7f3eca2ce659fc2401b8acb52bd2182b433e12b1`、octagram `bd12863f45fbbd5c7db06d5ec8be8987b10253bf`、predict `72e4d717e56c6542569c88b317700b3471164c42`。
- 其中 octagram 为 GPL-3.0，该组合 DLL 不能仅标成 BSD。Windows 分发应同时提供本项目前端和该引擎组合的对应源码，源码包应包含其构建脚本和依赖。该要求不改变各原创源文件自身的 BSD 许可。
- 插件编入二进制与实际加载不同。`Engine` 明确只请求 `default` 模块（core/dict/gears），固定配置不允许 Lua、预测插件、外部脚本或任意方案；这并不减免组合二进制的许可义务。

glog、leveldb、marisa-trie、OpenCC、yaml-cpp 的固定提交来自该 librime 提交的 submodule 记录；Boost 为上游构建脚本指定的 1.84.0。Lua 自身的许可另存于本目录。

本地平台代码使用 E 盘既有 MinGW-w64/UCRT64、GCC 16.2.0 与 CMake 4.4.3 编译，静态链接 GCC/libstdc++/winpthreads 运行库。其许可与运行库例外一并保留。构建依赖取得只发生在开发时，产品运行不下载内容。
