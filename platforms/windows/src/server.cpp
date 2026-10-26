// SPDX-License-Identifier: BSD-3-Clause
#include "common.hpp"
#include "ime_core.hpp"
#include <algorithm>
#include <memory>
#include <shellapi.h>

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
int Serve() {
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
  int result = 0;
  try {
    const auto root = yanvy::ModuleDir();
    ime::Engine engine((root / L"data").u8string(), (root / L"profile").u8string());
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
  LocalFree(argv);
  if (option == L"--stop") {
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, yanvy::ObjectName(L"Local\\Stop-").c_str());
    if (stop) { SetEvent(stop); CloseHandle(stop); }
    return 0;
  }
  if (option == L"--check") {
    try {
      auto root = yanvy::ModuleDir();
      ime::Engine engine((root / L"data").u8string(), (root / L"profile").u8string());
      return engine.TypeAscii("wqvb").candidates.empty() ? 6 : 0;
    } catch (...) { return 5; }
  }
  return option == L"--serve" ? Serve() : 1;
}
