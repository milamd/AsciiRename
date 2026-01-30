// Copyright (c) Jon Thysell <http://jonthysell.com>
// Licensed under the MIT License.

#ifndef HELPERS_H
#define HELPERS_H

#include <filesystem>
#include <string>
#include <vector>

namespace AsciiRename
{

void TrimTrailingPathSeparator(
#ifdef _WIN32
    std::wstring &s
#else
    std::string &s
#endif
);

bool TryGetUtf8(
#ifdef _WIN32
    std::wstring const &input,
#else
    std::string const &input,
#endif
    std::string &output);

bool TryGetAscii(std::string const &utf8Input, std::string &output);

// Sanitize a string by replacing shell metacharacters with underscores
// Handles: ; $ ` | & > < ' " \ * ? [ ] ( ) ! ~ # and newlines
std::string SanitizeForShell(const std::string &input);

// Extract path components that should be renamed, in bottom-up order
// (deepest components first). Skips root directories, drive letters, and . / ..
std::vector<std::filesystem::path> GetRenameableComponents(
#ifdef _WIN32
    const std::wstring &pathStr
#else
    const std::string &pathStr
#endif
);

} // namespace AsciiRename

#endif