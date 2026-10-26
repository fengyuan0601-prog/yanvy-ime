// SPDX-License-Identifier: BSD-3-Clause
#include "ime_core.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

static std::string Json(const std::string& s) {
  std::string out = "\"";
  const char hex[] = "0123456789abcdef";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += c;
  }
  return out + '"';
}

static void Print(const ime::Snapshot& s) {
  std::cout << "{\"handled\":" << (s.handled ? "true" : "false")
      << ",\"schema\":" << Json(s.schema) << ",\"input\":" << Json(s.input)
      << ",\"preedit\":" << Json(s.preedit) << ",\"commit\":" << Json(s.commit)
      << ",\"page\":" << s.page << ",\"candidates\":[";
  for (size_t i = 0; i < s.candidates.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << Json(s.candidates[i]);
  }
  std::cout << "]}" << std::endl;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: ime-core SHARED_DATA_DIR USER_DATA_DIR\n"
        << "Commands: schema wubi86|pinyin_simp; keys ASCII; key space|backspace|escape|enter|page_down; state; clear; quit\n"
        << "Diagnostic CLI only. This is not a registered system input method.\n";
    return 2;
  }
  try {
    ime::Engine engine(argv[1], argv[2]);
    std::cout << "{\"ready\":true,\"engine\":\"librime\",\"version\":"
        << Json(engine.Version()) << ",\"learning\":false}" << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line == "quit") break;
      try {
        if (line.rfind("schema ", 0) == 0) {
          engine.SelectSchema(line.substr(7)); Print(engine.State());
        } else if (line.rfind("keys ", 0) == 0) Print(engine.TypeAscii(line.substr(5)));
        else if (line == "state") Print(engine.State());
        else if (line == "clear") Print(engine.Clear());
        else if (line == "key space") Print(engine.Key(32));
        else if (line == "key backspace") Print(engine.Key(0xff08));
        else if (line == "key escape") Print(engine.Key(0xff1b));
        else if (line == "key enter") Print(engine.Key(0xff0d));
        else if (line == "key page_down") Print(engine.Key(0xff56));
        else if (line.size() == 5 && line.rfind("key ", 0) == 0)
          Print(engine.Key(static_cast<unsigned char>(line[4])));
        else throw std::runtime_error("Unknown command.");
      } catch (const std::exception& e) {
        std::cout << "{\"error\":" << Json(e.what()) << "}" << std::endl;
      }
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ime-core: " << e.what() << '\n';
    return 1;
  }
}
