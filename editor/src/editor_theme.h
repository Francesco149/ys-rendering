// editor_theme.h — Shared ImGui theme, fonts, and embedded FontAwesome 6 icon atlas
#pragma once

#include <imgui.h>
#include "fa6/IconsFontAwesome6.h"
#include "fa6/FA6FreeSolidFontData.h"

inline void apply_modern_dark_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 4.0f;
    s.PopupRounding     = 6.0f;
    s.ScrollbarRounding = 4.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 4.0f;
    s.WindowPadding     = ImVec2(8.0f, 8.0f);
    s.FramePadding      = ImVec2(6.0f, 4.0f);
    s.ItemSpacing       = ImVec2(6.0f, 5.0f);
    s.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    s.IndentSpacing     = 14.0f;
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 8.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.11f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.16f, 0.19f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.21f, 0.23f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.28f, 0.33f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.96f, 0.62f, 0.04f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.96f, 0.62f, 0.04f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(1.00f, 0.78f, 0.55f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.24f, 0.27f, 0.33f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.20f, 0.23f, 0.28f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.25f, 0.29f, 0.35f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.28f, 0.32f, 0.40f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.18f, 0.19f, 0.22f, 1.00f);
    c[ImGuiCol_Text]                 = ImVec4(0.90f, 0.90f, 0.93f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.52f, 0.58f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
}

inline void build_imgui_font_atlas() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.PixelSnapH = true;

    // 1. Primary: Latin + Cyrillic + Greek + Common Symbols (Inter)
    static const ImWchar latin_cyr[] = {
        0x0020, 0x00FF,   // ASCII + Latin-1
        0x0100, 0x017F,   // Latin Extended-A
        0x0180, 0x024F,   // Latin Extended-B
        0x1E00, 0x1EFF,   // Latin Extended Additional (Vietnamese)
        0x0400, 0x052F,   // Cyrillic + Cyrillic Supplement
        0x0370, 0x03FF,   // Greek + Coptic
        0x2000, 0x206F,   // General Punctuation
        0x20AC, 0x20AC,   // €
        0x2190, 0x21FF,   // Arrows
        0x2500, 0x25FF,   // Box Drawing / Block Elements
        0,
    };

    std::string latin_path = app_paths::resolve_asset("fonts/InterVariable.ttf", "FONT_LATIN");
    ImFont* main_font = nullptr;
    if (app_paths::file_exists(latin_path)) {
        main_font = io.Fonts->AddFontFromFileTTF(latin_path.c_str(), 16.0f, &cfg, latin_cyr);
    }
    if (!main_font) {
        main_font = io.Fonts->AddFontDefault(&cfg);
    }
    if (main_font) io.FontDefault = main_font;

    // 2. Fallback: CJK (Han + Kana + fullwidth forms) merged into the primary font
    static const ImWchar cjk_ranges[] = {
        0x3000, 0x30FF,   // CJK Symbols/Punctuation, Hiragana, Katakana
        0x31F0, 0x31FF,   // Katakana Phonetic Extensions
        0x3400, 0x4DBF,   // CJK Extension A
        0x4E00, 0x9FFF,   // CJK Unified Ideographs
        0xF900, 0xFAFF,   // CJK Compatibility Ideographs
        0xFF00, 0xFFEF,   // Fullwidth Forms
        0,
    };
    std::string cjk_path = app_paths::resolve_asset("fonts/ipag.ttf", "FONT_CJK");
    if (app_paths::file_exists(cjk_path)) {
        ImFontConfig mcfg;
        mcfg.MergeMode = true;
        mcfg.GlyphMinAdvanceX = 16.0f;
        io.Fonts->AddFontFromFileTTF(cjk_path.c_str(), 16.0f, &mcfg, cjk_ranges);
    }

    // 3. Fallback: Embedded FontAwesome 6 Solid icons merged into the primary font
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig icfg;
    icfg.MergeMode = true;
    icfg.PixelSnapH = true;
    icfg.GlyphMinAdvanceX = 14.0f;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        fa_solid_900_compressed_data,
        fa_solid_900_compressed_size,
        14.0f,
        &icfg,
        icons_ranges
    );
}
