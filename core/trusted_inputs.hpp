// SPDX-License-Identifier: BSD-3-Clause
// Generated only by an explicit reviewed policy update; never from runtime data.
#pragma once
namespace ime::policy {
struct Input { const char* name; const char* sha256; };
inline constexpr Input kShared[] = {
  {"default.yaml", "0628ada16651d56c4cdfcb8e45ddba23d4d585cc8be0580a62c4e077c0bcc141"},
  {"key_bindings.yaml", "ee2476598b4cae89068b823868196eb07b5f72007a600814fe5804b39008b3ed"},
  {"pinyin_simp.dict.yaml", "e341598343a0f0f2035bb1aafc34a7f3bb7887deeecb3f60796262aaa2983e6b"},
  {"pinyin_simp.schema.yaml", "726772190a325ae4bf236915e7d62a23fedd8cf1da8a74370a20f87196c67775"},
  {"punctuation.yaml", "d6f89592149098f0a2d0ed3e5c83f49adb6226cdf1d967b0bc0d293f4e5c496a"},
  {"stroke.dict.yaml", "b3e93dce89c185f45c3d6e189b86b3a8626913352cc85e1094c786579a665791"},
  {"stroke.schema.yaml", "06f680cf7c9d9a69948681ee25e9f6aeccfde4d8d590f93e179d4c4b3caee879"},
  {"symbols.yaml", "6d08969525bd66c67abf1791c99928079356bd0e673386c49c1959d2a52fdbba"},
  {"wubi86.dict.yaml", "f833d86b72341fe82e069a425b6625f29ef85f1bc0f34f6fb7975fe514888b5a"},
  {"wubi86.schema.yaml", "cdb5aac1a9aa071552d5fdffdfe5a6618b429b19358b8b1e003130e835d5a166"},
};
inline constexpr Input kUser[] = {
  {"default.custom.yaml", "9739f1280b4dbee86ded823266005abbd83cde69d27bd35de70f3707407a719f"},
  {"pinyin_simp.custom.yaml", "c0621284960a1178eea6ce83125259fbe7524517c3a2b702efc410238cb2d477"},
  {"stroke.custom.yaml", "ed90b1f45771de94ce6a8d130091adfe0dbc2cc29a46eab7395798229c7444b5"},
  {"wubi86.custom.yaml", "82262ad9ff05e2adac885d052aa044ed2266aedcafcd467179c920c479d7a952"},
};
}  // namespace ime::policy
