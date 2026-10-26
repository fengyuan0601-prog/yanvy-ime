// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "version.hpp"
namespace yanvy {
bool ValidInstallPath(const std::filesystem::path& path);
int InstallerUI(HINSTANCE instance,bool uninstall,bool preview,int show);
int RenderInstaller(HINSTANCE instance,const std::filesystem::path& output,UINT dpi,int page);
}
