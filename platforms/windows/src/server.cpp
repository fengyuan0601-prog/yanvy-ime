// SPDX-License-Identifier: BSD-3-Clause
#include "common.hpp"
#include "ime_core.hpp"
#include "tray_icon.hpp"
#include "runtime_profile.hpp"
#include "version.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <shellapi.h>
#include <cwctype>

namespace {
template<size_t N> void Copy(wchar_t (&dest)[N], const std::string& source) {
  auto text = yanvy::Wide(source);
  const auto size = std::min(N - 1, text.size());
  std::copy_n(text.data(), size, dest);
  dest[size] = 0;
}
yanvy::Response Pack(const ime::Snapshot& state) {
  yanvy::Response r;
  r.ok = 1;
  r.handled = state.handled;
  r.page = state.page;
  r.highlighted = state.highlighted;
  r.last_page = state.last_page;
  r.cursor = uint32_t(yanvy::Wide(state.preedit.substr(0, std::min<size_t>(std::max(0, state.cursor), state.preedit.size()))).size());
  Copy(r.input, state.input);
  Copy(r.preedit, state.preedit);
  Copy(r.commit, state.commit);
  r.count = uint32_t(std::min<size_t>(10, state.candidates.size()));
  for (size_t i = 0; i < r.count; ++i) Copy(r.candidates[i], state.candidates[i]);
  return r;
}
// Serve older, ABI-compatible clients through the current broker. Each alias
// uses the same per-user/session ACL and forwards in memory only. A still
// running old broker keeps ownership of its pipe until it exits; no takeover.
struct LegacyBridge {
  unsigned version = 0;
  HANDLE stop = nullptr;
  SECURITY_ATTRIBUTES* security = nullptr;
  HANDLE thread = nullptr;
  static DWORD WINAPI Run(void* value) {
    auto& self = *static_cast<LegacyBridge*>(value);
    const auto name = yanvy::ObjectNameForVersion(L"\\\\.\\pipe\\",self.version);
    while (WaitForSingleObject(self.stop,0) != WAIT_OBJECT_0) {
      HANDLE pipe = CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
          PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,
          sizeof(yanvy::Response),sizeof(yanvy::Request),500,self.security);
      if (pipe == INVALID_HANDLE_VALUE) { if (WaitForSingleObject(self.stop,500)==WAIT_OBJECT_0) break; continue; }
      OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
      if (!ov.hEvent) { CloseHandle(pipe); if (WaitForSingleObject(self.stop,500)==WAIT_OBJECT_0) break; continue; }
      BOOL connected=ConnectNamedPipe(pipe,&ov);const DWORD error=GetLastError();
      if (!connected&&error==ERROR_PIPE_CONNECTED) connected=TRUE;
      if (!connected&&error==ERROR_IO_PENDING) {
        HANDLE waiters[]={self.stop,ov.hEvent};
        connected=WaitForMultipleObjects(2,waiters,FALSE,INFINITE)==WAIT_OBJECT_0+1;
        if (!connected) { CancelIoEx(pipe,&ov);DWORD ignored=0;GetOverlappedResult(pipe,&ov,&ignored,TRUE); }
      }
      CloseHandle(ov.hEvent);
      if (connected) {
        yanvy::Request request;yanvy::Response response;
        if (yanvy::Transfer(pipe,&request,sizeof(request),false,1000) &&
            request.magic==yanvy::kMagic && request.version==yanvy::kProtocol && request.schema<ime::kSchemaCount) {
          yanvy::Call(request,response);
          if (yanvy::Transfer(pipe,&response,sizeof(response),true)) {
            BYTE ack=0;yanvy::Transfer(pipe,&ack,sizeof(ack),false);
          }
        }
      }
      DisconnectNamedPipe(pipe);CloseHandle(pipe);
    }
    return 0;
  }
};
struct LegacyBridges {
  HANDLE stop;
  std::array<LegacyBridge,5> items{};
  bool Start(SECURITY_ATTRIBUTES* security) {
    for (size_t i=0;i<items.size();++i) {
      items[i].version=unsigned(i+2);items[i].stop=stop;items[i].security=security;
      items[i].thread=CreateThread(nullptr,0,LegacyBridge::Run,&items[i],0,nullptr);
      if (!items[i].thread) return false;
    }
    return true;
  }
  ~LegacyBridges() {
    SetEvent(stop);
    for (auto& item:items) if (item.thread) { WaitForSingleObject(item.thread,INFINITE);CloseHandle(item.thread); }
  }
};
int Serve(const std::filesystem::path& test_profile = {}) {
  const auto sid = yanvy::UserSid();
  if (sid.empty()) return 2;
  PSECURITY_DESCRIPTOR sd = nullptr;
  // Local user only, remote clients rejected. The low integrity label permits
  // the same user's sandboxed desktop renderer to connect; no network socket.
  const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + sid + L")S:(ML;;NW;;;LW)";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return 3;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  HANDLE singleton = CreateMutexW(&sa, TRUE, yanvy::ObjectName(L"Local\\Broker-").c_str());
  DWORD mutex_error = GetLastError();
  if (!singleton || mutex_error == ERROR_ALREADY_EXISTS) {
    if (singleton) CloseHandle(singleton);
    LocalFree(sd);
    return 0;
  }
  HANDLE stop = CreateEventW(&sa, TRUE, FALSE, yanvy::ObjectName(L"Local\\Stop-").c_str());
  if (!stop) { ReleaseMutex(singleton);CloseHandle(singleton);LocalFree(sd);return 3; }
  int result = 0;
  try {
    const auto root = yanvy::ModuleDir();
    auto user=yanvy::UserDataRoot();
    auto profile=test_profile.empty()?user/yanvy::kVersion/L"profile":test_profile;
    if(test_profile.empty()&&user.empty())throw std::runtime_error("User data directory unavailable.");
    auto lock_name=std::wstring(L"Global\\YANVY.Profile.")+sid;
    HANDLE profile_lock=CreateMutexW(nullptr,FALSE,lock_name.c_str());
    if(!profile_lock)throw std::runtime_error("Cannot lock user profile.");
    DWORD wait=WaitForSingleObject(profile_lock,30000);
    if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(profile_lock);throw std::runtime_error("Cannot lock user profile.");}
    struct LockGuard{HANDLE handle;~LockGuard(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}} guard{profile_lock};
    yanvy::PrepareUserProfile(root,profile);
    ime::Engine engine((root / L"data").u8string(), profile.u8string());
    ReleaseMutex(profile_lock);CloseHandle(profile_lock);guard.handle=nullptr;
    // Isolated protocol fixtures must not create an extra user-facing icon.
    std::unique_ptr<yanvy::TrayIcon> tray;
    if (test_profile.empty()) tray = std::make_unique<yanvy::TrayIcon>();
    LegacyBridges bridges{stop};
    if (!bridges.Start(&sa)) { SetEvent(stop);result=5; }
    GUID owner{};
    std::string schema = "wubi86";
    while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
      HANDLE pipe = CreateNamedPipeW(yanvy::PipeName().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
          sizeof(yanvy::Response), sizeof(yanvy::Request), 500, &sa);
      if (pipe == INVALID_HANDLE_VALUE) { result = 4; break; }
      OVERLAPPED ov{};
      ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      BOOL connected = ConnectNamedPipe(pipe, &ov);
      const DWORD error = GetLastError();
      if (!connected && error == ERROR_PIPE_CONNECTED) connected = TRUE;
      if (!connected && error == ERROR_IO_PENDING) {
        HANDLE handles[] = {stop, ov.hEvent};
        connected = WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1;
      }
      if (!connected && error == ERROR_IO_PENDING) {
        CancelIoEx(pipe, &ov);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &ov, &ignored, TRUE);
      }
      CloseHandle(ov.hEvent);
      if (connected) {
        yanvy::Request q;
        yanvy::Response r;
        if (yanvy::Transfer(pipe, &q, sizeof(q), false, 1000) &&
            q.magic == yanvy::kMagic && q.version == yanvy::kProtocol && q.schema < ime::kSchemaCount) {
          try {
            const bool owns = IsEqualGUID(q.owner, owner);
            if (q.op == yanvy::Op::Focus) {
              if (!owns) engine.Clear();
              owner = q.owner;
              const std::string wanted = ime::kSchemaIds[q.schema];
              if (schema != wanted) { engine.SelectSchema(wanted); schema = wanted; }
              r = Pack(engine.State());
            } else if (q.op == yanvy::Op::Clear) {
              r = Pack(owns ? engine.Clear() : ime::Snapshot{});
            } else if (owns && q.op == yanvy::Op::State) {
              r = Pack(engine.State());
            } else if (owns && q.op == yanvy::Op::Key && q.key <= 0xffff && !(q.modifiers & ~0x400000ffu)) {
              auto state = engine.Key(int(q.key), int(q.modifiers));
              if (state.input.size() > 1023 || state.preedit.size() > 2047 || state.commit.size() > 8191) engine.Clear();
              else r = Pack(state);
            }
          } catch (...) {
            engine.Clear();
            // Only the failure category is returned; no input or exception log.
          }
          yanvy::Transfer(pipe, &r, sizeof(r), true);
          BYTE ack = 0;
          yanvy::Transfer(pipe, &ack, sizeof(ack), false);
        }
      }
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
    }
  } catch (...) { result = 5; }
  if (stop) CloseHandle(stop);
  ReleaseMutex(singleton);
  CloseHandle(singleton);
  LocalFree(sd);
  return result;
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const std::wstring option = argc > 1 ? argv[1] : L"--serve";
  std::filesystem::path test_profile;
#ifdef YANVY_BROKER_FIXTURE
  if(option==L"--serve-test"&&argc==4){test_profile=argv[2];yanvy::test_object_suffix=argv[3];}
  if(option==L"--stop-test"&&argc==3)yanvy::test_object_suffix=argv[2];
  if(option==L"--serve-test"||option==L"--stop-test") {
    const auto& suffix=yanvy::test_object_suffix;
    if (suffix.size()!=32 || !std::all_of(suffix.begin(),suffix.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})) {
      LocalFree(argv);return 1;
    }
  }
#endif
  LocalFree(argv);
  if (option == L"--stop"
#ifdef YANVY_BROKER_FIXTURE
      || (option == L"--stop-test" && !yanvy::test_object_suffix.empty())
#endif
  ) {
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, yanvy::ObjectName(L"Local\\Stop-").c_str());
    if (stop) { SetEvent(stop); CloseHandle(stop); }
    return 0;
  }
  if (option == L"--check") {
    try {
      auto root = yanvy::ModuleDir();
      ime::Engine engine((root / L"data").u8string(), (root / L"profile").u8string());
      return engine.TypeAscii("wqvb").candidates.empty() ? 6 : 0;
    } catch (const std::filesystem::filesystem_error&) { return 7; }
    catch (const std::exception& error) {
      // Return categories only: never write paths or input to diagnostics.
      const std::string reason=error.what();
      if(reason=="Refusing reparse point in protected data path."||reason=="Refusing symlink in protected data path.")return 8;
      if(reason=="Refusing profile: trusted source or no-learning patch was modified."||
         reason=="Required trusted data file is missing."||reason=="Refusing unexpected shared data, configuration, or script."||
         reason=="Refusing unexpected user schema, script, or saved dictionary.")return 9;
      if(reason=="Missing default configuration."||reason=="Required schema did not deploy."||
         reason=="Refusing schema: approved components or no-learning policy differ."||
         reason=="Refusing default configuration: approved schema list or startup policy differ.")return 10;
      return 5;
    } catch (...) { return 5; }
  }
  if (option == L"--tray-smoke") { yanvy::TrayIcon tray; return tray.Registered()&&tray.Version4()&&tray.RemoveForTest()?0:7; }
#ifdef YANVY_BROKER_FIXTURE
  if(option==L"--serve-test"&&!test_profile.empty())return Serve(test_profile);
#endif
  return option == L"--serve" ? Serve() : 1;
}
