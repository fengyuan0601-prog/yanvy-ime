// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "machine_store.hpp"
#include "user_cleanup_list.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace yanvy {
struct CleanupDirectory {
  std::filesystem::path path;HANDLE handle=INVALID_HANDLE_VALUE;
  CleanupDirectory(std::filesystem::path p,HANDLE h):path(std::move(p)),handle(h){}
  CleanupDirectory(CleanupDirectory&& other)noexcept:path(std::move(other.path)),handle(other.handle){other.handle=INVALID_HANDLE_VALUE;}
  CleanupDirectory(const CleanupDirectory&)=delete;
  ~CleanupDirectory(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
  void Close(){if(handle!=INVALID_HANDLE_VALUE){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;}}
};
inline DWORD GuardCleanupDirectory(const std::filesystem::path& path,bool removable,std::vector<CleanupDirectory>& guards){
  HANDLE directory=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|(removable?DELETE:0),
      FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(directory==INVALID_HANDLE_VALUE)return GetLastError();
  BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(directory,&info)){const DWORD error=GetLastError();CloseHandle(directory);return error;}
  if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
    CloseHandle(directory);return ERROR_ACCESS_DENIED;
  }
  guards.emplace_back(path,directory);return ERROR_SUCCESS;
}
inline bool CleanupParentGuarded(const std::filesystem::path& path,const std::vector<CleanupDirectory>& guards){
  return std::any_of(guards.begin(),guards.end(),[&](const auto& guard){return guard.handle!=INVALID_HANDLE_VALUE&&SameMachinePath(guard.path,path.parent_path());});
}
inline DWORD DeleteCleanupFile(const std::filesystem::path& path,const std::vector<CleanupDirectory>& guards){
  // Never traverse a directory that appeared after the initial guarded scan.
  if(!CleanupParentGuarded(path,guards))return ERROR_SUCCESS;
  HANDLE file=CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
      nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE){DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?ERROR_SUCCESS:error;}
  BY_HANDLE_FILE_INFORMATION info{};DWORD result=ERROR_SUCCESS;
  if(!GetFileInformationByHandle(file,&info))result=GetLastError();
  else if(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))result=ERROR_ACCESS_DENIED;
  else{FILE_DISPOSITION_INFO remove{TRUE};if(!SetFileInformationByHandle(file,FileDispositionInfo,&remove,sizeof(remove)))result=GetLastError();}
  CloseHandle(file);return result;
}
inline DWORD ReadCleanupMarker(const std::filesystem::path& root){
  HANDLE file=CreateFileW((root/L"YANVY-INSTALL-MARKER.txt").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE)return GetLastError();
  BY_HANDLE_FILE_INFORMATION info{};char buffer[1025]{};DWORD count=0;DWORD result=ERROR_INVALID_DATA;
  if(GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
      !info.nFileSizeHigh&&info.nFileSizeLow<=1024&&ReadFile(file,buffer,1024,&count,nullptr)){
    std::string line(buffer,count);const auto end=line.find('\n');if(end!=std::string::npos)line.resize(end);
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line.compare(0,6,"YANVY ")==0){const auto version=line.substr(6);
      if(std::all_of(version.begin(),version.end(),[](unsigned char c){return c<128;})&&VersionDirectoryName(std::wstring(version.begin(),version.end())))result=ERROR_SUCCESS;
    }
  }
  CloseHandle(file);return result;
}
inline DWORD CleanupUserInstallation(const std::filesystem::path& root,bool data_only){
  HANDLE token=nullptr;TOKEN_ELEVATION elevation{};DWORD bytes=0;
  if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return GetLastError();
  const BOOL queried=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&bytes);const DWORD token_error=GetLastError();CloseHandle(token);
  if(!queried)return token_error;
  if(elevation.TokenIsElevated)return ERROR_ELEVATION_REQUIRED;
  if(!PlainLocalPath(root)||root.lexically_normal()!=root||root==root.root_path()||root.wstring().size()>240)return ERROR_INVALID_NAME;
  const auto attributes=GetFileAttributesW(root.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES){DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?ERROR_SUCCESS:error;}
  if(GetFileAttributesW((root/L"YANVY-INSTALL-MARKER.txt").c_str())==INVALID_FILE_ATTRIBUTES){
    const DWORD marker_error=GetLastError();
    if(marker_error==ERROR_FILE_NOT_FOUND||marker_error==ERROR_PATH_NOT_FOUND){
      // The privileged exact cleanup may already have finished, leaving only
      // settings or unknown files. A read-only absence check is idempotent.
      const auto present=[&](const wchar_t* name){
        if(GetFileAttributesW((root/name).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
        const DWORD code=GetLastError();return code!=ERROR_FILE_NOT_FOUND&&code!=ERROR_PATH_NOT_FOUND;
      };
      bool remaining=false;for(auto name:kCleanupDataFiles)remaining=present(name)||remaining;
      for(auto name:kCleanupCacheFiles)remaining=present(name)||remaining;
      if(!data_only)for(auto name:kCleanupRuntimeFiles)remaining=present(name)||remaining;
      if(!remaining)return ERROR_SUCCESS;
    }
  }
  std::vector<std::filesystem::path> ancestors;
  for(auto path=root.parent_path();!path.empty();path=path.parent_path()){ancestors.push_back(path);if(path==path.root_path())break;}
  std::vector<CleanupDirectory> guards;
  for(auto it=ancestors.rbegin();it!=ancestors.rend();++it){const DWORD error=GuardCleanupDirectory(*it,false,guards);if(error)return error;}
  DWORD error=GuardCleanupDirectory(root,true,guards);if(error)return error;
  // Hold every visited directory without FILE_SHARE_DELETE, preventing links
  // or ancestor renames from redirecting exact-file deletion during cleanup.
  std::error_code ec;std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::none,ec),end;
  if(ec)return ERROR_ACCESS_DENIED;
  while(it!=end){
    const auto path=it->path();const auto attr=GetFileAttributesW(path.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES||attr&FILE_ATTRIBUTE_REPARSE_POINT)return ERROR_ACCESS_DENIED;
    if(attr&FILE_ATTRIBUTE_DIRECTORY){error=GuardCleanupDirectory(path,true,guards);if(error)return error;}
    it.increment(ec);if(ec)return ERROR_ACCESS_DENIED;
  }
  error=ReadCleanupMarker(root);if(error)return error;
  DWORD failure=ERROR_SUCCESS;
  const auto remove=[&](const wchar_t* name){DWORD code=DeleteCleanupFile(root/name,guards);if(code&&!failure)failure=code;};
  for(auto name:kCleanupDataFiles)remove(name);
  for(auto name:kCleanupCacheFiles)remove(name);
  if(!data_only)for(auto name:kCleanupRuntimeFiles)remove(name);
  // A locked file leaves the marker available for a later safe retry.
  if(!failure)remove(L"YANVY-INSTALL-MARKER.txt");
  const auto directory=[&](const std::filesystem::path& path){
    for(auto& guard:guards)if(SameMachinePath(guard.path,path)){
      FILE_DISPOSITION_INFO remove{TRUE};
      if(!SetFileInformationByHandle(guard.handle,FileDispositionInfo,&remove,sizeof(remove))){
        const DWORD code=GetLastError();if(code!=ERROR_DIR_NOT_EMPTY&&!failure)failure=code;
      }
      guard.Close();break;
    }
  };
  for(auto name:kCleanupDirectories)directory(root/name);
  if(!failure)directory(root);
  return failure;
}
}
