// SPDX-License-Identifier: BSD-3-Clause
// Synthetic files and explicit fresh root only; never installs or runs a broker.
#include "user_cleanup.hpp"
#include <fstream>
#include <iostream>
#include <winioctl.h>

void Write(const std::filesystem::path& path,const std::string& text){std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<text;}
void Populate(const std::filesystem::path& root){
  Write(root/L"YANVY-INSTALL-MARKER.txt","YANVY 0.1.0-fixture\n0123456789abcdef\n");
  Write(root/L"YANVY-Server.exe","not an executable\n");Write(root/L"data"/L"default.yaml","synthetic data\n");
  Write(root/L"profile"/L"build"/L"wubi86.table.bin","synthetic cache\n");
  Write(root/L"settings.ini","preserve settings\n");Write(root/L"private"/L"custom.txt","preserve unknown\n");
}
bool MakeReadOnlyFixtureDirectory(const std::filesystem::path& path){
  PSECURITY_DESCRIPTOR descriptor=nullptr;const auto sddl=L"D:P(A;OICI;GRGX;;;"+yanvy::UserSid()+L")";
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
  BOOL present=FALSE,defaulted=FALSE;PACL acl=nullptr;GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted);
  const DWORD result=SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);
  LocalFree(descriptor);return result==ERROR_SUCCESS;
}
bool Junction(const std::filesystem::path& link,const std::filesystem::path& target){
  if(!CreateDirectoryW(link.c_str(),nullptr))return false;
  HANDLE directory=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(directory==INVALID_HANDLE_VALUE)return false;
  struct Buffer{DWORD tag;WORD length,reserved,sub_offset,sub_length,print_offset,print_length;wchar_t paths[32768];} data{};
  const auto print=target.wstring(),sub=L"\\??\\"+print;data.tag=IO_REPARSE_TAG_MOUNT_POINT;
  data.sub_length=WORD(sub.size()*2);data.print_offset=WORD((sub.size()+1)*2);data.print_length=WORD(print.size()*2);
  memcpy(data.paths,sub.c_str(),(sub.size()+1)*2);memcpy(reinterpret_cast<BYTE*>(data.paths)+data.print_offset,print.c_str(),(print.size()+1)*2);
  DWORD bytes=DWORD(offsetof(Buffer,paths)+(sub.size()+print.size()+2)*2),returned=0;data.length=WORD(bytes-8);
  const bool ok=DeviceIoControl(directory,FSCTL_SET_REPARSE_POINT,&data,bytes,nullptr,0,&returned,nullptr)!=FALSE;CloseHandle(directory);return ok;
}
int wmain(int argc,wchar_t** argv){
  if(argc!=2)return 1;
  const auto base=std::filesystem::path(argv[1]);
  if(!base.is_absolute()||base.lexically_normal()!=base||base==base.root_path()||std::filesystem::exists(base))return 2;
  const auto full=base/L"中文 旧目录";Populate(full);
  DWORD result=yanvy::CleanupUserInstallation(full,false);
  if(result==ERROR_ELEVATION_REQUIRED){std::cout<<"elevated token correctly refused; ordinary-user cleanup cases SKIP (medium token required)\n";return 0;}
  if(result!=0||std::filesystem::exists(full/L"YANVY-Server.exe")||std::filesystem::exists(full/L"data"/L"default.yaml")||
      !std::filesystem::exists(full/L"settings.ini")||!std::filesystem::exists(full/L"private"/L"custom.txt"))return 3;
  if(yanvy::CleanupUserInstallation(full,false)!=0)return 16;
  if(!MakeReadOnlyFixtureDirectory(full))return 19;
  const DWORD readonly_result=yanvy::CleanupUserInstallation(full,false);
  if(readonly_result!=0)std::cerr<<"already-cleaned read-only result="<<readonly_result<<'\n';
  if(readonly_result!=0||
      !std::filesystem::exists(full/L"settings.ini")||!std::filesystem::exists(full/L"private"/L"custom.txt"))return 18;
  const auto empty=base/L"complete";Populate(empty);
  DeleteFileW((empty/L"settings.ini").c_str());DeleteFileW((empty/L"private"/L"custom.txt").c_str());RemoveDirectoryW((empty/L"private").c_str());
  if(yanvy::CleanupUserInstallation(empty,false)!=0||std::filesystem::exists(empty))return 17;
  const auto data=base/L"data-only";Populate(data);
  if(yanvy::CleanupUserInstallation(data,true)!=0||!std::filesystem::exists(data/L"YANVY-Server.exe")||std::filesystem::exists(data/L"data"/L"default.yaml"))return 4;
  const auto wrong=base/L"wrong-marker";Populate(wrong);Write(wrong/L"YANVY-INSTALL-MARKER.txt","YANVY fake\n");
  if(yanvy::CleanupUserInstallation(wrong,false)!=ERROR_INVALID_DATA||!std::filesystem::exists(wrong/L"YANVY-Server.exe"))return 5;
  const auto absent=base/L"no-marker";Populate(absent);DeleteFileW((absent/L"YANVY-INSTALL-MARKER.txt").c_str());
  if(yanvy::CleanupUserInstallation(absent,false)!=ERROR_FILE_NOT_FOUND||!std::filesystem::exists(absent/L"YANVY-Server.exe"))return 6;
  const auto locked=base/L"locked";Populate(locked);
  HANDLE lock=CreateFileW((locked/L"YANVY-Server.exe").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(lock==INVALID_HANDLE_VALUE)return 7;
  result=yanvy::CleanupUserInstallation(locked,false);CloseHandle(lock);
  if(result!=ERROR_SHARING_VIOLATION||!std::filesystem::exists(locked/L"YANVY-INSTALL-MARKER.txt"))return 8;
  if(yanvy::CleanupUserInstallation(locked,false)!=0||std::filesystem::exists(locked/L"YANVY-Server.exe"))return 9;
  const auto linked=base/L"reparse",outside=base/L"outside";Populate(linked);Write(outside/L"keep.txt","preserve\n");
  if(!Junction(linked/L"redirect",outside))return 10;
  if(yanvy::CleanupUserInstallation(linked,false)!=ERROR_ACCESS_DENIED||!std::filesystem::exists(linked/L"YANVY-Server.exe")||!std::filesystem::exists(outside/L"keep.txt"))return 11;
  if(!Junction(base/L"ancestor-link",linked))return 12;
  if(yanvy::CleanupUserInstallation(base/L"ancestor-link",false)!=ERROR_INVALID_NAME)return 13;
  const auto guard_root=base/L"held";std::filesystem::create_directory(guard_root);std::vector<yanvy::CleanupDirectory> guards;
  if(yanvy::GuardCleanupDirectory(guard_root,true,guards)!=0||MoveFileExW(guard_root.c_str(),(base/L"moved").c_str(),0))return 14;
  if(yanvy::CleanupUserInstallation(base/L"never-existed",false)!=0)return 15;
  std::cout<<"ordinary-user exact cleanup, data-only scope, unknown/settings preservation, malformed/missing marker, locked retry, junction and rename protection PASS\n";
  return 0;
}
