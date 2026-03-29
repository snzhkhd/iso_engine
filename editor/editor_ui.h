#pragma once
#define _CRT_SECURE_NO_WARNINGS
// iso_engine editor — lightweight UI helpers (no raygui dependency)
// ────────────────────────────────────────────────────────────────

#include "raylib.h"
#include <string>
#include <cstring>
#include <cstdio>

namespace iso_ed { namespace ui {

// ─── Colors ───────────────────────────────────

struct Theme {
    Color bg_dark      = {28, 28, 32, 255};
    Color bg_panel     = {38, 38, 45, 255};
    Color bg_item      = {50, 50, 60, 255};
    Color bg_hover     = {65, 65, 80, 255};
    Color bg_active    = {80, 120, 200, 255};
    Color bg_selected  = {60, 100, 180, 255};
    Color border       = {70, 70, 85, 255};
    Color text         = {220, 220, 225, 255};
    Color text_dim     = {140, 140, 155, 255};
    Color text_bright  = {255, 255, 255, 255};
    Color accent       = {90, 140, 230, 255};
    Color danger       = {220, 60, 60, 255};
    Color success      = {60, 180, 90, 255};
    Color warning      = {230, 180, 50, 255};
};

inline Theme theme; // global theme

// ─── Basic drawing ────────────────────────────

inline void draw_panel(Rectangle rect, Color bg = {}, Color border_col = {}) {
    if (bg.a == 0) bg = theme.bg_panel;
    if (border_col.a == 0) border_col = theme.border;
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 1.f, border_col);
}

inline void draw_label(const char* text, int x, int y, int size = 14, Color col = {}) {
    if (col.a == 0) col = theme.text;
    DrawText(text, x, y, size, col);
}

inline void draw_label_centered(const char* text, Rectangle rect, int size = 14, Color col = {}) {
    if (col.a == 0) col = theme.text;
    int tw = MeasureText(text, size);
    int tx = (int)(rect.x + (rect.width - tw) * 0.5f);
    int ty = (int)(rect.y + (rect.height - size) * 0.5f);
    DrawText(text, tx, ty, size, col);
}

// ─── Button ───────────────────────────────────

inline bool button(Rectangle rect, const char* label, bool selected = false, int font_size = 14) {
    Vector2 mp = GetMousePosition();
    bool hover = CheckCollisionPointRec(mp, rect);
    bool click = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    Color bg = selected ? theme.bg_selected : (hover ? theme.bg_hover : theme.bg_item);
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 1.f, hover ? theme.accent : theme.border);
    draw_label_centered(label, rect, font_size, hover ? theme.text_bright : theme.text);

    return click;
}

/// Small icon-style button
inline bool icon_button(Rectangle rect, const char* label, bool selected = false) {
    return button(rect, label, selected, 12);
}

// ─── Toggle button ────────────────────────────

inline bool toggle(Rectangle rect, const char* label, bool* value) {
    Vector2 mp = GetMousePosition();
    bool hover = CheckCollisionPointRec(mp, rect);

    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        *value = !(*value);

    Color bg = *value ? theme.bg_active : (hover ? theme.bg_hover : theme.bg_item);
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 1.f, *value ? theme.accent : theme.border);
    draw_label_centered(label, rect, 12, *value ? theme.text_bright : theme.text_dim);

    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// ─── Tooltip ──────────────────────────────────

inline void tooltip(const char* text) {
    Vector2 mp = GetMousePosition();
    int tw = MeasureText(text, 12) + 10;
    int th = 20;
    int tx = (int)mp.x + 16;
    int ty = (int)mp.y - 8;
    DrawRectangle(tx, ty, tw, th, {0, 0, 0, 200});
    DrawText(text, tx + 5, ty + 4, 12, theme.text);
}

// ─── Scroll area ──────────────────────────────

struct ScrollState {
    float offset   = 0.f;  // current scroll offset (pixels)
    float max_scroll = 0.f;
    bool  dragging = false;
};

/// Begin a scroll area. Returns the scissor clip rectangle.
/// content_height is the total height of content inside.
inline Rectangle begin_scroll(Rectangle rect, ScrollState& state, float content_height) {
    state.max_scroll = std::max(0.f, content_height - rect.height);

    // Mouse wheel scroll
    Vector2 mp = GetMousePosition();
    if (CheckCollisionPointRec(mp, rect)) {
        state.offset -= GetMouseWheelMove() * 40.f;
    }

    // Clamp
    if (state.offset < 0.f) state.offset = 0.f;
    if (state.offset > state.max_scroll) state.offset = state.max_scroll;

    BeginScissorMode((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height);
    return rect;
}

inline void end_scroll() {
    EndScissorMode();
}

/// Draw a minimal scrollbar on the right side
inline void draw_scrollbar(Rectangle area, const ScrollState& state, float content_height) {
    if (content_height <= area.height) return;

    float bar_h = (area.height / content_height) * area.height;
    if (bar_h < 20.f) bar_h = 20.f;
    float bar_y = area.y + (state.offset / state.max_scroll) * (area.height - bar_h);

    DrawRectangle((int)(area.x + area.width - 6), (int)bar_y, 4, (int)bar_h,
                  {100, 100, 120, 150});
}

// ─── Text input (simple, single-line) ─────────

struct TextInputState {
    char   buffer[256] = {};
    int    cursor = 0;
    bool   active = false;
    float  blink_timer = 0.f;
};

inline bool text_input(Rectangle rect, TextInputState& state) {
    Vector2 mp = GetMousePosition();
    bool hover = CheckCollisionPointRec(mp, rect);
    bool changed = false;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        state.active = hover;

    // Background
    Color bg = state.active ? Color{45, 45, 55, 255} : theme.bg_item;
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 1.f, state.active ? theme.accent : theme.border);

    // Text
    DrawText(state.buffer, (int)rect.x + 5, (int)(rect.y + (rect.height - 14) * 0.5f),
             14, theme.text);

    // Cursor blink
    if (state.active) {
        state.blink_timer += GetFrameTime();
        if ((int)(state.blink_timer * 2.f) % 2 == 0) {
            int cx = (int)rect.x + 5 + MeasureText(state.buffer, 14);
            DrawRectangle(cx, (int)rect.y + 4, 2, (int)rect.height - 8, theme.accent);
        }

        // Handle input
        int key = GetCharPressed();
        while (key > 0) {
            int len = (int)strlen(state.buffer);
            if (len < 254 && key >= 32 && key < 127) {
                state.buffer[len] = (char)key;
                state.buffer[len + 1] = '\0';
                changed = true;
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            int len = (int)strlen(state.buffer);
            if (len > 0) {
                state.buffer[len - 1] = '\0';
                changed = true;
            }
        }
    }

    return changed;
}

// ─── Dropdown ─────────────────────────────────

inline int dropdown_inline(Rectangle rect, const char** items, int item_count,
                            int selected, const char* label = nullptr)
{
    // Just draw as a button with the current selection, cycle on click
    char buf[64];
    if (label)
        snprintf(buf, sizeof(buf), "%s: %s", label, items[selected]);
    else
        snprintf(buf, sizeof(buf), "%s", items[selected]);

    if (button(rect, buf, false, 12)) {
        return (selected + 1) % item_count;
    }
    return selected;
}

// ─── Number spinner ───────────────────────────

inline bool spinner(Rectangle rect, const char* label, int* value, int min_val, int max_val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %d", label, *value);

    bool changed = false;
    float bw = 24;

    // Minus button
    Rectangle minus_r = {rect.x, rect.y, bw, rect.height};
    if (button(minus_r, "-", false, 14) && *value > min_val) {
        (*value)--;
        changed = true;
    }

    // Value display
    Rectangle val_r = {rect.x + bw, rect.y, rect.width - bw * 2, rect.height};
    draw_panel(val_r, theme.bg_item);
    draw_label_centered(buf, val_r, 12);

    // Plus button
    Rectangle plus_r = {rect.x + rect.width - bw, rect.y, bw, rect.height};
    if (button(plus_r, "+", false, 14) && *value < max_val) {
        (*value)++;
        changed = true;
    }

    return changed;
}

// ─── Confirmation dialog ──────────────────────

inline bool confirm_dialog(const char* title, const char* message, bool* open) {
    if (!*open) return false;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, {0, 0, 0, 120}); // dim background

    float dw = 360, dh = 140;
    Rectangle dialog = {(sw - dw) * 0.5f, (sh - dh) * 0.5f, dw, dh};
    draw_panel(dialog, {45, 45, 55, 255});
    draw_label(title, (int)dialog.x + 15, (int)dialog.y + 12, 16, theme.text_bright);
    draw_label(message, (int)dialog.x + 15, (int)dialog.y + 40, 14, theme.text_dim);

    bool confirmed = false;
    Rectangle ok_r = {dialog.x + dw - 160, dialog.y + dh - 40, 70, 28};
    Rectangle no_r = {dialog.x + dw - 80, dialog.y + dh - 40, 70, 28};

    if (button(ok_r, "OK")) { confirmed = true; *open = false; }
    if (button(no_r, "Cancel")) { *open = false; }
    if (IsKeyPressed(KEY_ESCAPE)) { *open = false; }
    if (IsKeyPressed(KEY_ENTER))  { confirmed = true; *open = false; }

    return confirmed;
}

// ─── Status bar message ───────────────────────

struct StatusMessage {
    char   text[256] = {};
    float  timer = 0.f;
    Color  color = {};

    void set(const char* msg, Color c = {}, float duration = 3.f) {
        strncpy(text, msg, 255);
        if (c.a == 0) c = theme.text;
        color = c;
        timer = duration;
    }

    void update(float dt) {
        if (timer > 0.f) timer -= dt;
    }

    void draw(int x, int y) {
        if (timer > 0.f) {
            float alpha = timer < 0.5f ? timer * 2.f : 1.f;
            Color c = {color.r, color.g, color.b, (unsigned char)(255 * alpha)};
            DrawText(text, x, y, 14, c);
        }
    }
};

// ─── Mouse helpers ────────────────────────────

/// Is mouse inside rect and NOT consumed by a panel above?
inline bool mouse_in(Rectangle rect) {
    return CheckCollisionPointRec(GetMousePosition(), rect);
}

}} // namespace iso_ed::ui
