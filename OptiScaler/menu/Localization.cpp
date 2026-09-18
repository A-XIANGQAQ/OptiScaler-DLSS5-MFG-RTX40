#include "pch.h"
#include "Localization.h"

#include "Util.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    // Transparent hash so a lookup can pass a string_view without building a
    // temporary string on every text draw.
    struct StringHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view v) const { return std::hash<std::string_view> {}(v); }
        size_t operator()(const std::string& s) const { return std::hash<std::string_view> {}(s); }
    };

    struct StringEqual
    {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const { return a == b; }
    };

    std::unordered_map<std::string, std::string, StringHash, StringEqual> g_table;
    std::vector<unsigned short> g_ranges;
    std::string g_sourcePath;
    bool g_attempted = false;

    void Trim(std::string& s)
    {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
            s.pop_back();

        const size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos)
            s.clear();
        else if (start > 0)
            s.erase(0, start);
    }

    // Turns the "\n", "\r", "\t", "\s" and "\\" escapes the file uses into the
    // bytes they stand for. The English side needs them as much as the
    // translation does: a label written as "...\nmodel." in the sources holds a
    // real newline at runtime, and the key has to match that.
    //
    // "\s" exists because a few labels carry a significant leading or trailing
    // space (" natively on Vulkan", "...FSR-FG "). Trimming would destroy those,
    // so they are written escaped and pass through untouched.
    std::string Unescape(std::string_view raw)
    {
        std::string out;
        out.reserve(raw.size());

        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == '\\' && i + 1 < raw.size())
            {
                switch (raw[i + 1])
                {
                case 'n': out.push_back('\n'); ++i; continue;
                case 'r': out.push_back('\r'); ++i; continue;
                case 't': out.push_back('\t'); ++i; continue;
                case 's': out.push_back(' '); ++i; continue;
                case '=': out.push_back('='); ++i; continue;
                case '\\': out.push_back('\\'); ++i; continue;
                default: break;
                }
            }
            out.push_back(raw[i]);
        }

        return out;
    }

    // UTF-8 to code points. Malformed bytes are skipped so a partly broken file
    // still contributes the entries that are intact.
    void CollectCodePoints(std::string_view text, std::vector<unsigned int>& out)
    {
        size_t i = 0;
        while (i < text.size())
        {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            unsigned int cp = 0;
            size_t length = 0;

            if (lead < 0x80) { cp = lead; length = 1; }
            else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1Fu; length = 2; }
            else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0Fu; length = 3; }
            else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07u; length = 4; }
            else { ++i; continue; }

            if (i + length > text.size())
                break;

            bool valid = true;
            for (size_t k = 1; k < length; ++k)
            {
                const unsigned char cont = static_cast<unsigned char>(text[i + k]);
                if ((cont & 0xC0) != 0x80) { valid = false; break; }
                cp = (cp << 6) | (cont & 0x3Fu);
            }

            if (!valid) { ++i; continue; }

            out.push_back(cp);
            i += length;
        }
    }

    // One range per run of code points, plus the Latin and punctuation blocks
    // that untranslated strings still need.
    void BuildGlyphRanges()
    {
        std::vector<unsigned int> points;
        for (const auto& entry : g_table)
        {
            CollectCodePoints(entry.first, points);
            CollectCodePoints(entry.second, points);
        }

        static const unsigned int always[] = { 0x0020, 0x00FF, 0x2000, 0x206F, 0x2190, 0x21FF, 0x25A0, 0x25FF };
        points.insert(points.end(), std::begin(always), std::end(always));

        std::sort(points.begin(), points.end());
        points.erase(std::unique(points.begin(), points.end()), points.end());

        g_ranges.clear();
        size_t i = 0;
        while (i < points.size())
        {
            const unsigned int first = points[i];
            unsigned int last = first;
            while (i + 1 < points.size() && points[i + 1] <= last + 1)
            {
                ++i;
                last = points[i];
            }
            g_ranges.push_back(static_cast<unsigned short>(first));
            g_ranges.push_back(static_cast<unsigned short>(last));
            ++i;
        }
        g_ranges.push_back(0);
    }

    bool ParseFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            // Strip a UTF-8 BOM from the first line of the file.
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);

            Trim(line);

            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;

            // The separator is the first '=' that is not escaped. Several help
            // texts contain a plain '=' of their own ("1080 / 1.5 = 720"), so
            // splitting on the first one outright would cut the key in half and
            // the line would silently never match. Write "\=" for an '=' that
            // belongs to the text.
            size_t separator = std::string::npos;
            for (size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '\\')
                {
                    ++i;      // the escaped character cannot be the separator
                    continue;
                }
                if (line[i] == '=')
                {
                    separator = i;
                    break;
                }
            }

            if (separator == std::string::npos)
                continue;

            // Trim before unescaping, never after: "\s" is how a significant
            // leading or trailing space is written, and trimming the decoded
            // text would eat exactly the character it was meant to protect.
            std::string rawKey = line.substr(0, separator);
            std::string rawValue = line.substr(separator + 1);
            Trim(rawKey);
            Trim(rawValue);

            std::string key = Unescape(rawKey);
            std::string value = Unescape(rawValue);

            if (key.empty() || value.empty())
                continue;

            // A key carrying an ID marker is indexed on its display half, so
            // "Reset##detail" and "Reset" end up as one entry.
            const size_t marker = key.find("##");
            if (marker != std::string::npos)
            {
                key.erase(marker);
                Trim(key);
                if (key.empty())
                    continue;
            }

            g_table[key] = std::move(value);
        }

        return !g_table.empty();
    }
}

void L10N::Initialize()
{
    if (g_attempted)
        return;

    g_attempted = true;

    std::error_code ec;
    const std::filesystem::path dllDir = Util::DllPath().parent_path();

    const std::filesystem::path candidates[] = {
        dllDir / L"zh-CN.txt",
        dllDir / L"OptiScaler" / L"language" / L"zh-CN.txt",
        dllDir / L"OptiScaler" / L"zh-CN.txt",
    };

    for (const auto& candidate : candidates)
    {
        if (g_sourcePath.empty())
            g_sourcePath = wstring_to_string(candidate.wstring());

        if (!std::filesystem::exists(candidate, ec))
            continue;

        if (ParseFile(candidate))
        {
            g_sourcePath = wstring_to_string(candidate.wstring());
            BuildGlyphRanges();
            LOG_INFO("Localization: {} entries loaded from {}", g_table.size(), g_sourcePath);
            return;
        }
    }

    g_table.clear();
    LOG_WARN("Localization: no translation table found (looked for {})", g_sourcePath);
}

bool L10N::IsLoaded()
{
    if (!g_attempted)
        Initialize();

    return !g_table.empty();
}

bool L10N::Lookup(const char* text, const char* text_end, const char*& out_text, const char*& out_end)
{
    if (text == nullptr)
        return false;

    if (!g_attempted)
        Initialize();

    if (g_table.empty())
        return false;

    if (text_end == nullptr)
        text_end = text + std::strlen(text);

    // ImGui clips a widget label at its "##" ID marker before drawing it, so a
    // range that ends there is still a complete label. Anything else is a
    // fragment (word wrap, ellipsis) and must be left alone.
    if (*text_end != '\0' && !(text_end[0] == '#' && text_end[1] == '#'))
        return false;

    std::string_view key(text, static_cast<size_t>(text_end - text));

    const size_t marker = key.find("##");
    if (marker != std::string_view::npos)
        key = key.substr(0, marker);

    if (key.empty())
        return false;

    const auto it = g_table.find(key);
    if (it == g_table.end())
        return false;

    out_text = it->second.data();
    out_end = it->second.data() + it->second.size();
    return true;
}

const char* L10N::T(const char* text)
{
    if (text == nullptr)
        return text;

    if (!g_attempted)
        Initialize();

    if (g_table.empty())
        return text;

    const auto it = g_table.find(std::string_view(text));
    return it == g_table.end() ? text : it->second.c_str();
}

const unsigned short* L10N::GlyphRanges()
{
    if (!g_attempted)
        Initialize();

    return g_ranges.empty() ? nullptr : g_ranges.data();
}

std::string L10N::SystemFontPath()
{
    if (!IsLoaded())
        return {};

    wchar_t windowsDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0)
        return {};

    const std::filesystem::path fontDir = std::filesystem::path(windowsDir) / L"Fonts";

    // Ordered by how well each face holds up at menu size: a modern UI font
    // first, then the ones that are merely legible.
    static const wchar_t* candidates[] = {
        L"msyh.ttc",   // Microsoft YaHei
        L"msyhl.ttc",
        L"Deng.ttf",   // DengXian
        L"simhei.ttf", // SimHei
        L"simsun.ttc", // SimSun
    };

    std::error_code ec;
    for (const wchar_t* name : candidates)
    {
        const std::filesystem::path path = fontDir / name;
        if (std::filesystem::exists(path, ec))
            return wstring_to_string(path.wstring());
    }

    LOG_WARN("Localization: no CJK font found in {}", wstring_to_string(fontDir.wstring()));
    return {};
}

const char* L10N::SourcePath()
{
    return g_sourcePath.c_str();
}
