// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <cstdint>

namespace yanvy {
constexpr uint32_t kMagic = 0x59564e59, kProtocol = 2;
enum class Op : uint32_t { Focus = 1, Key, Clear, State, OpenSettings };
// Fixed-width, pointer-free local IPC; identical in 32- and 64-bit clients.
struct Request {
  uint32_t magic = kMagic, version = kProtocol;
  Op op = Op::State;
  uint32_t key = 0, modifiers = 0, schema = 0;
  GUID owner{};
};
struct Response {
  // OpenSettings uses handled for HRESULT, ok for a recognized action reply;
  // every remaining field is zero. Other operations retain their input ABI.
  uint32_t magic = kMagic, version = kProtocol, ok = 0, handled = 0;
  uint32_t page = 0, highlighted = 0, count = 0, last_page = 1, cursor = 0;
  wchar_t input[1024]{}, preedit[1024]{}, commit[4096]{};
  wchar_t candidates[10][256]{};
};
static_assert(sizeof(Request) == 40);
static_assert(sizeof(Response) == 17444);
}
