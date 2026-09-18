#pragma once

#include <string>

// Runtime translation of the OptiScaler menu.
//
// The table lives in a UTF-8 text file next to the DLL rather than in the
// binary, so wording can be changed without a rebuild and without depending on
// the compiler's idea of the source charset. Each line is
//
//     English=translated
//
// Lines starting with ';' or '#' are comments. "\n", "\r", "\t" and "\\" are
// understood on both sides of the '=', so a multi-line label can be written on
// a single line. The first '=' separates the two halves; everything after it is
// part of the translation.
//
// When no file is found every lookup misses and the menu stays English, so a
// missing table is harmless.

namespace L10N
{
    // Reads the table and prepares the glyph ranges. Safe to call repeatedly;
    // only the first call touches the disk.
    void Initialize();

    // True once a table with at least one entry has been loaded.
    bool IsLoaded();

    // Looks up [text, text_end) in the table. A range that stops at a "##" ID
    // marker is accepted, because ImGui hands widget labels to the draw helpers
    // already clipped there. A key carrying "##" is matched on its display half
    // so "Reset##detail" and "Reset##colour" share one entry.
    //
    // On a hit out_text/out_end point at the translation, which outlives the
    // call. Returns false, leaving the outputs untouched, when nothing matches.
    bool Lookup(const char* text, const char* text_end, const char*& out_text, const char*& out_end);

    // Translates one whole 0-terminated literal, returning the translation when
    // there is one and the argument itself otherwise.
    //
    // The ImGui hooks only ever see text that is already assembled, so two
    // shapes of call are invisible to them and have to ask explicitly:
    //
    //   * a printf ARGUMENT -- Text("%s", cond ? "MOVES" : "flat so far")
    //     is composed before ImGui sees it, so wrap the argument: T("MOVES").
    //   * a buffer built by snprintf -- wrap the format string, and any literal
    //     argument that carries words, so the composed result is already
    //     translated: snprintf(buf, n, T("%s scan %.4f"), ..., T("   [editing]")).
    //
    // Safe on nullptr, on a literal with no entry, and before Initialize().
    const char* T(const char* text);

    // 0-terminated ImWchar range list covering every character the loaded table
    // uses, so the atlas only bakes glyphs that can actually be drawn. Latin-1
    // and general punctuation are always included for untranslated strings.
    // Returns nullptr when no table is loaded.
    const unsigned short* GlyphRanges();

    // First installed CJK font that exists, as a UTF-8 path. Empty when the
    // translation table is absent or no suitable font is installed.
    std::string SystemFontPath();

    // UTF-8 path of the table that was loaded, or the first candidate that was
    // looked for. For the log.
    const char* SourcePath();
}
