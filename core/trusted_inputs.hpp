// SPDX-License-Identifier: BSD-3-Clause
// Generated only by an explicit reviewed policy update; never from runtime data.
#pragma once
namespace ime::policy {
struct Input { const char* name; const char* sha256; };
inline constexpr Input kShared[] = {
  {"default.yaml", "0628ada16651d56c4cdfcb8e45ddba23d4d585cc8be0580a62c4e077c0bcc141"},
  {"double_pinyin.schema.yaml", "763430a99d0cd693805766b49d60b9092bc2af0ca26eb107f1d044566ce765d3"},
  {"double_pinyin_flypy.schema.yaml", "6b522a7e9cb743474287a14678597460bd124369f32573dd63e8f04e7c41d4b9"},
  {"double_pinyin_mspy.schema.yaml", "c72dc607000b39795ab267f80ec8727c6551015579b735a839f198422a75d7a2"},
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
  {"default.custom.yaml", "95c53b1a759e17adfd873c8664e355e0c3710987688504bba1836eff071b01f2"},
  {"double_pinyin.custom.yaml", "3a0f2264fcd3f0bb74ef3bd632b4000bba83a4267f6dc89b9cee5c854f489cff"},
  {"double_pinyin_flypy.custom.yaml", "3a0f2264fcd3f0bb74ef3bd632b4000bba83a4267f6dc89b9cee5c854f489cff"},
  {"double_pinyin_mspy.custom.yaml", "3a0f2264fcd3f0bb74ef3bd632b4000bba83a4267f6dc89b9cee5c854f489cff"},
  {"pinyin_simp.custom.yaml", "c0621284960a1178eea6ce83125259fbe7524517c3a2b702efc410238cb2d477"},
  {"stroke.custom.yaml", "ed90b1f45771de94ce6a8d130091adfe0dbc2cc29a46eab7395798229c7444b5"},
  {"wubi86.custom.yaml", "82262ad9ff05e2adac885d052aa044ed2266aedcafcd467179c920c479d7a952"},
};
}  // namespace ime::policy
