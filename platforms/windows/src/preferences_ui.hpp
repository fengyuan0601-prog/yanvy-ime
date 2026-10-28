// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "appearance.hpp"
#include "version.hpp"
namespace yanvy { int PreferencesUI(HINSTANCE instance,int show); int ApplyInputScheme(const std::filesystem::path& install,const std::filesystem::path& user,uint32_t schema); }
#ifdef YANVY_PREFERENCES_FIXTURE
namespace yanvy { int PreferencesFixture(const std::filesystem::path& directory); }
#endif
