#pragma once
#include <cstddef>
#include <string>

/// Truncate `s` to at most `max_bytes` bytes WITHOUT cutting a multi-byte
/// UTF-8 sequence in half (a plain "%.38s" can leave a broken trailing byte
/// that renders as garbage and can confuse the terminal).
std::string truncateUtf8(const std::string &s, std::size_t max_bytes);

/// True if the active C locale's character set is UTF-8.
bool localeIsUtf8();

/// Set the process locale from the environment and make sure it is UTF-8:
/// if the environment gives "C"/POSIX or a legacy charset (typical over ssh,
/// in containers, or with LANG unset), try C.UTF-8 and en_US.UTF-8.
/// LC_NUMERIC is then forced back to "C" so config numbers always use a
/// '.' decimal point whatever the user's language.
/// Returns whether a UTF-8 locale is now active.  The bar glyphs are UTF-8
/// block characters, so without this they render as mojibake.
bool initUtf8Locale();
