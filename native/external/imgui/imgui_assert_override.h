#pragma once

// Purpose: override IM_ASSERT with runtime control.
// - When g_imgui_disable_asserts == true: log and continue (no modal boxes).
// - When g_imgui_disable_asserts == false: forward to C assert to show dialogs.

#include <cstdio>
#include <cstring>
#include <assert.h>

// Global runtime switch (defined in application)
extern bool g_imgui_disable_asserts;

// Lightweight repeat suppression per call-site (file:line) for logging path
static inline void ImGuiAssertOverride(const char* file, int line, const char* expr) {
    static const char* s_last_file = nullptr;
    static int         s_last_line = 0;
    static int         s_repeat    = 0;

    auto print_idstack_hint = [&](const char* tag) {
        std::printf("WARNING: %s, continuing execution\n", tag);
    };

    bool is_idstack_issue = (std::strstr(expr, "PushID/PopID") != nullptr) ||
                            (std::strstr(expr, "SizeOfIDStack") != nullptr) ||
                            (std::strstr(expr, "TreeNode/TreePop") != nullptr);

    if (s_last_file && std::strcmp(file, s_last_file) == 0 && line == s_last_line) {
        if (s_repeat < 3) {
            if (is_idstack_issue) print_idstack_hint("IDStack assertion failed");
            std::printf("ASSERTION FAILED (repeat %d): %s:%d %s\n", s_repeat + 1, file, line, expr);
        } else if (s_repeat == 3) {
            if (is_idstack_issue) print_idstack_hint("IDStack assertion failed");
            std::printf("ASSERTION FAILED: %s:%d %s (suppressing further repeats)\n", file, line, expr);
        }
        s_repeat++;
        return;
    }

    s_last_file = file;
    s_last_line = line;
    s_repeat    = 0;

    if (is_idstack_issue) print_idstack_hint("IDStack assertion failed");
    std::printf("ASSERTION FAILED: %s:%d %s\n", file, line, expr);
}

#undef IM_ASSERT
#define IM_ASSERT(_EXPR) do { \
    if (!(_EXPR)) { \
        if (g_imgui_disable_asserts) { \
            ImGuiAssertOverride(__FILE__, __LINE__, #_EXPR); \
        } else { \
            /* Forward to C assert so system dialog/break triggers */ \
            assert(_EXPR); \
        } \
    } \
} while (0)
