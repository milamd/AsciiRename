---
title: "feat: Rename full paths with shell-safe character sanitization"
type: feat
date: 2026-01-30
---

# feat: Rename Full Paths with Shell-Safe Character Sanitization

## Overview

Two related improvements to AsciiRename:

1. **Full path renaming**: Rename parent directories, not just filenames
2. **Shell metacharacter sanitization**: Remove/replace characters that cause shell issues (`;`, `$`, `|`, etc.)

## Problem Statement

**Current issues:**
- Parent directories with Unicode names require multiple manual runs
- Shell metacharacters in filenames cause command-line problems

**Example problematic filename:**
```
データ/report;rm -rf /.txt
```

This is both Unicode AND contains shell injection characters.

## Proposed Solution

### 1. Full Path Renaming

Process path components bottom-up (deepest first):
```
データ/東京/報告.txt  →  deta/toukyou/houkoku.txt
```

### 2. Shell Metacharacter Sanitization

Add `SanitizeForShell()` function to replace dangerous characters:

| Character | Replacement | Reason |
|-----------|-------------|--------|
| `;` | `_` | Command separator |
| `$` | `_` | Variable expansion |
| `` ` `` | `_` | Command substitution |
| `|` | `_` | Pipe |
| `&` | `_` | Background/AND |
| `>` | `_` | Redirect |
| `<` | `_` | Redirect |
| `'` | `_` | Quote |
| `"` | `_` | Quote |
| `\\` | `_` | Escape (except path sep) |
| `*` | `_` | Glob |
| `?` | `_` | Glob |
| `[` | `_` | Glob |
| `]` | `_` | Glob |
| `(` | `_` | Subshell |
| `)` | `_` | Subshell |
| `!` | `_` | History expansion |
| `~` | `_` | Home expansion |
| `#` | `_` | Comment (at start) |
| newline | `_` | Command separator |
| space | `_` | Argument separator (optional) |

## Technical Approach

### New Helper Function

**File: `src/helpers.cpp`**

```cpp
std::string SanitizeForShell(const std::string& input)
{
    static const std::string dangerous = ";$`|&><'\"\\*?[]()!~#\n\r";
    std::string result;
    result.reserve(input.length());
    
    for (char c : input) {
        if (dangerous.find(c) != std::string::npos) {
            result += '_';
        } else {
            result += c;
        }
    }
    return result;
}
```

### Integration into Rename Pipeline

```cpp
// In TryGetAscii or main rename logic:
std::string ascii = TryGetAscii(utf8Input);
std::string safe = SanitizeForShell(ascii);  // NEW
```

### Path Component Processing

```cpp
std::vector<fs::path> GetRenameableComponents(const fs::path& fullPath)
{
    std::vector<fs::path> result;
    fs::path current;
    
    for (const auto& comp : fullPath) {
        current /= comp;
        // Skip root, drive letters, . and ..
        if (comp == "/" || comp == "\\" || comp == "." || comp == ".." ||
            (comp.string().length() == 2 && comp.string()[1] == ':')) {
            continue;
        }
        result.push_back(current);
    }
    std::reverse(result.begin(), result.end());  // Bottom-up
    return result;
}
```

## Acceptance Criteria

### Full Path Renaming
- [ ] Single file renames all parent directories bottom-up
- [ ] Only argument path components renamed (not `/home/user`)
- [ ] Multiple args sharing parents work correctly
- [ ] Works with `-n`, `-v`, `-o`, `-r` flags

### Shell Sanitization
- [ ] Semicolons replaced: `a;b.txt` → `a_b.txt`
- [ ] Pipes replaced: `a|b.txt` → `a_b.txt`
- [ ] Backticks replaced: ``a`b`.txt`` → `a_b_.txt`
- [ ] Dollar signs replaced: `$HOME.txt` → `_HOME.txt`
- [ ] Multiple dangerous chars: `a;b|c.txt` → `a_b_c.txt`
- [ ] Combined with Unicode: `データ;test.txt` → `deta_test.txt`

### Error Handling
- [ ] Path too long after expansion: Error before rename
- [ ] Permission denied: Warning, continue

## Testing

### Shell Sanitization Tests
```cpp
TEST(Sanitize, Semicolon) {
    EXPECT_EQ(SanitizeForShell("a;b"), "a_b");
}
TEST(Sanitize, MultipleChars) {
    EXPECT_EQ(SanitizeForShell("a;b|c$d"), "a_b_c_d");
}
TEST(Sanitize, Backtick) {
    EXPECT_EQ(SanitizeForShell("$(whoami)"), "_(whoami)");
}
```

### Integration Tests
```bash
# Test shell-dangerous filename
touch "report;test.txt"
./ascii-rename "report;test.txt"
[ -f "report_test.txt" ] || exit 1

# Test combined Unicode + dangerous
touch "データ;報告.txt"
./ascii-rename "データ;報告.txt"
[ -f "deta_houkoku.txt" ] || exit 1
```

## References

- Current ASCII logic: `src/helpers.cpp:93-110`
- Buffer overflow learnings: `docs/solutions/security-issues/cpp-unicode-buffer-overflow-heap-corruption.md`
- Main rename loop: `src/main.cpp:123-261`
