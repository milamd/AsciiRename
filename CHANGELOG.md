# AsciiRename Changelog #

## v1.2.0 ##

* Rename full file paths including parent directories (bottom-up processing)
* Add shell metacharacter sanitization (`;$`|&><'"\\*?[]()!~#` replaced with `_`)
* Handle multiple arguments sharing parent directories via deduplication
* Track renamed paths to resolve subsequent operations correctly

## v1.1.0 ##

* Upgrade to anyascii 3.2
* Change emoji replacement to resolve with % instead of :
* Fix buffer overflow in Unicode-to-ASCII transliteration (4x buffer sizing)

## v1.0.0 ##

* First release
