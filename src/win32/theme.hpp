#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <array>
#include <cstdint>

namespace reclaim::ui {

struct ThemePalette {
    Gdiplus::Color canvas;
    Gdiplus::Color rail;
    Gdiplus::Color surface;
    Gdiplus::Color surface_hover;
    Gdiplus::Color border;
    Gdiplus::Color text_primary;
    Gdiplus::Color text_muted;
    Gdiplus::Color accent;
    Gdiplus::Color accent_hover;
    Gdiplus::Color accent_text;
    Gdiplus::Color safe_fg;
    Gdiplus::Color safe_bg;
    Gdiplus::Color review_fg;
    Gdiplus::Color review_bg;
    Gdiplus::Color caution_fg;
    Gdiplus::Color caution_bg;
    std::array<Gdiplus::Color, 6> treemap_colors;
};

inline ThemePalette make_palette(bool dark) {
    if (dark) {
        return ThemePalette{
            Gdiplus::Color(255, 13, 17, 23),     // canvas #0D1117
            Gdiplus::Color(255, 17, 22, 29),     // rail #11161D
            Gdiplus::Color(255, 22, 27, 34),     // surface #161B22
            Gdiplus::Color(255, 31, 38, 48),     // surface_hover #1F2630
            Gdiplus::Color(255, 48, 54, 61),     // border #30363D
            Gdiplus::Color(255, 230, 237, 243),  // text_primary #E6EDF3
            Gdiplus::Color(255, 139, 148, 158),  // text_muted #8B949E
            Gdiplus::Color(255, 217, 119, 6),    // accent Signal Amber #D97706
            Gdiplus::Color(255, 245, 158, 11),   // accent_hover #F59E0B
            Gdiplus::Color(255, 13, 17, 23),     // accent_text
            Gdiplus::Color(255, 86, 211, 100),   // safe_fg #56D364
            Gdiplus::Color(255, 19, 46, 28),     // safe_bg
            Gdiplus::Color(255, 227, 179, 65),   // review_fg #E3B341
            Gdiplus::Color(255, 47, 35, 14),     // review_bg
            Gdiplus::Color(255, 255, 123, 114),  // caution_fg #FF7B72
            Gdiplus::Color(255, 52, 22, 22),     // caution_bg
            {
                Gdiplus::Color(255, 217, 119, 6),   // Signal Amber
                Gdiplus::Color(255, 76, 154, 255),  // Cobalt Blue
                Gdiplus::Color(255, 57, 197, 207),  // Cyan Teal
                Gdiplus::Color(255, 86, 211, 100),  // Emerald
                Gdiplus::Color(255, 227, 179, 65),  // Ochre Gold
                Gdiplus::Color(255, 255, 123, 114)  // Coral Red
            }
        };
    }

    return ThemePalette{
        Gdiplus::Color(255, 248, 249, 250),  // canvas #F8F9FA
        Gdiplus::Color(255, 241, 243, 245),  // rail #F1F3F5
        Gdiplus::Color(255, 255, 255, 255),  // surface #FFFFFF
        Gdiplus::Color(255, 238, 241, 245),  // surface_hover
        Gdiplus::Color(255, 208, 215, 222),  // border #D0D7DE
        Gdiplus::Color(255, 31, 35, 40),     // text_primary #1F2328
        Gdiplus::Color(255, 87, 96, 106),    // text_muted #57606A
        Gdiplus::Color(255, 180, 83, 9),     // accent Copper #B45309
        Gdiplus::Color(255, 217, 119, 6),    // accent_hover #D97706
        Gdiplus::Color(255, 255, 255, 255),  // accent_text
        Gdiplus::Color(255, 26, 127, 55),    // safe_fg #1A7F37
        Gdiplus::Color(255, 218, 251, 225),  // safe_bg
        Gdiplus::Color(255, 154, 103, 0),    // review_fg #9A6700
        Gdiplus::Color(255, 255, 248, 197),  // review_bg
        Gdiplus::Color(255, 207, 34, 46),    // caution_fg #CF222E
        Gdiplus::Color(255, 255, 235, 233),  // caution_bg
        {
            Gdiplus::Color(255, 180, 83, 9),
            Gdiplus::Color(255, 9, 105, 218),
            Gdiplus::Color(255, 27, 124, 131),
            Gdiplus::Color(255, 26, 127, 55),
            Gdiplus::Color(255, 154, 103, 0),
            Gdiplus::Color(255, 207, 34, 46)
        }
    };
}

inline void apply_immersive_dark_titlebar(HWND hwnd, bool dark) {
    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &value, sizeof(value));
}

inline void fill_rounded_rect(Gdiplus::Graphics& g,
                              const Gdiplus::Brush& brush,
                              const Gdiplus::Pen* border_pen,
                              Gdiplus::RectF r,
                              float radius) {
    Gdiplus::GraphicsPath path;
    const float d = radius * 2.0f;
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
    g.FillPath(&brush, &path);
    if (border_pen) {
        g.DrawPath(border_pen, &path);
    }
}

}  // namespace reclaim::ui

#endif  // _WIN32
