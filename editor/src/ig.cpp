// ig.cpp — Dear ImGui → Lua binding (tw.ig.*).
//
// Deliberately a small, flat surface: windows/children, widgets, tables,
// popups, draw list, style, input state. Everything Lua-side UI needs;
// nothing more. Widget return convention: `changed, value(s)` (changed is
// false when the user didn't touch it this frame). Draw lists are
// lightuserdata handles into a per-frame registry.
#include "editor.h"

#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>  // ImGui::IsNamedKey()
#include <imgui_stdlib.h>

#include "fa6/IconsFontAwesome6.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// ── helpers ─────────────────────────────────────────────────────────────────

static ImVec2 check_v2(lua_State* L, int i) {
  return ImVec2((float)luaL_checknumber(L, i), (float)luaL_checknumber(L, i + 1));
}
static void push_v2(lua_State* L, ImVec2 v) {
  lua_pushnumber(L, v.x);
  lua_pushnumber(L, v.y);
}
static ImVec4 check_v4(lua_State* L, int i) {
  return ImVec4((float)luaL_checknumber(L, i), (float)luaL_checknumber(L, i + 1),
                (float)luaL_checknumber(L, i + 2),
                (float)luaL_checknumber(L, i + 3));
}
static void push_v4(lua_State* L, ImVec4 v) {
  lua_pushnumber(L, v.x);
  lua_pushnumber(L, v.y);
  lua_pushnumber(L, v.z);
  lua_pushnumber(L, v.w);
}
static const char* opt_str(lua_State* L, int i, const char* def) {
  return lua_isnoneornil(L, i) ? def : luaL_checkstring(L, i);
}
static bool opt_bool(lua_State* L, int i, bool def) {
  return lua_isnoneornil(L, i) ? def : lua_toboolean(L, i);
}
static ImU32 col32(lua_State* L, int i) {
  float r = (float)luaL_checknumber(L, i);
  float g = (float)luaL_checknumber(L, i + 1);
  float b = (float)luaL_checknumber(L, i + 2);
  float a = luaL_optnumber(L, i + 3, 1);
  return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}

// ── Begin/End balance tracker (safety net) ──────────────────────────────────
// Tracks depth of each Begin/End category. At frame end, ig_balance_check()
// force-closes any unbalanced pairs with a warning instead of crashing.
static struct {
  int window, child, popup, menu, menu_bar, table, tab_bar, tab_item;
  int list_box, tree, tooltip, group, disabled;
} g_bal = {};

void ig_balance_check() {
  auto warn_close = [](const char* name, int& depth, void(*closer)()) {
    while (depth > 0) {
      fprintf(stderr, "[ig] WARNING: force-closing unbalanced %s (%d remain)\n",
              name, depth);
      closer();
      depth--;
    }
  };
  // Close innermost first, outermost last
  warn_close("EndTooltip",  g_bal.tooltip,  ImGui::EndTooltip);
  warn_close("EndDisabled", g_bal.disabled, ImGui::EndDisabled);
  warn_close("TreePop",     g_bal.tree,     ImGui::TreePop);
  warn_close("EndListBox",  g_bal.list_box, ImGui::EndListBox);
  warn_close("EndTabItem",  g_bal.tab_item, ImGui::EndTabItem);
  warn_close("EndTabBar",   g_bal.tab_bar,  ImGui::EndTabBar);
  warn_close("EndTable",    g_bal.table,    ImGui::EndTable);
  warn_close("EndMenu",     g_bal.menu,     ImGui::EndMenu);
  warn_close("EndMenuBar",  g_bal.menu_bar, ImGui::EndMenuBar);
  warn_close("EndPopup",    g_bal.popup,    ImGui::EndPopup);
  warn_close("EndGroup",    g_bal.group,    ImGui::EndGroup);
  warn_close("EndChild",    g_bal.child,    ImGui::EndChild);
  warn_close("End",         g_bal.window,   ImGui::End);
}

// ── Scoped wrappers ────────────────────────────────────────────────────────
// ig.child("name", w, h, function() ... end)  — safe: EndChild always called
// ig.child("name", w, h, cflags, wflags, function() ... end)  — with flags
//
// "Always-End" pattern: End called regardless of Begin's return value.
// Body only executes when Begin returns true (content is visible).
// Errors in the body are caught, End is called, then re-raised.

static int l_scoped_window(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int fn_idx;
  ImGuiWindowFlags flags = 0;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { flags = (ImGuiWindowFlags)luaL_optinteger(L, 2, 0); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  g_bal.window++;
  bool open = ImGui::Begin(name, nullptr, flags);
  int err = 0;
  if (open) {
    lua_pushvalue(L, fn_idx);
    err = lua_pcall(L, 0, 0, 0);
  }
  ImGui::End();
  g_bal.window--;
  if (err) return lua_error(L);
  return 0;
}

static int l_scoped_child(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  float w = (float)luaL_optnumber(L, 2, 0);
  float h = (float)luaL_optnumber(L, 3, 0);
  int fn_idx;
  ImGuiChildFlags cf = 0;
  ImGuiWindowFlags wf = 0;
  if (lua_isfunction(L, 4)) { fn_idx = 4; }
  else if (lua_isfunction(L, 5)) { cf = (ImGuiChildFlags)luaL_optinteger(L, 4, 0); fn_idx = 5; }
  else { cf = (ImGuiChildFlags)luaL_optinteger(L, 4, 0); wf = (ImGuiWindowFlags)luaL_optinteger(L, 5, 0); fn_idx = 6; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  g_bal.child++;
  bool open = ImGui::BeginChild(name, ImVec2(w, h), cf, wf);
  int err = 0;
  if (open) {
    lua_pushvalue(L, fn_idx);
    err = lua_pcall(L, 0, 0, 0);
  }
  ImGui::EndChild();
  g_bal.child--;
  if (err) return lua_error(L);
  return 0;
}

// "Conditional-End" pattern: body + End only called when Begin returns true.

static int l_scoped_popup(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int fn_idx = lua_isfunction(L, 2) ? 2 : 3;
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginPopup(name)) {
    g_bal.popup++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndPopup();
    g_bal.popup--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_popup_modal(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int fn_idx;
  ImGuiWindowFlags flags = 0;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { flags = (ImGuiWindowFlags)luaL_optinteger(L, 2, 0); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginPopupModal(name, nullptr, flags)) {
    g_bal.popup++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndPopup();
    g_bal.popup--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_popup_context_window(lua_State* L) {
  const char* id = lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1);
  int fn_idx;
  ImGuiPopupFlags flags = 1;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { flags = (ImGuiPopupFlags)luaL_optinteger(L, 2, 1); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginPopupContextWindow(id, flags)) {
    g_bal.popup++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndPopup();
    g_bal.popup--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_popup_context_item(lua_State* L) {
  const char* id = lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1);
  int fn_idx;
  ImGuiPopupFlags flags = 1;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { flags = (ImGuiPopupFlags)luaL_optinteger(L, 2, 1); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginPopupContextItem(id, flags)) {
    g_bal.popup++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndPopup();
    g_bal.popup--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_menu(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int fn_idx;
  bool enabled = true;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { enabled = lua_toboolean(L, 2); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginMenu(name, enabled)) {
    g_bal.menu++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndMenu();
    g_bal.menu--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_menu_bar(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  if (ImGui::BeginMenuBar()) {
    g_bal.menu_bar++;
    lua_pushvalue(L, 1);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndMenuBar();
    g_bal.menu_bar--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_table(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int cols = (int)luaL_checkinteger(L, 2);
  int fn_idx;
  ImGuiTableFlags flags = 0;
  if (lua_isfunction(L, 3)) { fn_idx = 3; }
  else { flags = (ImGuiTableFlags)luaL_optinteger(L, 3, 0); fn_idx = 4; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginTable(name, cols, flags)) {
    g_bal.table++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndTable();
    g_bal.table--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_tab_bar(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int fn_idx;
  ImGuiTabBarFlags flags = 0;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { flags = (ImGuiTabBarFlags)luaL_optinteger(L, 2, 0); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginTabBar(name, flags)) {
    g_bal.tab_bar++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndTabBar();
    g_bal.tab_bar--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_tab_item(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int fn_idx;
  ImGuiTabItemFlags flags = 0;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { flags = (ImGuiTabItemFlags)luaL_optinteger(L, 2, 0); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginTabItem(name, nullptr, flags)) {
    g_bal.tab_item++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndTabItem();
    g_bal.tab_item--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_list_box(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  float w = (float)luaL_optnumber(L, 2, 0);
  float h = (float)luaL_optnumber(L, 3, 0);
  int fn_idx = 4;
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::BeginListBox(name, ImVec2(w, h))) {
    g_bal.list_box++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::EndListBox();
    g_bal.list_box--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_tree_node(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  int fn_idx;
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
  if (lua_isfunction(L, 2)) { fn_idx = 2; }
  else { flags = (ImGuiTreeNodeFlags)luaL_optinteger(L, 2, ImGuiTreeNodeFlags_DefaultOpen); fn_idx = 3; }
  luaL_checktype(L, fn_idx, LUA_TFUNCTION);
  if (ImGui::TreeNodeEx(name, flags)) {
    g_bal.tree++;
    lua_pushvalue(L, fn_idx);
    int err = lua_pcall(L, 0, 0, 0);
    ImGui::TreePop();
    g_bal.tree--;
    if (err) return lua_error(L);
  }
  return 0;
}

static int l_scoped_tooltip(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  g_bal.tooltip++;
  ImGui::BeginTooltip();
  lua_pushvalue(L, 1);
  int err = lua_pcall(L, 0, 0, 0);
  ImGui::EndTooltip();
  g_bal.tooltip--;
  if (err) return lua_error(L);
  return 0;
}

static int l_scoped_group(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  g_bal.group++;
  ImGui::BeginGroup();
  lua_pushvalue(L, 1);
  int err = lua_pcall(L, 0, 0, 0);
  ImGui::EndGroup();
  g_bal.group--;
  if (err) return lua_error(L);
  return 0;
}

static int l_scoped_disabled(lua_State* L) {
  bool cond = lua_toboolean(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  g_bal.disabled++;
  ImGui::BeginDisabled(cond);
  lua_pushvalue(L, 2);
  int err = lua_pcall(L, 0, 0, 0);
  ImGui::EndDisabled();
  g_bal.disabled--;
  if (err) return lua_error(L);
  return 0;
}

// draw list handles: registry array of ImDrawList*, lightuserdata index
static std::vector<ImDrawList*> g_dl;
static const char* DL_MT = "tw.DrawList";

static ImDrawList* check_dl(lua_State* L, int i) {
  int idx = (int)(intptr_t)lua_touserdata(L, i);
  if (idx < 0 || idx >= (int)g_dl.size()) luaL_error(L, "bad draw list");
  return g_dl[idx];
}
static int l_dl_gc(lua_State* L) {
  (void)L;
  return 0; // registry cleared per frame; handles are borrowed
}

// ── window / layout ─────────────────────────────────────────────────────────

static int l_begin(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  ImGuiWindowFlags flags = (ImGuiWindowFlags)luaL_optinteger(L, 2, 0);
  bool open = ImGui::Begin(name, nullptr, flags);
  lua_pushboolean(L, open);
  return 1;
}
static int l_end(lua_State* L) {
  ImGui::End();
  return 0;
}
static int l_begin_child(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  float w = (float)luaL_optnumber(L, 2, 0);
  float h = (float)luaL_optnumber(L, 3, 0);
  ImGuiChildFlags cflags = (ImGuiChildFlags)luaL_optinteger(L, 4, 0);
  ImGuiWindowFlags wflags = (ImGuiWindowFlags)luaL_optinteger(L, 5, 0);
  bool open = ImGui::BeginChild(name, ImVec2(w, h), cflags, wflags);
  lua_pushboolean(L, open);
  return 1;
}
static int l_end_child(lua_State* L) {
  ImGui::EndChild();
  return 0;
}
static int l_same_line(lua_State* L) {
  ImGui::SameLine((float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, -1));
  return 0;
}
static int l_separator(lua_State* L) {
  ImGui::Separator();
  return 0;
}
static int l_spacing(lua_State* L) {
  ImGui::Spacing();
  return 0;
}
static int l_dummy(lua_State* L) {
  ImGui::Dummy(ImVec2((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)));
  return 0;
}
static int l_invisible_button(lua_State* L) {
  lua_pushboolean(L, ImGui::InvisibleButton(luaL_checkstring(L, 1),
                                            ImVec2((float)luaL_checknumber(L, 2),
                                                   (float)luaL_checknumber(L, 3))));
  return 1;
}
static int l_indent(lua_State* L) {
  ImGui::Indent((float)luaL_optnumber(L, 1, 0));
  return 0;
}
static int l_unindent(lua_State* L) {
  ImGui::Unindent((float)luaL_optnumber(L, 1, 0));
  return 0;
}
static int l_text(lua_State* L) {
  ImGui::TextUnformatted(luaL_checkstring(L, 1));
  return 0;
}
static int l_text_colored(lua_State* L) {
  const char* s = luaL_checkstring(L, 1);
  ImVec4 c((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
           (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
  ImGui::TextColored(c, "%s", s);
  return 0;
}
static int l_text_wrapped(lua_State* L) {
  ImGui::TextWrapped("%s", luaL_checkstring(L, 1));
  return 0;
}
static int l_label_text(lua_State* L) {
  ImGui::LabelText("%s", luaL_checkstring(L, 1), "%s", luaL_checkstring(L, 2));
  return 0;
}
static int l_bullet_text(lua_State* L) {
  ImGui::BulletText("%s", luaL_checkstring(L, 1));
  return 0;
}
static int l_align_text(lua_State* L) {
  ImGui::AlignTextToFramePadding();
  return 0;
}
static int l_new_line(lua_State* L) {
  ImGui::NewLine();
  return 0;
}
static int l_begin_group(lua_State* L) {
  ImGui::BeginGroup();
  return 0;
}
static int l_end_group(lua_State* L) {
  ImGui::EndGroup();
  return 0;
}

// ── widgets ─────────────────────────────────────────────────────────────────

static int l_button(lua_State* L) {
  bool hit = ImGui::Button(luaL_checkstring(L, 1),
                           ImVec2((float)luaL_optnumber(L, 2, 0),
                                  (float)luaL_optnumber(L, 3, 0)));
  lua_pushboolean(L, hit);
  return 1;
}
static int l_small_button(lua_State* L) {
  lua_pushboolean(L, ImGui::SmallButton(luaL_checkstring(L, 1)));
  return 1;
}
static int l_arrow_button(lua_State* L) {
  lua_pushboolean(L, ImGui::ArrowButton(luaL_checkstring(L, 1),
                                        (ImGuiDir)luaL_checkinteger(L, 2)));
  return 1;
}
static int l_checkbox(lua_State* L) {
  bool v = lua_toboolean(L, 2);
  bool changed = ImGui::Checkbox(luaL_checkstring(L, 1), &v);
  lua_pushboolean(L, changed);
  lua_pushboolean(L, v);
  return 2;
}
static int l_radio_button(lua_State* L) {
  bool active = lua_toboolean(L, 2);
  if (ImGui::RadioButton(luaL_checkstring(L, 1), active)) {
    lua_pushboolean(L, true);
    return 1;
  }
  lua_pushboolean(L, false);
  return 1;
}
static int l_combo(lua_State* L) {
  const char* label = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  int current = (int)luaL_checkinteger(L, 3);
  int n = (int)lua_rawlen(L, 2);
  if (current < 0) current = 0;
  if (current >= n) current = n - 1;
  const char* preview = "";
  if (n > 0) {
    lua_rawgeti(L, 2, current + 1);
    preview = lua_tostring(L, -1);
  }
  bool changed = false;
  if (ImGui::BeginCombo(label, preview)) {
    for (int i = 0; i < n; i++) {
      lua_rawgeti(L, 2, i + 1);
      const char* item = lua_tostring(L, -1);
      if (ImGui::Selectable(item ? item : "", i == current)) {
        current = i;
        changed = true;
      }
      lua_pop(L, 1);
    }
    ImGui::EndCombo();
  }
  if (n > 0) lua_pop(L, 1);
  lua_pushboolean(L, changed);
  lua_pushinteger(L, current);
  return 2;
}
static int l_slider_float(lua_State* L) {
  float v = (float)luaL_checknumber(L, 2);
  bool changed = ImGui::SliderFloat(luaL_checkstring(L, 1), &v,
                                    (float)luaL_checknumber(L, 3),
                                    (float)luaL_checknumber(L, 4),
                                    opt_str(L, 5, "%.3f"));
  lua_pushboolean(L, changed);
  lua_pushnumber(L, v);
  return 2;
}
static int l_slider_int(lua_State* L) {
  int v = (int)luaL_checkinteger(L, 2);
  bool changed = ImGui::SliderInt(luaL_checkstring(L, 1), &v,
                                  (int)luaL_checkinteger(L, 3),
                                  (int)luaL_checkinteger(L, 4),
                                  opt_str(L, 5, "%d"));
  lua_pushboolean(L, changed);
  lua_pushinteger(L, v);
  return 2;
}
static int l_drag_float(lua_State* L) {
  float v = (float)luaL_checknumber(L, 2);
  bool changed = ImGui::DragFloat(luaL_checkstring(L, 1), &v,
                                  (float)luaL_optnumber(L, 3, 0.01f),
                                  (float)luaL_optnumber(L, 4, 0),
                                  (float)luaL_optnumber(L, 5, 0),
                                  opt_str(L, 6, "%.3f"));
  lua_pushboolean(L, changed);
  lua_pushnumber(L, v);
  return 2;
}
static int l_drag_int(lua_State* L) {
  const char* label = luaL_checkstring(L, 1);
  int v = (int)luaL_checkinteger(L, 2);
  float speed = (float)luaL_optnumber(L, 3, 1.0f);
  int min_v = (int)luaL_optinteger(L, 4, 0);
  int max_v = (int)luaL_optinteger(L, 5, 0);
  bool changed = ImGui::DragInt(label, &v, speed, min_v, max_v);
  lua_pushboolean(L, changed);
  lua_pushinteger(L, v);
  return 2;
}
static int l_input_int(lua_State* L) {
  int v = (int)luaL_checkinteger(L, 2);
  bool changed = ImGui::InputInt(luaL_checkstring(L, 1), &v,
                                 (int)luaL_optinteger(L, 3, 1));
  lua_pushboolean(L, changed);
  lua_pushinteger(L, v);
  return 2;
}
static int l_input_float(lua_State* L) {
  float v = (float)luaL_checknumber(L, 2);
  bool changed = ImGui::InputFloat(luaL_checkstring(L, 1), &v,
                                   (float)luaL_optnumber(L, 3, 0.01f));
  lua_pushboolean(L, changed);
  lua_pushnumber(L, v);
  return 2;
}
static int l_input_text(lua_State* L) {
  const char* label = luaL_checkstring(L, 1);
  static std::string buf;
  buf = luaL_optstring(L, 2, "");
  bool changed = ImGui::InputText(label, &buf);
  lua_pushboolean(L, changed);
  lua_pushstring(L, buf.c_str());
  return 2;
}
static int l_input_text_with_hint(lua_State* L) {
  const char* label = luaL_checkstring(L, 1);
  const char* hint = luaL_optstring(L, 2, "");
  static std::string buf;
  buf = luaL_optstring(L, 3, "");
  bool changed = ImGui::InputTextWithHint(label, hint, &buf);
  lua_pushboolean(L, changed);
  lua_pushstring(L, buf.c_str());
  return 2;
}
static int l_color_edit4(lua_State* L) {
  float c[4] = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5)};
  ImGuiColorEditFlags flags = (ImGuiColorEditFlags)luaL_optinteger(L, 6, 0);
  bool changed = ImGui::ColorEdit4(luaL_checkstring(L, 1), c, flags);
  lua_pushboolean(L, changed);
  for (int i = 0; i < 4; i++) lua_pushnumber(L, c[i]);
  return 5;
}
static int l_color_edit3(lua_State* L) {
  float c[3] = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                (float)luaL_checknumber(L, 4)};
  ImGuiColorEditFlags flags = (ImGuiColorEditFlags)luaL_optinteger(L, 5, 0);
  bool changed = ImGui::ColorEdit3(luaL_checkstring(L, 1), c, flags);
  lua_pushboolean(L, changed);
  for (int i = 0; i < 3; i++) lua_pushnumber(L, c[i]);
  return 4;
}
static int l_color_picker3(lua_State* L) {
  float c[3] = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                (float)luaL_checknumber(L, 4)};
  ImGuiColorEditFlags flags = (ImGuiColorEditFlags)luaL_optinteger(L, 5, 0);
  bool changed = ImGui::ColorPicker3(luaL_checkstring(L, 1), c, flags);
  lua_pushboolean(L, changed);
  for (int i = 0; i < 3; i++) lua_pushnumber(L, c[i]);
  return 4;
}
static int l_color_picker4(lua_State* L) {
  float c[4] = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                (float)luaL_checknumber(L, 4), (float)luaL_optnumber(L, 5, 1.0)};
  ImGuiColorEditFlags flags = (ImGuiColorEditFlags)luaL_optinteger(L, 6, 0);
  bool changed = ImGui::ColorPicker4(luaL_checkstring(L, 1), c, flags);
  lua_pushboolean(L, changed);
  for (int i = 0; i < 4; i++) lua_pushnumber(L, c[i]);
  return 5;
}
static int l_color_button(lua_State* L) {
  const char* id = luaL_checkstring(L, 1);
  ImVec4 c = check_v4(L, 2);
  bool hit = ImGui::ColorButton(id, c,
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                                ImVec2((float)luaL_optnumber(L, 6, 0),
                                       (float)luaL_optnumber(L, 7, 0)));
  lua_pushboolean(L, hit);
  return 1;
}
static int l_set_next_item_width(lua_State* L) {
  ImGui::SetNextItemWidth((float)luaL_checknumber(L, 1));
  return 0;
}
static int l_push_item_width(lua_State* L) {
  ImGui::PushItemWidth((float)luaL_checknumber(L, 1));
  return 0;
}
static int l_pop_item_width(lua_State* L) {
  ImGui::PopItemWidth();
  return 0;
}
static int l_begin_disabled(lua_State* L) {
  ImGui::BeginDisabled(lua_toboolean(L, 1));
  return 0;
}
static int l_end_disabled(lua_State* L) {
  ImGui::EndDisabled();
  return 0;
}
static int l_progress_bar(lua_State* L) {
  ImGui::ProgressBar((float)luaL_checknumber(L, 1),
                     ImVec2((float)luaL_optnumber(L, 2, 0),
                            (float)luaL_optnumber(L, 3, 0)),
                     lua_isnoneornil(L, 4) ? nullptr : luaL_checkstring(L, 4));
  return 0;
}

// ── tables ──────────────────────────────────────────────────────────────────

static int l_begin_table(lua_State* L) {
  bool open = ImGui::BeginTable(
      luaL_checkstring(L, 1), (int)luaL_checkinteger(L, 2),
      (ImGuiTableFlags)luaL_optinteger(L, 3, 0),
      ImVec2((float)luaL_optnumber(L, 4, 0), (float)luaL_optnumber(L, 5, 0)));
  lua_pushboolean(L, open);
  return 1;
}
static int l_table_setup_column(lua_State* L) {
  ImGui::TableSetupColumn(luaL_checkstring(L, 1),
                          (ImGuiTableColumnFlags)luaL_optinteger(L, 2, 0),
                          (float)luaL_optnumber(L, 3, 0));
  return 0;
}
static int l_table_headers_row(lua_State* L) {
  ImGui::TableHeadersRow();
  return 0;
}
static int l_table_next_row(lua_State* L) {
  ImGui::TableNextRow();
  return 0;
}
static int l_table_next_column(lua_State* L) {
  lua_pushboolean(L, ImGui::TableNextColumn());
  return 1;
}
static int l_table_set_column_index(lua_State* L) {
  ImGui::TableSetColumnIndex((int)luaL_checkinteger(L, 1));
  return 0;
}
static int l_end_table(lua_State* L) {
  ImGui::EndTable();
  return 0;
}

// ── selectable / tree / list ────────────────────────────────────────────────

static int l_selectable(lua_State* L) {
  bool hit = ImGui::Selectable(
      luaL_checkstring(L, 1), lua_toboolean(L, 2),
      (ImGuiSelectableFlags)luaL_optinteger(L, 3, 0),
      ImVec2((float)luaL_optnumber(L, 4, 0), (float)luaL_optnumber(L, 5, 0)));
  lua_pushboolean(L, hit);
  return 1;
}
static int l_tree_node(lua_State* L) {
  bool open = ImGui::TreeNodeEx(luaL_checkstring(L, 1),
                                ImGuiTreeNodeFlags_DefaultOpen |
                                    (ImGuiTreeNodeFlags)luaL_optinteger(L, 2, 0));
  lua_pushboolean(L, open);
  return 1;
}
static int l_tree_pop(lua_State* L) {
  ImGui::TreePop();
  return 0;
}
static int l_collapsing_header(lua_State* L) {
  bool open = ImGui::CollapsingHeader(luaL_checkstring(L, 1),
                                      (ImGuiTreeNodeFlags)luaL_optinteger(L, 2, 0));
  lua_pushboolean(L, open);
  return 1;
}
static int l_begin_list_box(lua_State* L) {
  bool open = ImGui::BeginListBox(luaL_checkstring(L, 1),
                                  ImVec2((float)luaL_optnumber(L, 2, 0),
                                         (float)luaL_optnumber(L, 3, 0)));
  lua_pushboolean(L, open);
  return 1;
}
static int l_end_list_box(lua_State* L) {
  ImGui::EndListBox();
  return 0;
}
static int l_begin_tab_bar(lua_State* L) {
  bool open = ImGui::BeginTabBar(luaL_checkstring(L, 1),
                                 (ImGuiTabBarFlags)luaL_optinteger(L, 2, 0));
  lua_pushboolean(L, open);
  return 1;
}
static int l_end_tab_bar(lua_State* L) {
  ImGui::EndTabBar();
  return 0;
}
static int l_begin_tab_item(lua_State* L) {
  bool open = ImGui::BeginTabItem(luaL_checkstring(L, 1), nullptr,
                                  (ImGuiTabItemFlags)luaL_optinteger(L, 2, 0));
  lua_pushboolean(L, open);
  return 1;
}
static int l_end_tab_item(lua_State* L) {
  ImGui::EndTabItem();
  return 0;
}

// ── popups / menus / tooltips ───────────────────────────────────────────────

static int l_begin_popup_context_item(lua_State* L) {
  bool open = ImGui::BeginPopupContextItem(
      lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1),
      (ImGuiPopupFlags)luaL_optinteger(L, 2, 1));
  lua_pushboolean(L, open);
  return 1;
}
static int l_begin_popup_context_window(lua_State* L) {
  bool open = ImGui::BeginPopupContextWindow(
      lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1),
      (ImGuiPopupFlags)luaL_optinteger(L, 2, 1));
  lua_pushboolean(L, open);
  return 1;
}
static int l_open_popup(lua_State* L) {
  ImGui::OpenPopup(luaL_checkstring(L, 1));
  return 0;
}
static int l_begin_popup(lua_State* L) {
  bool open = ImGui::BeginPopup(luaL_checkstring(L, 1));
  lua_pushboolean(L, open);
  return 1;
}
static int l_begin_popup_modal(lua_State* L) {
  bool open = ImGui::BeginPopupModal(luaL_checkstring(L, 1), nullptr,
                                     (ImGuiWindowFlags)luaL_optinteger(L, 2, 0));
  lua_pushboolean(L, open);
  return 1;
}
static int l_end_popup(lua_State* L) {
  ImGui::EndPopup();
  return 0;
}
static int l_close_current_popup(lua_State* L) {
  ImGui::CloseCurrentPopup();
  return 0;
}
static int l_begin_menu(lua_State* L) {
  bool open = ImGui::BeginMenu(luaL_checkstring(L, 1), opt_bool(L, 2, true));
  lua_pushboolean(L, open);
  return 1;
}
static int l_end_menu(lua_State* L) {
  ImGui::EndMenu();
  return 0;
}
static int l_begin_menu_bar(lua_State* L) {
  lua_pushboolean(L, ImGui::BeginMenuBar());
  return 1;
}
static int l_end_menu_bar(lua_State* L) {
  ImGui::EndMenuBar();
  return 0;
}
static int l_menu_item(lua_State* L) {
  bool hit = ImGui::MenuItem(luaL_checkstring(L, 1),
                             lua_isnoneornil(L, 2) ? nullptr : luaL_checkstring(L, 2),
                             lua_toboolean(L, 3), opt_bool(L, 4, true));
  lua_pushboolean(L, hit);
  return 1;
}
static int l_set_tooltip(lua_State* L) {
  ImGui::SetTooltip("%s", luaL_checkstring(L, 1));
  return 0;
}
static int l_begin_tooltip(lua_State* L) {
  ImGui::BeginTooltip();
  return 0;
}
static int l_end_tooltip(lua_State* L) {
  ImGui::EndTooltip();
  return 0;
}

static int l_plot_lines(lua_State* L) {
  const char* label = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  int count = (int)luaL_optinteger(L, 3, (lua_Integer)lua_rawlen(L, 2));
  float scale_min = (float)luaL_optnumber(L, 4, 0.0);
  float scale_max = (float)luaL_optnumber(L, 5, 0.0);
  float gw = (float)luaL_optnumber(L, 6, 0.0);
  float gh = (float)luaL_optnumber(L, 7, 40.0);
  const char* overlay = luaL_optstring(L, 8, nullptr);

  if (count <= 0) return 0;
  std::vector<float> vals(count);
  for (int i = 0; i < count; i++) {
    lua_rawgeti(L, 2, i + 1);
    vals[i] = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
  }
  ImGui::PlotLines(label, vals.data(), count, 0, overlay,
                   scale_min == 0.0f && scale_max == 0.0f ? FLT_MAX : scale_min,
                   scale_min == 0.0f && scale_max == 0.0f ? FLT_MAX : scale_max,
                   ImVec2(gw, gh));
  return 0;
}

// ── draw list ───────────────────────────────────────────────────────────────

static int push_dl(lua_State* L, ImDrawList* dl) {
  g_dl.push_back(dl);
  lua_pushlightuserdata(L, (void*)(intptr_t)(g_dl.size() - 1));
  luaL_setmetatable(L, DL_MT);
  return 1;
}
void ig_clear_draw_lists() {
  g_dl.clear();
}

static int l_get_window_draw_list(lua_State* L) {
  return push_dl(L, ImGui::GetWindowDrawList());
}
static int l_get_foreground_draw_list(lua_State* L) {
  return push_dl(L, ImGui::GetForegroundDrawList());
}
static int l_dl_add_image(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImTextureID tex = (ImTextureID)(intptr_t)luaL_checkinteger(L, 2);
  ImVec2 a((float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
  ImVec2 b((float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6));
  ImVec2 ua((float)luaL_optnumber(L, 7, 0), (float)luaL_optnumber(L, 8, 0));
  ImVec2 ub((float)luaL_optnumber(L, 9, 1), (float)luaL_optnumber(L, 10, 1));
  ImU32 col = IM_COL32_WHITE;
  if (lua_gettop(L) >= 14) col = col32(L, 11);
  dl->AddImage(tex, a, b, ua, ub, col);
  return 0;
}
static int l_dl_add_rect_filled(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 a((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  ImVec2 b((float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
  ImU32 col = col32(L, 6);
  float rounding = (float)luaL_optnumber(L, 10, 0);
  dl->AddRectFilled(a, b, col, rounding);
  return 0;
}
static int l_dl_add_rect(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 a((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  ImVec2 b((float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
  ImU32 col = col32(L, 6);
  float rounding = (float)luaL_optnumber(L, 10, 0);
  float thickness = (float)luaL_optnumber(L, 11, 1);
  dl->AddRect(a, b, col, rounding, 0, thickness);
  return 0;
}
static int l_dl_add_line(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 a((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  ImVec2 b((float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
  ImU32 col = col32(L, 6);
  float thickness = (float)luaL_optnumber(L, 10, 1);
  dl->AddLine(a, b, col, thickness);
  return 0;
}
static int l_dl_add_text(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 pos((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  ImU32 col = col32(L, 4);
  dl->AddText(pos, col, luaL_checkstring(L, 8));
  return 0;
}
static int l_dl_add_circle_filled(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 c((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  float rad = (float)luaL_checknumber(L, 4);
  ImU32 col = col32(L, 5);
  dl->AddCircleFilled(c, rad, col, (int)luaL_optinteger(L, 9, 24));
  return 0;
}
static int l_dl_add_circle(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 c((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  float rad = (float)luaL_checknumber(L, 4);
  ImU32 col = col32(L, 5);
  int segs = (int)luaL_optinteger(L, 9, 0);
  float thickness = (float)luaL_optnumber(L, 10, 1.0f);
  dl->AddCircle(c, rad, col, segs, thickness);
  return 0;
}
static int l_dl_add_triangle_filled(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 a((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  ImVec2 b((float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
  ImVec2 c((float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7));
  ImU32 col = col32(L, 8);
  dl->AddTriangleFilled(a, b, c, col);
  return 0;
}
static int l_dl_push_clip_rect(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  ImVec2 a((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
  ImVec2 b((float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
  dl->PushClipRect(a, b, lua_toboolean(L, 6));
  return 0;
}
static int l_dl_pop_clip_rect(lua_State* L) {
  ImDrawList* dl = check_dl(L, 1);
  dl->PopClipRect();
  return 0;
}

// ── style ───────────────────────────────────────────────────────────────────

static int l_push_style_color(lua_State* L) {
  ImGui::PushStyleColor((ImGuiCol)luaL_checkinteger(L, 1), check_v4(L, 2));
  return 0;
}
static int l_pop_style_color(lua_State* L) {
  ImGui::PopStyleColor((int)luaL_optinteger(L, 1, 1));
  return 0;
}
static int l_push_style_var(lua_State* L) {
  ImGuiStyleVar v = (ImGuiStyleVar)luaL_checkinteger(L, 1);
  // imgui asserts if the wrong PushStyleVar variant is used for a var
  auto is_v2 = [](ImGuiStyleVar x) {
    switch (x) {
      case ImGuiStyleVar_WindowPadding:
      case ImGuiStyleVar_WindowMinSize:
      case ImGuiStyleVar_WindowTitleAlign:
      case ImGuiStyleVar_FramePadding:
      case ImGuiStyleVar_ItemSpacing:
      case ImGuiStyleVar_ItemInnerSpacing:
      case ImGuiStyleVar_CellPadding:
      case ImGuiStyleVar_ButtonTextAlign:
      case ImGuiStyleVar_SelectableTextAlign:
      case ImGuiStyleVar_SeparatorTextAlign:
      case ImGuiStyleVar_SeparatorTextPadding:
        return true;
      default:
        return false;
    }
  };
  if (is_v2(v)) {
    ImGui::PushStyleVar(v, check_v2(L, 2));
  } else {
    ImGui::PushStyleVar(v, (float)luaL_checknumber(L, 2));
  }
  return 0;
}
static int l_pop_style_var(lua_State* L) {
  ImGui::PopStyleVar((int)luaL_optinteger(L, 1, 1));
  return 0;
}
static int l_push_font(lua_State* L) {
  int i = (int)luaL_checkinteger(L, 1);
  if (i >= 0 && i < (int)ImGui::GetIO().Fonts->Fonts.Size) {
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[i]);
  }
  return 0;
}
static int l_pop_font(lua_State* L) {
  ImGui::PopFont();
  return 0;
}
static int l_has_font(lua_State* L) {
  int i = (int)luaL_checkinteger(L, 1);
  lua_pushboolean(L, i >= 0 && i < (int)ImGui::GetIO().Fonts->Fonts.Size);
  return 1;
}
static int l_set_next_window_pos(lua_State* L) {
  ImGui::SetNextWindowPos(ImVec2((float)luaL_checknumber(L, 1),
                                 (float)luaL_checknumber(L, 2)));
  return 0;
}
static int l_set_next_window_size(lua_State* L) {
  ImGui::SetNextWindowSize(ImVec2((float)luaL_checknumber(L, 1),
                                  (float)luaL_checknumber(L, 2)));
  return 0;
}
static int l_set_next_window_bg_alpha(lua_State* L) {
  ImGui::SetNextWindowBgAlpha((float)luaL_checknumber(L, 1));
  return 0;
}

// ── state / info ────────────────────────────────────────────────────────────

static int l_is_item_clicked(lua_State* L) {
  lua_pushboolean(L, ImGui::IsItemClicked((ImGuiMouseButton)luaL_optinteger(L, 1, 0)));
  return 1;
}
static int l_is_item_active(lua_State* L) {
  lua_pushboolean(L, ImGui::IsItemActive());
  return 1;
}
static int l_is_item_hovered(lua_State* L) {
  lua_pushboolean(L, ImGui::IsItemHovered());
  return 1;
}
static int l_is_item_edited(lua_State* L) {
  lua_pushboolean(L, ImGui::IsItemEdited());
  return 1;
}
static int l_is_item_deactivated_after_edit(lua_State* L) {
  lua_pushboolean(L, ImGui::IsItemDeactivatedAfterEdit());
  return 1;
}
static int l_is_window_hovered(lua_State* L) {
  lua_pushboolean(L, ImGui::IsWindowHovered(
                         (ImGuiHoveredFlags)luaL_optinteger(L, 1, 0)));
  return 1;
}
static int l_is_window_focused(lua_State* L) {
  lua_pushboolean(L, ImGui::IsWindowFocused(
                         (ImGuiFocusedFlags)luaL_optinteger(L, 1, 0)));
  return 1;
}
static int l_is_mouse_down(lua_State* L) {
  lua_pushboolean(L, ImGui::IsMouseDown((ImGuiMouseButton)luaL_checkinteger(L, 1)));
  return 1;
}
static int l_is_mouse_clicked(lua_State* L) {
  lua_pushboolean(L, ImGui::IsMouseClicked((ImGuiMouseButton)luaL_checkinteger(L, 1),
                                           opt_bool(L, 2, false)));
  return 1;
}
static int l_is_mouse_released(lua_State* L) {
  lua_pushboolean(L, ImGui::IsMouseReleased((ImGuiMouseButton)luaL_optinteger(L, 1, 0)));
  return 1;
}
static int l_is_mouse_dragging(lua_State* L) {
  lua_pushboolean(L, ImGui::IsMouseDragging((ImGuiMouseButton)luaL_checkinteger(L, 1),
                                            (float)luaL_optnumber(L, 2, 4)));
  return 1;
}
static int l_get_mouse_pos(lua_State* L) {
  ImVec2 p = ImGui::GetMousePos();
  push_v2(L, p);
  return 2;
}
static int l_get_mouse_drag_delta(lua_State* L) {
  ImVec2 d = ImGui::GetMouseDragDelta((ImGuiMouseButton)luaL_checkinteger(L, 1),
                                      (float)luaL_optnumber(L, 2, -1));
  push_v2(L, d);
  return 2;
}
static int l_reset_mouse_drag_delta(lua_State* L) {
  ImGui::ResetMouseDragDelta((ImGuiMouseButton)luaL_optinteger(L, 1, 0));
  return 0;
}
static int l_get_content_region_avail(lua_State* L) {
  push_v2(L, ImGui::GetContentRegionAvail());
  return 2;
}
static int l_get_cursor_pos(lua_State* L) {
  push_v2(L, ImGui::GetCursorPos());
  return 2;
}
static int l_set_cursor_pos(lua_State* L) {
  ImGui::SetCursorPos(ImVec2((float)luaL_checknumber(L, 1),
                             (float)luaL_checknumber(L, 2)));
  return 0;
}
static int l_get_cursor_screen_pos(lua_State* L) {
  push_v2(L, ImGui::GetCursorScreenPos());
  return 2;
}
static int l_get_item_rect_min(lua_State* L) {
  push_v2(L, ImGui::GetItemRectMin());
  return 2;
}
static int l_get_item_rect_max(lua_State* L) {
  push_v2(L, ImGui::GetItemRectMax());
  return 2;
}
static int l_get_frame_height(lua_State* L) {
  lua_pushnumber(L, ImGui::GetFrameHeight());
  return 1;
}
static int l_get_frame_height_with_spacing(lua_State* L) {
  lua_pushnumber(L, ImGui::GetFrameHeightWithSpacing());
  return 1;
}
static int l_get_window_pos(lua_State* L) {
  push_v2(L, ImGui::GetWindowPos());
  return 2;
}
static int l_get_window_size(lua_State* L) {
  push_v2(L, ImGui::GetWindowSize());
  return 2;
}
static int l_calc_text_size(lua_State* L) {
  push_v2(L, ImGui::CalcTextSize(luaL_checkstring(L, 1)));
  return 2;
}
static int l_get_text_line_height(lua_State* L) {
  lua_pushnumber(L, ImGui::GetTextLineHeight());
  return 1;
}
static int l_get_font_size(lua_State* L) {
  lua_pushnumber(L, ImGui::GetFontSize());
  return 1;
}
static int l_set_scroll_here_y(lua_State* L) {
  ImGui::SetScrollHereY((float)luaL_optnumber(L, 1, 0.5));
  return 0;
}
static int l_get_scroll_y(lua_State* L) {
  lua_pushnumber(L, ImGui::GetScrollY());
  return 1;
}
static int l_set_scroll_y(lua_State* L) {
  ImGui::SetScrollY((float)luaL_checknumber(L, 1));
  return 0;
}
static int l_get_id(lua_State* L) {
  lua_pushinteger(L, (intptr_t)ImGui::GetID(luaL_checkstring(L, 1)));
  return 1;
}
static int l_push_id(lua_State* L) {
  ImGui::PushID(luaL_checkstring(L, 1));
  return 0;
}
static int l_pop_id(lua_State* L) {
  ImGui::PopID();
  return 0;
}
static int l_is_key_pressed(lua_State* L) {
  ImGuiKey key = (ImGuiKey)luaL_checkinteger(L, 1);
  if (!ImGui::IsNamedKey(key)) {
    return luaL_error(L, "is_key_pressed: %d is not a valid ImGuiKey (use ig.key.* constants)", (int)key);
  }
  lua_pushboolean(L, ImGui::IsKeyPressed(key, opt_bool(L, 2, false)));
  return 1;
}
static int l_is_key_down(lua_State* L) {
  ImGuiKey key = (ImGuiKey)luaL_checkinteger(L, 1);
  if (!ImGui::IsNamedKey(key)) {
    return luaL_error(L, "is_key_down: %d is not a valid ImGuiKey (use ig.key.* constants)", (int)key);
  }
  lua_pushboolean(L, ImGui::IsKeyDown(key));
  return 1;
}
static int l_get_io(lua_State* L) {
  const ImGuiIO& io = ImGui::GetIO();
  lua_newtable(L);
  lua_pushnumber(L, io.DisplaySize.x);
  lua_setfield(L, -2, "display_w");
  lua_pushnumber(L, io.DisplaySize.y);
  lua_setfield(L, -2, "display_h");
  lua_pushnumber(L, io.DeltaTime);
  lua_setfield(L, -2, "delta_time");
  lua_pushnumber(L, io.MouseWheel);
  lua_setfield(L, -2, "mouse_wheel");
  lua_pushboolean(L, io.WantCaptureMouse);
  lua_setfield(L, -2, "want_capture_mouse");
  lua_pushboolean(L, io.WantCaptureKeyboard);
  lua_setfield(L, -2, "want_capture_keyboard");
  lua_pushboolean(L, io.WantTextInput);
  lua_setfield(L, -2, "want_text_input");
  lua_pushboolean(L, io.KeyCtrl);
  lua_setfield(L, -2, "key_ctrl");
  lua_pushboolean(L, io.KeyShift);
  lua_setfield(L, -2, "key_shift");
  lua_pushboolean(L, io.KeyAlt);
  lua_setfield(L, -2, "key_alt");
  lua_pushboolean(L, io.KeySuper);
  lua_setfield(L, -2, "key_super");
  return 1;
}

// ── registration ────────────────────────────────────────────────────────────

#define REG(name) lua_pushcfunction(L, l_##name); lua_setfield(L, -2, #name)

void ig_register(lua_State* L) {
  luaL_newmetatable(L, DL_MT);
  lua_pushcfunction(L, l_dl_gc);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);

  lua_getglobal(L, "lp");
  lua_newtable(L);
  REG(begin);
  lua_pushcfunction(L, l_end);
  lua_setfield(L, -2, "end_");
  REG(begin_child);
  REG(end_child);
  REG(same_line);
  REG(separator);
  REG(spacing);
  REG(dummy);
  REG(invisible_button);
  REG(indent);
  REG(unindent);
  REG(text);
  REG(text_colored);
  REG(text_wrapped);
  REG(label_text);
  REG(bullet_text);
  REG(align_text);
  REG(new_line);
  REG(begin_group);
  REG(end_group);
  REG(button);
  REG(small_button);
  REG(arrow_button);
  REG(checkbox);
  REG(radio_button);
  REG(combo);
  REG(slider_float);
  REG(slider_int);
  REG(drag_float);
  REG(drag_int);
  REG(input_int);
  REG(input_float);
  REG(input_text);
  REG(input_text_with_hint);
  REG(color_edit4);
  REG(color_edit3);
  REG(color_button);
  REG(color_picker4);
  REG(color_picker3);
  REG(set_next_item_width);
  REG(push_item_width);
  REG(pop_item_width);
  REG(end_disabled);
  REG(progress_bar);
  REG(begin_table);
  REG(table_setup_column);
  REG(table_headers_row);
  REG(table_next_row);
  REG(table_next_column);
  REG(table_set_column_index);
  REG(end_table);
  REG(selectable);
  REG(tree_node);
  REG(tree_pop);
  REG(collapsing_header);
  REG(begin_list_box);
  REG(end_list_box);
  REG(begin_tab_bar);
  REG(end_tab_bar);
  REG(begin_tab_item);
  REG(end_tab_item);
  REG(begin_popup_context_window);
  REG(begin_popup_context_item);
  REG(open_popup);
  REG(begin_popup);
  REG(begin_popup_modal);
  REG(end_popup);
  REG(close_current_popup);
  REG(begin_menu);
  REG(end_menu);
  REG(begin_menu_bar);
  REG(end_menu_bar);
  REG(menu_item);
  REG(set_tooltip);
  REG(begin_tooltip);
  REG(end_tooltip);
  REG(get_window_draw_list);
  REG(plot_lines);
  REG(get_foreground_draw_list);
  REG(dl_add_image);
  REG(dl_add_rect_filled);
  REG(dl_add_rect);
  REG(dl_add_line);
  REG(dl_add_text);
  REG(dl_add_circle_filled);
  REG(dl_add_circle);
  REG(dl_add_triangle_filled);
  REG(dl_push_clip_rect);
  REG(dl_pop_clip_rect);
  REG(push_style_color);
  REG(pop_style_color);
  REG(push_style_var);
  REG(pop_style_var);
  REG(push_font);
  REG(pop_font);
  REG(has_font);
  REG(set_next_window_pos);
  REG(set_next_window_size);
  REG(set_next_window_bg_alpha);
  REG(is_item_clicked);
  REG(is_item_active);
  REG(reset_mouse_drag_delta);
  REG(is_item_hovered);
  REG(is_item_edited);
  REG(is_item_deactivated_after_edit);
  REG(is_window_hovered);
  REG(is_window_focused);
  REG(is_mouse_down);
  REG(is_mouse_clicked);
  REG(is_mouse_released);
  REG(is_mouse_dragging);
  REG(get_mouse_pos);
  REG(get_mouse_drag_delta);
  REG(get_content_region_avail);
  REG(get_cursor_pos);
  REG(set_cursor_pos);
  REG(get_cursor_screen_pos);
  REG(get_item_rect_min);
  REG(get_item_rect_max);
  REG(get_frame_height);
  REG(get_frame_height_with_spacing);
  REG(get_window_pos);
  REG(get_window_size);
  REG(calc_text_size);
  REG(get_text_line_height);
  REG(get_font_size);
  REG(set_scroll_here_y);
  REG(get_scroll_y);
  REG(set_scroll_y);
  REG(get_id);
  REG(push_id);
  REG(pop_id);
  REG(is_key_pressed);
  REG(is_key_down);
  REG(get_io);

  // ── Scoped wrappers (safe Begin/End — preferred API) ──────────────────────
  lua_pushcfunction(L, l_scoped_window);   lua_setfield(L, -2, "window");
  lua_pushcfunction(L, l_scoped_child);    lua_setfield(L, -2, "child");
  lua_pushcfunction(L, l_scoped_popup);    lua_setfield(L, -2, "popup");
  lua_pushcfunction(L, l_scoped_popup_modal);          lua_setfield(L, -2, "popup_modal");
  lua_pushcfunction(L, l_scoped_popup_context_window); lua_setfield(L, -2, "popup_context_window");
  lua_pushcfunction(L, l_scoped_popup_context_item);   lua_setfield(L, -2, "popup_context_item");
  lua_pushcfunction(L, l_scoped_menu);     lua_setfield(L, -2, "menu");
  lua_pushcfunction(L, l_scoped_menu_bar); lua_setfield(L, -2, "menu_bar");
  lua_pushcfunction(L, l_scoped_table);    lua_setfield(L, -2, "table_");
  lua_pushcfunction(L, l_scoped_tab_bar);  lua_setfield(L, -2, "tab_bar");
  lua_pushcfunction(L, l_scoped_tab_item); lua_setfield(L, -2, "tab_item");
  lua_pushcfunction(L, l_scoped_list_box); lua_setfield(L, -2, "list_box");
  lua_pushcfunction(L, l_scoped_tree_node);lua_setfield(L, -2, "tree");
  lua_pushcfunction(L, l_scoped_tooltip);  lua_setfield(L, -2, "tooltip_");
  lua_pushcfunction(L, l_scoped_group);    lua_setfield(L, -2, "group");
  lua_pushcfunction(L, l_scoped_disabled); lua_setfield(L, -2, "disabled");
  lua_setfield(L, -2, "ig");
  lua_pop(L, 1);

  // key constants (tw.ig.key = {...})
  lua_getglobal(L, "lp");
  lua_getfield(L, -1, "ig");
  lua_newtable(L);
#define KEY(name) lua_pushinteger(L, ImGuiKey_##name); lua_setfield(L, -2, #name)
  KEY(A); KEY(B); KEY(C); KEY(D); KEY(E); KEY(F); KEY(G); KEY(H); KEY(I);
  KEY(J); KEY(K); KEY(L); KEY(M); KEY(N); KEY(O); KEY(P); KEY(Q); KEY(R);
  KEY(S); KEY(T); KEY(U); KEY(V); KEY(W); KEY(X); KEY(Y); KEY(Z);
  KEY(0); KEY(1); KEY(2); KEY(3); KEY(4); KEY(5); KEY(6); KEY(7); KEY(8); KEY(9);
  KEY(F1); KEY(F2); KEY(F3); KEY(F4); KEY(F5); KEY(F6);
  KEY(F7); KEY(F8); KEY(F9); KEY(F10); KEY(F11); KEY(F12);
  KEY(Space); KEY(Enter); KEY(Escape); KEY(Tab); KEY(Delete); KEY(Backspace);
  KEY(UpArrow); KEY(DownArrow); KEY(LeftArrow); KEY(RightArrow); KEY(Home); KEY(End); KEY(PageUp);
  KEY(PageDown); KEY(Minus); KEY(Equal); KEY(LeftBracket); KEY(RightBracket);
  KEY(Semicolon); KEY(Apostrophe); KEY(Comma); KEY(Period); KEY(Slash);
  KEY(Backslash); KEY(GraveAccent);
#undef KEY
  lua_setfield(L, -2, "key");
  lua_pop(L, 2);

  // style enums (tw.ig.col / tw.ig.var / tw.ig.flags)
  lua_getglobal(L, "lp");
  lua_getfield(L, -1, "ig");
  lua_newtable(L); // slot 3: col
#define COL(name) lua_pushinteger(L, ImGuiCol_##name); lua_setfield(L, 3, #name); \
                  lua_pushinteger(L, ImGuiCol_##name); lua_setfield(L, 2, "Col_" #name)
  COL(Text); COL(TextDisabled); COL(WindowBg); COL(ChildBg); COL(PopupBg);
  COL(Border); COL(BorderShadow); COL(FrameBg); COL(FrameBgHovered);
  COL(FrameBgActive); COL(TitleBg); COL(TitleBgActive); COL(CheckMark);
  COL(SliderGrab); COL(SliderGrabActive); COL(Button); COL(ButtonHovered);
  COL(ButtonActive); COL(Header); COL(HeaderHovered); COL(HeaderActive);
  COL(Separator); COL(SeparatorHovered); COL(SeparatorActive);
  COL(ResizeGrip); COL(ResizeGripHovered); COL(ResizeGripActive);
  COL(Tab); COL(TabHovered); COL(TabActive); COL(TableHeaderBg);
  COL(TableBorderStrong); COL(TableBorderLight); COL(TableRowBg);
  COL(TableRowBgAlt); COL(TextSelectedBg); COL(DragDropTarget);
  COL(NavHighlight); COL(ModalWindowDimBg);
#undef COL
  lua_setfield(L, 2, "col");

  lua_newtable(L); // slot 3: var
#define VAR(name) lua_pushinteger(L, ImGuiStyleVar_##name); lua_setfield(L, 3, #name); \
                  lua_pushinteger(L, ImGuiStyleVar_##name); lua_setfield(L, 2, "StyleVar_" #name)
  VAR(Alpha); VAR(WindowPadding); VAR(WindowRounding); VAR(WindowBorderSize);
  VAR(ChildRounding); VAR(ChildBorderSize); VAR(PopupRounding);
  VAR(FramePadding); VAR(FrameRounding); VAR(FrameBorderSize);
  VAR(ItemSpacing); VAR(ItemInnerSpacing); VAR(IndentSpacing);
  VAR(ScrollbarSize); VAR(ScrollbarRounding); VAR(GrabMinSize);
  VAR(GrabRounding); VAR(ButtonTextAlign); VAR(SelectableTextAlign);
  VAR(SeparatorTextBorderSize); VAR(SeparatorTextAlign);
#undef VAR
  lua_setfield(L, 2, "var");

  lua_newtable(L); // slot 3: wflag
#define FLAG(name) lua_pushinteger(L, ImGuiWindowFlags_##name); lua_setfield(L, 3, #name); \
                   lua_pushinteger(L, ImGuiWindowFlags_##name); lua_setfield(L, 2, "WindowFlags_" #name)
  FLAG(NoTitleBar); FLAG(NoResize); FLAG(NoMove); FLAG(NoScrollbar);
  FLAG(NoScrollWithMouse); FLAG(NoCollapse); FLAG(NoSavedSettings);
  FLAG(NoInputs); FLAG(NoBringToFrontOnFocus); FLAG(NoBackground);
  FLAG(NoDecoration); FLAG(AlwaysAutoResize); FLAG(NoNav);
  FLAG(NoFocusOnAppearing); FLAG(MenuBar);
#undef FLAG
  lua_setfield(L, 2, "wflag");

  lua_newtable(L); // slot 3: icon
#define DEF_ICON(name, fa_code) lua_pushstring(L, fa_code); lua_setfield(L, 3, name)
  DEF_ICON("POINTER", ICON_FA_ARROW_POINTER);
  DEF_ICON("SELECT", ICON_FA_ARROW_POINTER);
  DEF_ICON("MOVE", ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT);
  DEF_ICON("GRAB", ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT);
  DEF_ICON("EXTRUDE", ICON_FA_EXPAND);
  DEF_ICON("VERTEX", ICON_FA_CIRCLE_DOT);
  DEF_ICON("EDGE", ICON_FA_SLASH);
  DEF_ICON("FACE", ICON_FA_SQUARE);
  DEF_ICON("PAINT", ICON_FA_PAINTBRUSH);
  DEF_ICON("BRUSH", ICON_FA_PAINTBRUSH);
  DEF_ICON("PALETTE", ICON_FA_PALETTE);
  DEF_ICON("IMAGE", ICON_FA_IMAGE);
  DEF_ICON("UNDO", ICON_FA_ROTATE_LEFT);
  DEF_ICON("REDO", ICON_FA_ROTATE_RIGHT);
  DEF_ICON("CUBE", ICON_FA_CUBE);
  DEF_ICON("BOX", ICON_FA_CUBE);
  DEF_ICON("CYLINDER", ICON_FA_DATABASE);
  DEF_ICON("PLUS", ICON_FA_PLUS);
  DEF_ICON("EXPORT", ICON_FA_FILE_EXPORT);
  DEF_ICON("SAVE", ICON_FA_FLOPPY_DISK);
  DEF_ICON("FOLDER", ICON_FA_FOLDER_OPEN);
  DEF_ICON("FOLDER_OPEN", ICON_FA_FOLDER_OPEN);
  DEF_ICON("SUN", ICON_FA_SUN);
  DEF_ICON("LIGHTBULB", ICON_FA_LIGHTBULB);
  DEF_ICON("TRASH", ICON_FA_TRASH_CAN);
  DEF_ICON("CLEAR", ICON_FA_TRASH_CAN);
  DEF_ICON("GRIP", ICON_FA_GRIP_LINES_VERTICAL);
  DEF_ICON("GEAR", ICON_FA_GEAR);
  DEF_ICON("SETTINGS", ICON_FA_GEAR);
  DEF_ICON("SLIDERS", ICON_FA_SLIDERS);
  DEF_ICON("EYE", ICON_FA_EYE);
  DEF_ICON("CAMERA", ICON_FA_CAMERA);
  DEF_ICON("CHECK", ICON_FA_CHECK);
  DEF_ICON("XMARK", ICON_FA_XMARK);
  DEF_ICON("SEARCH", ICON_FA_MAGNIFYING_GLASS);
  DEF_ICON("INFO", ICON_FA_CIRCLE_INFO);
  DEF_ICON("WARNING", ICON_FA_TRIANGLE_EXCLAMATION);
  DEF_ICON("QUESTION", ICON_FA_CIRCLE_QUESTION);
#undef DEF_ICON
  lua_setfield(L, 2, "icon");

  lua_pushcfunction(L, [](lua_State* L) -> int {
    ig_balance_check();
    return 0;
  });
  lua_setfield(L, 2, "balance_check");

  lua_pushvalue(L, 2);
  lua_setglobal(L, "ig");

  lua_setfield(L, 1, "ig"); // lp.ig = ig table
  lua_pop(L, 1);            // pop lp
}
