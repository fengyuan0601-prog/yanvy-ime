// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace yanvy {
inline constexpr DWORD kDataReused = 12;
inline constexpr DWORD kPackageMismatch = 42;
enum class PackageDestination { empty, reusable, rejected };
struct PackageFile { bool data;std::filesystem::path relative;std::array<unsigned char,32> digest; };
using PackageManifest = std::vector<PackageFile>;
inline bool SafePackageRelative(const std::wstring& text) {
  if(text.empty()||text.size()>240||text.find_first_of(L"\\:\"*?<>|\r\n\t")!=std::wstring::npos)return false;
  if(text.front()==L'/'||text.back()==L'/')return false;
  size_t start=0;
  while(start<text.size()){
    const auto end=text.find(L'/',start);const auto part=text.substr(start,end==std::wstring::npos?end:end-start);
    if(part.empty()||part==L"."||part==L".."||part.back()==L'.'||part.back()==L' ')return false;
    for(wchar_t c:part)if(c<32)return false;
    if(end==std::wstring::npos)break;
    start=end+1;
  }
  return true;
}
inline bool ParsePackageManifest(std::string_view bytes,PackageManifest& manifest) {
  manifest.clear();constexpr std::string_view magic="YANVY-PACKAGE-MANIFEST 1";
  const auto first=bytes.find('\n');if(bytes.size()>1024*1024||first==std::string_view::npos)return false;
  auto header=bytes.substr(0,first);if(!header.empty()&&header.back()=='\r')header.remove_suffix(1);
  if(header!=magic)return false;
  bytes.remove_prefix(first+1);std::set<std::wstring> names;
  bool marker=false,info=false,runtime=false,data=false,profile=false;
  while(!bytes.empty()){
    const auto end=bytes.find('\n');if(end==std::string_view::npos)return false;
    auto line=bytes.substr(0,end);bytes.remove_prefix(end+1);
    if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
    if(line.size()<68||line[1]!='\t'||line[66]!='\t'||(line[0]!='D'&&line[0]!='R'))return false;
    PackageFile file{};file.data=line[0]=='D';
    const auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<32;++i){int hi=nibble(line[2+i*2]),lo=nibble(line[3+i*2]);if(hi<0||lo<0)return false;file.digest[i]=static_cast<unsigned char>(hi*16+lo);}
    const auto path=line.substr(67);if(path.size()>960)return false;
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),nullptr,0);if(count<=0)return false;
    std::wstring wide(size_t(count),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),wide.data(),count)!=count||!SafePackageRelative(wide))return false;
    auto lower=wide;CharLowerBuffW(lower.data(),DWORD(lower.size()));if(!names.insert(lower).second)return false;
    file.relative=std::filesystem::path(wide);file.relative.make_preferred();
    if(file.data){
      const bool input=(lower.rfind(L"data/",0)==0||lower.rfind(L"profile/",0)==0)&&lower.find(L'/',lower.find(L'/')+1)==std::wstring::npos&&file.relative.extension()==L".yaml";
      const bool license=lower.rfind(L"licenses/",0)==0;
      const bool root=lower==L"testing.txt"||lower==L"build-info.json"||lower==L"yanvy-install-marker.txt";
      const auto extension=std::filesystem::path(lower).extension().wstring();
      if(!(input||license||root)||extension==L".exe"||extension==L".dll"||extension==L".com"||extension==L".bat"||extension==L".cmd"||extension==L".ps1"||extension==L".vbs"||extension==L".js")return false;
      marker|=lower==L"yanvy-install-marker.txt";info|=lower==L"build-info.json";
      data|=lower.rfind(L"data/",0)==0;profile|=lower.rfind(L"profile/",0)==0;
    }else runtime=true;
    manifest.push_back(std::move(file));if(manifest.size()>4096)return false;
  }
  return marker&&info&&runtime&&data&&profile;
}
inline bool LoadPackageManifest(HINSTANCE instance,PackageManifest& manifest) {
  auto resource=FindResourceW(instance,MAKEINTRESOURCEW(104),RT_RCDATA);if(!resource)return false;
  auto loaded=LoadResource(instance,resource);const auto data=static_cast<const char*>(loaded?LockResource(loaded):nullptr);
  const DWORD size=SizeofResource(instance,resource);
  return data&&size&&ParsePackageManifest(std::string_view(data,size),manifest);
}
inline bool PackagePathWithoutReparse(const std::filesystem::path& path) {
  for(auto part=path;!part.empty();part=part.parent_path()){
    DWORD attributes=GetFileAttributesW(part.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return false;}
    else if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return false;
    if(part==part.parent_path())break;
  }
  return true;
}
inline bool PackageFileDigest(const std::filesystem::path& path,std::array<unsigned char,32>& digest) {
  if(!PackagePathWithoutReparse(path))return false;
  HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  struct CloseFile{HANDLE value;~CloseFile(){CloseHandle(value);}} close{file};
  BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(file,&info)||info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return false;
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  struct CloseHash{BCRYPT_ALG_HANDLE& algorithm;BCRYPT_HASH_HANDLE& hash;~CloseHash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} cleanup{algorithm,hash};
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)return false;
  std::array<unsigned char,65536> buffer{};DWORD count=0;
  do{if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&count,nullptr))return false;if(count&&BCryptHashData(hash,buffer.data(),count,0)<0)return false;}while(count);
  return BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0;
}
inline bool PackageDataInputsExact(const std::filesystem::path& root,const PackageManifest& manifest) {
  // The ordinary-user payload has no generated caches. Extra input files or
  // directories would violate the core's fixed schema/no-learning policy.
  for(const auto* directory:{L"data",L"profile"}){
    const auto folder=root/directory;if(!PackagePathWithoutReparse(folder))return false;
    std::error_code error;std::filesystem::directory_iterator it(folder,error),end;if(error)return false;
    for(;it!=end;it.increment(error)){
      if(error)return false;
      const auto relative=std::filesystem::path(directory)/it->path().filename();bool expected=false;
      for(const auto& file:manifest)if(file.data&&_wcsicmp(file.relative.c_str(),relative.c_str())==0){expected=true;break;}
      if(!expected)return false;
    }
    if(error)return false;
  }
  return true;
}
inline PackageDestination InspectPackageDestination(const std::filesystem::path& root,const PackageManifest& manifest,bool data_only) {
  if(manifest.empty()||!root.is_absolute()||root.lexically_normal()!=root||!PackagePathWithoutReparse(root))return PackageDestination::rejected;
  std::error_code error;const bool exists=std::filesystem::exists(root,error);if(error)return PackageDestination::rejected;
  if(!exists)return PackageDestination::empty;
  if(!std::filesystem::is_directory(root,error)||error)return PackageDestination::rejected;
  if(std::filesystem::is_empty(root,error)&&!error)return PackageDestination::empty;
  if(error)return PackageDestination::rejected;
  for(const auto& file:manifest){
    if(data_only&&!file.data)continue;
    std::array<unsigned char,32> digest{};
    if(!PackageFileDigest(root/file.relative,digest)||digest!=file.digest)return PackageDestination::rejected;
  }
  if(data_only&&!PackageDataInputsExact(root,manifest))return PackageDestination::rejected;
  return PackageDestination::reusable;
}
inline bool PreparedDataOwnedByAttempt(DWORD result){return result==ERROR_SUCCESS;}
inline bool PreparedDataAccepted(DWORD result){return result==ERROR_SUCCESS||result==kDataReused;}
inline bool MayCleanupPreviousInstallation(bool uninstall,bool configuration_failed,bool broker_checked){return uninstall||(!configuration_failed&&broker_checked);}
}
