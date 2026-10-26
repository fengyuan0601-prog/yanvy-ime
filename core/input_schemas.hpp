// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace ime {
// Stable local wire IDs; never reorder existing entries when adding a layout.
inline constexpr const char* kSchemaIds[]={"wubi86","pinyin_simp","double_pinyin_flypy","double_pinyin","double_pinyin_mspy"};
inline constexpr const wchar_t* kSchemaLabels[]={L"五笔86",L"全拼",L"小鹤双拼",L"自然码双拼",L"微软双拼"};
inline constexpr const wchar_t* kSchemaShortLabels[]={L"五笔86",L"全拼",L"小鹤",L"自然码",L"微软"};
inline constexpr const wchar_t* kSchemaExamples[]={L"wqvb",L"nihao",L"nihc",L"nihk",L"nihk"};
inline constexpr const char* kDeployedSchemas[]={"wubi86","pinyin_simp","double_pinyin_flypy","double_pinyin","double_pinyin_mspy","stroke"};
inline constexpr uint32_t kSchemaCount=5;
static_assert(sizeof(kSchemaIds)/sizeof(kSchemaIds[0])==kSchemaCount);
static_assert(sizeof(kSchemaLabels)/sizeof(kSchemaLabels[0])==kSchemaCount);
static_assert(sizeof(kSchemaShortLabels)/sizeof(kSchemaShortLabels[0])==kSchemaCount);
static_assert(sizeof(kSchemaExamples)/sizeof(kSchemaExamples[0])==kSchemaCount);
inline bool ApprovedSchema(std::string_view id){for(auto allowed:kSchemaIds)if(id==allowed)return true;return false;}
inline bool IsDoublePinyin(std::string_view id){return id==kSchemaIds[2]||id==kSchemaIds[3]||id==kSchemaIds[4];}
}
