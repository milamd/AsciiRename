// Copyright (c) Jon Thysell <http://jonthysell.com>
// Licensed under the MIT License.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdint.h>
#include <vector>

#include <filesystem>

#include <anyascii.h>
#include <libpu8.h>
#include <utf8.h>

#include "helpers.h"

#ifndef _WIN32
#define u8narrow(X) std::string(X)
#endif

namespace AsciiRename
{

void TrimTrailingPathSeparator(
#ifdef _WIN32
    std::wstring &s
#else
    std::string &s
#endif
)
{
    if (s.length() > 1 && (s.back() == '\\' || s.back() == '/'))
    {
        while ((s.back() == '\\' || s.back() == '/'))
        {
#ifdef _WIN32
            auto path = std::filesystem::path(s);
            if (path.has_root_path() && path.root_path().wstring() == s)
            {
                break;
            }
#endif
            s.pop_back();
        }
    }
}

bool TryGetUtf8(
#ifdef _WIN32
    std::wstring const &input,
#else
    std::string const &input,
#endif
    std::string &output)
{
    try
    {
        output = u8narrow(input);
        return true;
    }
    catch (...)
    {
        output = std::string();
        return false;
    }
}

// Adapted from https://github.com/anyascii/anyascii/blob/0.3.1/impl/c/test.c
static void anyascii_string(const char *in, char *out)
{
    uint32_t utf32;
    uint32_t state = 0;
    size_t rlen;
    for (; *in; in++)
    {
        utf8_decode(&state, &utf32, (unsigned char)*in);
        switch (state)
        {
        case UTF8_ACCEPT:;
            const char *r;
            rlen = anyascii(utf32, &r);
            memcpy(out, r, rlen);
            out += rlen;
            break;
        case UTF8_REJECT:
            state = UTF8_ACCEPT;
            break;
        }
    }
    *out = 0;
}

bool TryGetAscii(std::string const &utf8Input, std::string &output)
{
    // Allocate buffer with 4x input size to handle worst-case Unicode expansion
    // (some Unicode characters expand to multiple ASCII characters during transliteration)
    // +1 for null terminator
    std::vector<char> buffer(utf8Input.length() * 4 + 1);
    try
    {
        anyascii_string(utf8Input.c_str(), buffer.data());
        output = std::string(buffer.data());
        return true;
    }
    catch (...)
    {
        output = std::string();
        return false;
    }
}

std::string SanitizeForShell(const std::string &input)
{
    // Characters that are dangerous in shell contexts:
    // ; $ ` | & > < ' " \ * ? [ ] ( ) ! ~ # and newlines
    static const std::string dangerous = ";$`|&><'\"\\*?[]()!~#\n\r";
    std::string result;
    result.reserve(input.length());

    for (char c : input)
    {
        if (dangerous.find(c) != std::string::npos)
        {
            result += '_';
        }
        else
        {
            result += c;
        }
    }
    return result;
}

std::vector<std::filesystem::path> GetRenameableComponents(
#ifdef _WIN32
    const std::wstring &pathStr
#else
    const std::string &pathStr
#endif
)
{
    std::vector<std::filesystem::path> result;
    std::filesystem::path fullPath(pathStr);
    std::filesystem::path current;

    for (const auto &component : fullPath)
    {
        auto compStr = component.string();

        // Skip root directory markers
        if (compStr == "/" || compStr == "\\")
        {
            current /= component;
            continue;
        }

        // Skip relative path markers
        if (compStr == "." || compStr == "..")
        {
            current /= component;
            continue;
        }

        // Skip Windows drive letters (e.g., "C:")
        if (compStr.length() == 2 && compStr[1] == ':')
        {
            current /= component;
            continue;
        }

        current /= component;
        result.push_back(current);
    }

    // Reverse for bottom-up order (deepest paths first)
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace AsciiRename
