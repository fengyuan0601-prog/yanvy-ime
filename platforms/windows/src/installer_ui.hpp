// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "version.hpp"
#include "machine_store.hpp"
namespace yanvy {
inline std::filesystem::path SelectDefaultInstall(bool preferred,const std::filesystem::path& local){
  return preferred?std::filesystem::path(L"E:\\Codex\\Programs")/L"YANVY"/kVersion:local/L"Programs"/L"YANVY"/kVersion;
}
inline std::filesystem::path SelectSetupTemp(bool preferred,const std::filesystem::path& fallback){
  return preferred?std::filesystem::path(L"E:\\Codex\\Temp"):fallback;
}
inline std::filesystem::path ResolveInstallPath(const std::filesystem::path& selected){
  const auto name=selected.filename().wstring();
  if(VersionDirectoryName(name)&&_wcsicmp(selected.parent_path().filename().c_str(),L"YANVY")==0)return selected.parent_path()/kVersion;
  if(_wcsicmp(selected.filename().c_str(),L"YANVY")==0)return selected/kVersion;
  return selected/L"YANVY"/kVersion;
}
bool ValidInstallPath(const std::filesystem::path& path);
int InstallerUI(HINSTANCE instance,bool uninstall,bool preview,int show);
int RenderInstaller(HINSTANCE instance,const std::filesystem::path& output,UINT dpi,int page);
}
