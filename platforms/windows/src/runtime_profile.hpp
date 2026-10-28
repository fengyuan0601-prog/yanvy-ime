// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include <stdexcept>

namespace yanvy {
inline void RejectReparseAncestors(std::filesystem::path path){
  for(;!path.empty();path=path.parent_path()){
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Refusing reparse point in protected data path.");
    if(path==path.parent_path())break;
  }
}
// Never overwrite an existing patch. Engine::VerifyProfile validates the
// copied files, rejects extra files and refuses altered no-learning policy.
inline void PrepareUserProfile(const std::filesystem::path& install,const std::filesystem::path& user){
  if(install.empty()||user.empty())throw std::runtime_error("User data directory unavailable.");
  const auto source=install/L"profile";RejectReparseAncestors(source);RejectReparseAncestors(user);
  std::error_code error;std::filesystem::create_directories(user,error);
  if(error)throw std::filesystem::filesystem_error("Create user profile",user,error);
  RejectReparseAncestors(user);
  std::vector<std::wstring> names{L"default.custom.yaml"};
  for(const char* id:ime::kDeployedSchemas)names.push_back(Wide(id)+L".custom.yaml");
  for(const auto& name:names){
    const auto src=source/name,dst=user/name;
    RejectReparseAncestors(src);RejectReparseAncestors(dst);
    if(!std::filesystem::is_regular_file(src))throw std::runtime_error("Required trusted data file is missing.");
    std::filesystem::copy_file(src,dst,std::filesystem::copy_options::skip_existing,error);
    if(error)throw std::filesystem::filesystem_error("Copy user profile",dst,error);
  }
}
}
