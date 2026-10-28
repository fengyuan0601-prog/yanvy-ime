// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "version.hpp"
namespace yanvy {
inline std::filesystem::path SelectDefaultInstall(bool preferred,const std::filesystem::path& local){
  return preferred?std::filesystem::path(L"E:\\Codex\\Programs")/L"YANVY"/kVersion:local/L"Programs"/L"YANVY"/kVersion;
}
inline std::filesystem::path SelectSetupTemp(bool preferred,const std::filesystem::path& fallback){
  return preferred?std::filesystem::path(L"E:\\Codex\\Temp"):fallback;
}
bool ValidInstallPath(const std::filesystem::path& path);
int InstallerUI(HINSTANCE instance,bool uninstall,bool preview,int show);
int RenderInstaller(HINSTANCE instance,const std::filesystem::path& output,UINT dpi,int page);
}
