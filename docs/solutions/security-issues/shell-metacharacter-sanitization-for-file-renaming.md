---
title: Full path renaming with shell metacharacter sanitization
created: 2026-01-30
category: security-issues
tags:
  - path-handling
  - shell-injection
  - unicode-to-ascii
  - filesystem-operations
  - sanitization
  - bottom-up-processing
symptoms:
  - Only filename renamed, parent directories with Unicode remain unchanged
  - Shell metacharacters in filenames could cause command injection
  - Paths with special characters break shell commands
  - Multiple tool runs required for nested Unicode directories
module: AsciiRename
language: cpp
severity: high
root_cause: Original implementation only processed filename component, ignoring directory path, and lacked shell metacharacter sanitization
---

# Full Path Renaming with Shell Metacharacter Sanitization

## Problem

When working with deeply nested directory structures containing Unicode characters, users faced two significant challenges:

### 1. Multiple Passes Required for Nested Directories

Renaming a path like `/data/日本語/ファイル.txt` required running the tool multiple times. If you renamed the parent directory `日本語` first, the child path would become invalid.

```bash
# User wants to rename everything in this structure:
/data/日本語/ファイル.txt

# First run renames the directory:
ascii-rename /data/日本語
# Result: /data/RiBenYu/ファイル.txt

# But original child path is now broken:
ascii-rename /data/日本語/ファイル.txt
# ERROR: "/data/日本語/ファイル.txt" doesn't exist.

# User must re-specify the updated path:
ascii-rename /data/RiBenYu/ファイル.txt
```

This forced users into tedious iterative workflows, especially with deep nesting.

### 2. Shell Metacharacters in Filenames

Unicode transliteration could produce ASCII characters that are dangerous in shell contexts. A filename like `file$var.txt` or `report;rm -rf.txt` could cause unexpected command injection or variable expansion when used in scripts or pipelines.

```bash
# A transliterated filename might contain shell metacharacters:
mv "report.txt" "report$summary.txt"  # $summary gets expanded!
ls file*.txt                           # Glob expansion
echo "data|output.txt"                 # Pipe interpretation
```

## Solution

The implementation introduces three key components that work together: bottom-up path processing, ancestor path tracking, and shell-safe character sanitization.

### Bottom-Up Processing with `GetRenameableComponents()`

Instead of renaming paths in the order provided, the tool extracts all renameable components and processes them deepest-first:

```cpp
std::vector<std::filesystem::path> GetRenameableComponents(const std::string &pathStr)
{
    std::vector<std::filesystem::path> result;
    std::filesystem::path current;

    for (const auto &component : fullPath)
    {
        auto compStr = component.string();
        
        // Skip root markers: "/", ".", "..", "C:"
        if (compStr == "/" || compStr == "\\" ||
            compStr == "." || compStr == ".." ||
            (compStr.length() == 2 && compStr[1] == ':'))
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
```

For `/data/日本語/ファイル.txt`, this returns:
1. `/data/日本語/ファイル.txt` (depth 3)
2. `/data/日本語` (depth 2)
3. `/data` (depth 1)

By renaming children before parents, the filesystem structure remains valid throughout the operation.

### Path Tracking with `PathTracker`

When processing multiple arguments that share ancestors, the `PathTracker` class maintains a record of completed renames and updates subsequent paths accordingly:

```cpp
class PathTracker
{
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> renames_;

public:
    std::filesystem::path resolve(const std::filesystem::path &original) const
    {
        std::filesystem::path result = original;
        for (const auto &[from, to] : renames_)
        {
            // If result starts with 'from', replace prefix with 'to'
            // ... path prefix matching logic ...
        }
        return result;
    }

    void record(const std::filesystem::path &from, const std::filesystem::path &to)
    {
        renames_.emplace_back(from, to);
    }
};
```

This enables commands like:
```bash
ascii-rename /data/日本語/file1.txt /data/日本語/file2.txt
```

Both files share the parent `日本語`. After renaming `file1.txt`, the tracker updates the path for `file2.txt` to reference the renamed parent directory.

### Shell-Safe Sanitization with `SanitizeForShell()`

After ASCII transliteration, filenames pass through sanitization that replaces dangerous shell metacharacters with underscores:

```cpp
std::string SanitizeForShell(const std::string &input)
{
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
```

Characters replaced include:
- Command separators: `;` `&` `|`
- Redirects: `>` `<`
- Variable/command expansion: `$` `` ` ``
- Quotes: `'` `"`
- Globs: `*` `?` `[` `]`
- Subshells and grouping: `(` `)`
- History/home expansion: `!` `~`
- Comments: `#`
- Escape and newlines: `\` `\n` `\r`

### Example: Complete Workflow

```bash
# Input structure:
/project/données/café$prix.txt
/project/données/thé&menu.txt

# Single command handles everything:
ascii-rename -r /project/données

# Processing order (bottom-up):
# 1. /project/données/café$prix.txt → /project/données/cafe_prix.txt
# 2. /project/données/thé&menu.txt  → /project/données/the_menu.txt
# 3. /project/données              → /project/donnees

# Final structure:
/project/donnees/cafe_prix.txt
/project/donnees/the_menu.txt
```

## Prevention

### Path Processing Order: Always Bottom-Up

When renaming paths that include ancestor directories, **always process children before parents**:

```
# WRONG: Top-down breaks child paths
parent/       → renamed_parent/     (children now orphaned)
parent/child  → ERROR: path doesn't exist

# CORRECT: Bottom-up preserves valid paths
parent/child  → parent/renamed_child (parent still exists)
parent/       → renamed_parent/      (now safe to rename)
```

### Path Tracking for Renamed Ancestors

When multiple paths share common ancestors that get renamed, subsequent operations must resolve the **current** location, not the original. Use a path tracker:

**When to use path tracking:**
- Multiple CLI arguments with shared parent directories
- Recursive directory processing with `-r` flag
- Any batch rename operation where order matters

**Critical**: Record renames even in `--no-op` mode to maintain accurate path resolution for reporting.

### Shell Safety for CLI Tools

Filenames containing shell metacharacters create security and usability risks:

| Character | Risk |
|-----------|------|
| `; $ `` ` | Command injection |
| `\| & > <` | Pipeline/redirect hijacking |
| `' " \` | Quote breaking |
| `* ? [ ]` | Glob expansion |
| `( ) ! ~` | Shell expansion |
| `# \n \r` | Comment injection, line splitting |

**Best practices:**
1. Sanitize **after** Unicode transliteration (transliteration may introduce shell chars)
2. Use underscore replacement (preserves filename readability)
3. Apply to filename component only, not the full path
4. Consider filesystem-specific reserved characters (Windows: `< > : " / \ | ? *`)

## Testing

### Test Cases

#### 1. Deep Nesting (Multiple Directory Levels)

```bash
mkdir -p "tëst/nësted/dëep/fïle"
touch "tëst/nësted/dëep/fïle/dôc.txt"
ascii-rename -r "tëst"
# Verify: All 5 levels renamed bottom-up
ls -la test/nested/deep/file/doc.txt
```

#### 2. Multiple Shell Metacharacters

```bash
touch "file;name\$with\`pipes|and&more"
ascii-rename "file;name\$with\`pipes|and&more"
# Verify
ls file_name_with_pipes_and_more
```

#### 3. Mixed Unicode + Shell Characters

```bash
mkdir "Ünïcödé;injection\$path"
touch "Ünïcödé;injection\$path/café|piped.txt"
ascii-rename -r "Ünïcödé;injection\$path"
# Verify: Both Unicode transliteration AND shell sanitization applied
ls -la Unicode_injection_path/cafe_piped.txt
```

#### 4. Idempotency (Running Twice)

```bash
mkdir "tëst_dir" && touch "tëst_dir/fïle.txt"
ascii-rename -r "tëst_dir"
ascii-rename -r -v "test_dir"  # Should report "no need to rename"
```

#### 5. Multiple Arguments Sharing Parents

```bash
mkdir "pärënt"
touch "pärënt/fïle1.txt" "pärënt/fïle2.txt"
ascii-rename "pärënt/fïle1.txt" "pärënt/fïle2.txt"
# Verify: Parent renamed once, both files renamed
ls parent/file1.txt parent/file2.txt
```

## Related Links

- [Buffer overflow fix documentation](cpp-unicode-buffer-overflow-heap-corruption.md) - Related memory safety issue
- [Feature plan](../../plans/2026-01-30-feat-rename-full-path-directories-plan.md) - Original planning document
- [AnyAscii library](https://github.com/anyascii/anyascii) - Upstream transliteration library

## Commit References

- `a07e475` - feat: rename full paths with shell-safe character sanitization
- `5d10919` - docs: add feature plan and solution documentation
- `46b5afb` - update for malloc problem (related buffer safety fix)
