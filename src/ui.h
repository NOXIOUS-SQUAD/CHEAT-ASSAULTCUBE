#pragma once

#include <imgui.h>
#include "memory.h"

struct UiState
{
    bool open = true;
    int tab = 0;
    int ammoValue = 20;
    bool ammoLock = true;
};

namespace ui
{
void LoadFonts(float dpiScale);
void ApplyStyle();
void Draw(UiState& state, MemoryWriter& memory);
ImVec2 MenuPosition();
ImVec2 MenuSize();
}
