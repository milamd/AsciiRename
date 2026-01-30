---
title: Buffer overflow in Unicode-to-ASCII transliteration due to undersized buffer
created: 2026-01-30
category: runtime-errors
tags:
  - memory-management
  - buffer-overflow
  - heap-corruption
  - unicode
  - transliteration
  - c++
  - raii
  - std-vector
symptoms:
  - heap corruption during file rename operations
  - crashes when processing CJK characters (Chinese, Japanese, Korean)
  - undefined behavior with non-ASCII filenames
  - intermittent segfaults during transliteration
module: helpers
language: cpp
severity: critical
root_cause: Buffer allocated to input string length instead of accounting for multi-character ASCII expansion of Unicode codepoints
---

# Buffer Overflow in Unicode-to-ASCII Transliteration

## Problem

The `TryGetAscii` function in `src/helpers.cpp` was causing heap corruption when transliterating Unicode filenames to ASCII. The bug manifested as random crashes, corrupted memory, and unpredictable behavior—classic symptoms of a buffer overflow.

**Root cause:** The buffer was allocated based on the UTF-8 input length, but Unicode-to-ASCII transliteration can expand characters significantly. The `anyascii` library converts Unicode code points to their ASCII equivalents, which often requires multiple characters.

**Example:** A Japanese filename like `東京.txt` (6 bytes in UTF-8) transliterates to `dong jing.txt` (13 characters)—more than double the original size. Kanji characters routinely expand to their romanized pronunciations:

| Unicode | ASCII Transliteration | Expansion |
|---------|----------------------|-----------|
| 東 | dong | 1 → 4 |
| 京 | jing | 1 → 4 |
| ß | ss | 1 → 2 |
| ﬁ | fi | 1 → 2 |

**Additional bugs in the original code:**
- Memory leak: `delete buffer` used instead of `delete[] buffer`
- Undefined behavior: `output = nullptr` assigns a null pointer to `std::string`
- No null terminator space allocated in the buffer

### Original Buggy Code

```cpp
bool TryGetAscii(std::string const &utf8Input, std::string &output)
{
    char *buffer = new char[utf8Input.length()];
    try
    {
        anyascii_string(utf8Input.c_str(), buffer);
        output = std::string(buffer);
        return true;
    }
    catch (...)
    {
        output = nullptr;
        delete buffer;
        return false;
    }
}
```

## Solution

```cpp
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
```

**Key fixes:**

1. **4x buffer multiplier:** The worst-case expansion for `anyascii` is approximately 4x (some CJK characters transliterate to 4+ character romanizations). This ensures the buffer can handle any input without overflow.

2. **`std::vector<char>` instead of raw `new[]`:** Provides RAII semantics—the buffer is automatically deallocated when the function exits, whether normally or via exception. This eliminates both the memory leak and the need for manual `delete[]`.

3. **`output = std::string()` instead of `nullptr`:** The original code attempted to assign a null pointer to a `std::string`, which is undefined behavior. An empty string is the correct way to indicate no output.

4. **`+1` for null terminator:** Ensures space for the null terminator that `anyascii_string` writes.

## Prevention

### Best Practices for Buffer Allocation with Unicode

**Never assume 1:1 character mapping.** Unicode-to-ASCII transliteration can expand a single character into many:

| Input | Output | Expansion |
|-------|--------|-----------|
| `ü` | `ue` | 2x |
| `東` | `dong` | 4x |
| `京` | `jing` | 4x |
| `😀` | `:grinning_face:` | 15x |

### The 4x Rule for Unicode Expansion Buffers

For general CJK transliteration, **allocate at least 4x the input length**. For emoji support, consider 16x or dynamic resizing.

```cpp
// BAD: Buffer sized to input length
char* buffer = new char[input.length() + 1];  // OVERFLOW RISK

// GOOD: Apply 4x expansion factor
const size_t UNICODE_EXPANSION_FACTOR = 4;
std::vector<char> buffer(input.length() * UNICODE_EXPANSION_FACTOR + 1);
```

### Use std::vector Instead of Raw new/delete

| Approach | Pros | Cons |
|----------|------|------|
| `new char[]` | None | Manual cleanup, exception-unsafe, easy to miscalculate |
| `std::vector<char>` | RAII, bounds checking available, resizable | Slight overhead (negligible) |
| `std::string` | Best for string data, automatic management | May not work with C APIs |

```cpp
// BAD: Raw allocation - exception unsafe, manual cleanup
char* buffer = new char[size];
transliterate(input, buffer);  // If this throws, memory leaks
delete[] buffer;

// GOOD: RAII with std::vector
std::vector<char> buffer(size);
transliterate(input, buffer.data());
// Automatic cleanup even on exception
```

## Testing

### Test Cases for Unicode Expansion

#### Japanese Filenames (Kanji → Romaji)

```cpp
TEST(TransliterationTest, JapaneseKanjiExpansion) {
    // Single kanji can become 4+ romaji characters
    EXPECT_EQ(transliterate("東"), "tou");      // 1 char → 3
    EXPECT_EQ(transliterate("京"), "kyou");     // 1 char → 4
    EXPECT_EQ(transliterate("東京"), "toukyou"); // 2 chars → 7
    
    // Worst-case expansion test
    EXPECT_EQ(transliterate("電気通信"), "denkitsuushin"); // 4 → 13
}
```

#### Chinese Characters (Hanzi → Pinyin)

```cpp
TEST(TransliterationTest, ChinesePinyinExpansion) {
    EXPECT_EQ(transliterate("中"), "zhong");    // 1 char → 5
    EXPECT_EQ(transliterate("国"), "guo");      // 1 char → 3
    EXPECT_EQ(transliterate("中国"), "zhongguo"); // 2 chars → 8
}
```

### Testing for Heap Corruption

#### Using AddressSanitizer (ASAN)

```bash
# Compile with ASAN
clang++ -fsanitize=address -g -O1 \
    -fno-omit-frame-pointer \
    src/helpers.cpp -o helpers_asan

# Run tests - ASAN will catch buffer overflows
./helpers_asan
```

#### Using Valgrind

```bash
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --error-exitcode=1 \
         ./ascii-rename "東京.txt"
```

## Related Links

- [AnyAscii library](https://github.com/anyascii/anyascii) - upstream transliteration library
- [AnyAscii C implementation reference](https://github.com/anyascii/anyascii/blob/0.3.1/impl/c/test.c) - source of `anyascii_string` function
- [libpu8](https://github.com/jofeu/libpu8) - UTF-8 handling library used in this project

## Commit Reference

- `46b5afb` - "update for malloc problem" - The fix for this issue
