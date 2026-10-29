// SPDX-License-Identifier: GPL-3.0-or-later
// Uses a freshly generated, unregistered HKCU CLSID and removes only that key.
#define YANVY_SETUP_REGISTRY_FIXTURE
#include "../src/setup.cpp"

int wmain(int argc,wchar_t** argv) {
  if(argc>=4&&argc<=7&&std::wstring(argv[1])==L"--service-identities"){
    const HRESULT current=VerifyServiceIdentity(argv[2]);bool ok=SUCCEEDED(current);
    for(int index=3;index<argc;++index)ok=(VerifyServiceIdentity(argv[index])==CLASS_E_CLASSNOTAVAILABLE)&&ok;
    std::cout<<(ok?"current service accepted and legacy service refused before registry writes PASS\n":"service identity check FAIL\n");
    return ok?0:1;
  }
  if(argc!=1)return 2;
  GUID id{}; wchar_t name[64]{};
  if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id,name,64)) return 1;
  const std::wstring root=std::wstring(L"Software\\Classes\\CLSID\\")+name;
  std::wstring path=root+L"\\InprocServer32";
  auto read = [&](REGSAM view,std::wstring& output) {
    HKEY key=nullptr;wchar_t value[64]{};DWORD bytes=sizeof(value),type=0;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_READ|view,&key)!=ERROR_SUCCESS)return false;
    auto error=RegQueryValueExW(key,nullptr,nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes);
    RegCloseKey(key);if(error!=ERROR_SUCCESS||type!=REG_SZ)return false;output=value;return true;
  };
  for (REGSAM view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}) {
    HKEY existing=nullptr;auto error=RegOpenKeyExW(HKEY_CURRENT_USER,root.c_str(),0,KEY_READ|view,&existing);
    if(existing)RegCloseKey(existing);
    if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return 1;
  }
  struct Cleanup { const wchar_t* name;~Cleanup(){DeleteComClass(HKEY_CURRENT_USER,KEY_WOW64_32KEY,name);DeleteComClass(HKEY_CURRENT_USER,KEY_WOW64_64KEY,name);} } cleanup{name};
  std::wstring value;
  bool ok=SUCCEEDED(SetString(HKEY_CURRENT_USER,path,nullptr,L"x64",KEY_WOW64_64KEY)) &&
          SUCCEEDED(SetString(HKEY_CURRENT_USER,path,nullptr,L"x86",KEY_WOW64_32KEY)) &&
          read(KEY_WOW64_64KEY,value) && value==L"x64" &&
          read(KEY_WOW64_32KEY,value) && value==L"x86" &&
          SUCCEEDED(DeleteComClass(HKEY_CURRENT_USER,KEY_WOW64_32KEY,name)) &&
          !read(KEY_WOW64_32KEY,value) && read(KEY_WOW64_64KEY,value) && value==L"x64" &&
          SUCCEEDED(DeleteComClass(HKEY_CURRENT_USER,KEY_WOW64_64KEY,name)) &&
          !read(KEY_WOW64_64KEY,value);
  std::cout<<(ok ? "isolated 32/64 COM registry views PASS\n" : "isolated 32/64 COM registry views FAIL\n");
  return ok ? 0 : 1;
}
