// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "appearance.hpp"
#include "version.hpp"
namespace yanvy { int PreferencesUI(HINSTANCE instance,int show); }
#ifdef YANVY_PREFERENCES_FIXTURE
namespace yanvy { int PreferencesFixture(const std::filesystem::path& directory); }
#endif
