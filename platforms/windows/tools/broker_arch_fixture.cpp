// SPDX-License-Identifier: BSD-3-Clause
// Sends only synthetic input to a UUID-isolated test broker.
#include "common.hpp"
#include <algorithm>
#include <iostream>

int wmain(int argc,wchar_t** argv) {
  if(argc!=2)return 1;
  yanvy::test_object_suffix=argv[1];
  const auto& suffix=yanvy::test_object_suffix;
  if(suffix.size()!=32 || !std::all_of(suffix.begin(),suffix.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))return 1;
  yanvy::Request request;yanvy::Response response;
  if(FAILED(CoCreateGuid(&request.owner)))return 1;
  request.op=yanvy::Op::Focus;request.schema=1;
  if(!yanvy::Call(request,response))return 1;
  for(wchar_t key:std::wstring(L"nihao")) {
    request.op=yanvy::Op::Key;request.key=key;
    if(!yanvy::Call(request,response))return 1;
  }
  if(response.count==0||std::wstring(response.candidates[0])!=L"你好")return 1;
  request.key=' ';
  if(!yanvy::Call(request,response)||std::wstring(response.commit)!=L"你好")return 1;
  std::cout<<"cross-architecture real librime IPC PASS\n";
  return 0;
}
