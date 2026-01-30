// Copyright (c) Jon Thysell <http://jonthysell.com>
// Licensed under the MIT License.

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

#include <libpu8.h>

#include "helpers.h"

#ifndef VERSION_STR
#define VERSION_STR "0.0.0"
#endif

#ifdef _WIN32
#define L(s) L##s
#define ArgEquals(X, Y, Z) (X == L(Y) || X == L(Z))
#define ArgStartsWith(X, Y) (X.rfind(L(Y)) == 0)
#else
#define ArgEquals(X, Y, Z) (X == Y || X == Z)
#define ArgStartsWith(X, Y) (X.rfind(Y) == 0)
#endif

void ShowVersion()
{
    std::cout << "ascii-rename " << VERSION_STR << "\n";
}

void ShowHelp()
{
    std::cout << "Usage: ascii-rename [options...] [paths...]\n";
    std::cout << "-h, --help       Show this help and exit\n";
    std::cout << "-n, --no-op      Show what would happen but don't actually rename path(s)\n";
    std::cout << "-o, --overwrite  Overwrite existing paths(s)\n";
    std::cout << "-r, --recursive  Rename files and subdirectories recursively\n";
    std::cout << "-v, --verbose    Make the output more verbose\n";
    std::cout << "-V, --version    Show version number and exit\n";
}

struct PathItem
{
#ifdef _WIN32
    std::wstring Path;
#else
    std::string Path;
#endif
    bool SubsScanned;
};

// Tracks renamed paths so we can resolve paths that reference renamed ancestors
class PathTracker
{
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> renames_;

public:
    // Resolve a path by applying all recorded renames to its ancestors
    std::filesystem::path resolve(const std::filesystem::path &original) const
    {
        std::filesystem::path result = original;
        for (const auto &[from, to] : renames_)
        {
            // Check if 'result' starts with 'from'
            auto resultIt = result.begin();
            auto fromIt = from.begin();
            bool matches = true;

            while (fromIt != from.end())
            {
                if (resultIt == result.end() || *resultIt != *fromIt)
                {
                    matches = false;
                    break;
                }
                ++resultIt;
                ++fromIt;
            }

            if (matches)
            {
                // Replace the 'from' prefix with 'to'
                std::filesystem::path updated = to;
                while (resultIt != result.end())
                {
                    updated /= *resultIt;
                    ++resultIt;
                }
                result = updated;
            }
        }
        return result;
    }

    void record(const std::filesystem::path &from, const std::filesystem::path &to)
    {
        renames_.emplace_back(from, to);
    }
};

// Represents a single rename operation
struct RenameOp
{
    std::filesystem::path sourcePath;
    int depth; // For sorting - deeper paths first

    bool operator<(const RenameOp &other) const
    {
        // Sort by depth descending (deeper paths first)
        return depth > other.depth;
    }

    bool operator==(const RenameOp &other) const
    {
        return sourcePath == other.sourcePath;
    }
};

int main_utf8(int argc, char **argv)
{
    if (argc <= 1)
    {
        std::cout << "ascii-rename: try \'ascii-rename --help\' for more information\n";
        return 0;
    }

    // Process arguments
    auto pathItems = std::list<PathItem>();

    // Options
    bool noop = false;
    bool overwrite = false;
    bool recursive = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i)
    {
        const auto arg = u8widen(argv[i]);

        if (ArgEquals(arg, "-h", "--help"))
        {
            ShowHelp();
            return 0;
        }
        else if (ArgEquals(arg, "-V", "--version"))
        {
            ShowVersion();
            return 0;
        }
        else if (ArgEquals(arg, "-n", "--no-op"))
        {
            noop = true;
        }
        else if (ArgEquals(arg, "-o", "--overwrite"))
        {
            overwrite = true;
        }
        else if (ArgEquals(arg, "-r", "--recursive"))
        {
            recursive = true;
        }
        else if (ArgEquals(arg, "-v", "--verbose"))
        {
            verbose = true;
        }
        else if (ArgStartsWith(arg, "-"))
        {
            auto argStr = std::string();
            if (AsciiRename::TryGetUtf8(arg, argStr))
            {
                std::cerr << "ERROR: \"" << argStr << "\" option not recognized.";
            }
            else
            {
                std::cerr << "ERROR: Option not recognized.";
            }
            std::cerr << " Run with --help for usage info.\n";
            return -1;
        }
        else
        {
            pathItems.push_back({arg, false});
        }
    }

    // Collect all rename operations from all path arguments
    // This includes parent directories that need renaming
    std::vector<RenameOp> allOps;

    // First pass: expand recursive directories and collect all paths
    while (!pathItems.empty())
    {
        auto rawItem = pathItems.front();
        pathItems.pop_front();

        AsciiRename::TrimTrailingPathSeparator(rawItem.Path);

        auto originalPath = std::filesystem::path(rawItem.Path);

        if (!std::filesystem::exists(originalPath))
        {
            auto pathStr = std::string();
            AsciiRename::TryGetUtf8(rawItem.Path, pathStr);
            std::cerr << "ERROR: \"" << pathStr << "\" doesn't exist.\n";
            continue;
        }

        // Handle recursive directory expansion
        if (std::filesystem::is_directory(originalPath) && recursive && !rawItem.SubsScanned)
        {
            // Re-add directory with SubsScanned=true, then add children
            pathItems.push_front({rawItem.Path, true});

            for (const auto &child : std::filesystem::directory_iterator(originalPath))
            {
                pathItems.push_front({
#ifdef _WIN32
                    child.path().wstring(),
#else
                    child.path().string(),
#endif
                    false});
            }
            continue;
        }

        // Get all renameable path components (in bottom-up order)
        auto components = AsciiRename::GetRenameableComponents(rawItem.Path);

        for (size_t i = 0; i < components.size(); ++i)
        {
            // Depth is inverse of position (first in list = deepest = highest depth value)
            int depth = static_cast<int>(components.size() - i);
            allOps.push_back({components[i], depth});
        }
    }

    // Sort by depth descending (deepest paths processed first)
    std::sort(allOps.begin(), allOps.end());

    // Remove duplicates (keep first occurrence of each source path)
    auto last = std::unique(allOps.begin(), allOps.end());
    allOps.erase(last, allOps.end());

    if (verbose)
    {
        std::cout << "Collected " << allOps.size() << " path components to process.\n";
    }

    // Process all rename operations with path tracking
    PathTracker tracker;
    int renames = 0;
    int skipped = 0;

    for (auto &op : allOps)
    {
        // Resolve the current path (may have been affected by earlier renames)
        auto currentPath = tracker.resolve(op.sourcePath);

        auto currentPathStr = std::string();
        AsciiRename::TryGetUtf8(
#ifdef _WIN32
            currentPath.wstring(),
#else
            currentPath.string(),
#endif
            currentPathStr);

        if (verbose)
        {
            std::cout << "Processing \"" << currentPathStr << "\"...\n";
        }

        // Check if path still exists
        if (!std::filesystem::exists(currentPath))
        {
            if (verbose)
            {
                std::cout << "Path no longer exists, skipping \"" << currentPathStr << "\"...\n";
            }
            continue;
        }

        // Get ASCII + sanitized version of the filename only
        auto filenameStr = std::string();
        AsciiRename::TryGetUtf8(
#ifdef _WIN32
            currentPath.filename().wstring(),
#else
            currentPath.filename().string(),
#endif
            filenameStr);

        auto asciiFilename = std::string();
        if (!AsciiRename::TryGetAscii(filenameStr, asciiFilename))
        {
            std::cerr << "ERROR: Unable convert \"" << filenameStr << "\" to ASCII, skipping.\n";
            ++skipped;
            continue;
        }

        // Apply shell sanitization
        asciiFilename = AsciiRename::SanitizeForShell(asciiFilename);

        // Construct new path
        auto newPath = currentPath.parent_path() / asciiFilename;

        auto newPathStr = std::string();
        AsciiRename::TryGetUtf8(
#ifdef _WIN32
            newPath.wstring(),
#else
            newPath.string(),
#endif
            newPathStr);

        // Check if rename is needed
        if (currentPathStr == newPathStr)
        {
            if (verbose)
            {
                std::cout << "No need to rename \"" << currentPathStr << "\".\n";
            }
            continue;
        }

        // Check for collision
        if (std::filesystem::exists(newPath) && !overwrite)
        {
            // Special case: if source and dest are the same (case-insensitive match on some filesystems)
            // we should still allow the rename
            if (!std::filesystem::equivalent(currentPath, newPath))
            {
                std::cerr << "ERROR: \"" << newPathStr << "\" already exists.\n";
                std::cerr << "ERROR: Specify --overwrite to overwrite.\n";
                ++skipped;
                continue;
            }
        }

        // Perform the rename
        if (noop)
        {
            std::cout << "Would have renamed \"" << currentPathStr << "\" to \"" << newPathStr << "\"...\n";
            ++renames;
            // Record the rename for path resolution even in no-op mode
            tracker.record(currentPath, newPath);
        }
        else
        {
            std::cout << "Renaming \"" << currentPathStr << "\" to \"" << newPathStr << "\"...\n";
            try
            {
                std::filesystem::rename(currentPath, newPath);
                ++renames;
                // Record the rename for path resolution
                tracker.record(currentPath, newPath);
            }
            catch (std::filesystem::filesystem_error &e)
            {
                std::cerr << "ERROR: File system error, unable to rename \"" << currentPathStr << "\" to \""
                          << newPathStr << "\".\n";
                ++skipped;
            }
        }
    }

    if (verbose)
    {
        std::cout << "Renamed: " << renames << ", Skipped: " << skipped << ", Total: " << renames + skipped << "\n";
    }

    return skipped;
}
