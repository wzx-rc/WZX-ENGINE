// UI Director ("INTERFACE"): visual editing of the editor UI from inside the running engine.
//
// F9 toggles the edit mode. In edit mode an overlay sits over the whole window:
//   - hover shows any Dear ImGui item (every widget is seen through the ImGui test-engine hooks, so nothing has to be
//     annotated to be pickable), click selects it;
//   - drag moves the selected item, Shift+drag resizes it, arrows nudge it (Shift = 10 px), for the items whose code
//     goes through uidir::place() (the Atelier buttons, tabs and hit zones); their label can be renamed too;
//   - "Désigner" pins the item with an instruction for Claude.
// Everything is written to the project, next to the sources:
//   ui/layout.json         layout overrides (offset / size / text per ImGui ID), read at start and hot-reloaded when
//                          another tool (Claude) changes it; this file is the source of truth of the edited layout
//   ui/designations.json   designated items
//   ui/DESIGNATIONS.md     the same, written for Claude (ID, label to grep in src/, window, rectangle, instruction)
// The command API (POST /cmd) exposes it too: ui.items, ui.layout.get, ui.layout.set, ui.designations, ui.designate,
// ui.edit (see GameHost::command).
#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <string>

namespace app { struct Json; }

namespace uidir {

void init(const std::wstring& projectRoot);   // loads ui/layout.json and ui/designations.json
bool active();
void setActive(bool on);

// Applies the layout override of item `id` (an ImGui ID in the current ID stack) to `r` (screen pixels).
// `scale` converts the reference pixels of the layout file to screen pixels. When `text` is given and the layout renames
// the item, *text is replaced. Calling it twice for the same item in a frame (nested helpers) applies it once.
ImRect place(ImGuiID id, const ImRect& r, float scale, const char** text = nullptr);
// A custom-drawn region that is not an ImGui item (panel frames...): pickable and designatable, not movable.
void declare(ImGuiID id, const ImRect& r, const char* label);

void beginFrame();   // after ImGui::NewFrame()
void endFrame();     // after the whole UI, before ImGui::Render(): F9, overlay, inspector, hot reload

// POST /cmd {cmd: "ui.*"}: returns the JSON response, or an empty string when `cmd` is not a ui command.
std::string command(const app::Json& j, int& status);

} // namespace uidir
