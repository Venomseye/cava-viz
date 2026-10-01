#include "text_utils.h"

#include <cctype>
#include <clocale>
#include <langinfo.h>

std::string truncateUtf8(const std::string &s, std::size_t max_bytes) {
  if (s.size() <= max_bytes)
    return s;
  std::size_t cut = max_bytes;
  // s[cut] is the first byte we would drop.  If it is a continuation byte
  // (10xxxxxx) we are in the middle of a character: back up to its lead byte.
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
    --cut;
  return s.substr(0, cut);
}

bool localeIsUtf8() {
  const char *cs = nl_langinfo(CODESET);
  if (!cs)
    return false;
  // Accept "UTF-8", "utf8", "UTF8" ...
  std::string norm;
  for (const char *p = cs; *p; ++p)
    if (*p != '-' && *p != '_')
      norm += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
  return norm == "utf8";
}

bool initUtf8Locale() {
  std::setlocale(LC_ALL, "");
  bool utf8 = localeIsUtf8();
  if (!utf8) {
    for (const char *cand :
         {"C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8"}) {
      if (std::setlocale(LC_ALL, cand) && localeIsUtf8()) {
        utf8 = true;
        break;
      }
    }
    if (!utf8)
      std::setlocale(LC_ALL, ""); // nothing better: keep the user's
  }
  // Numbers must stay locale-independent: in e.g. de_DE/fr_FR/pt_BR,
  // printf("%.2f") emits "1,50" and strtof("1.50") stops at the '.', which
  // would corrupt the config file we write and mis-read the one we load.
  // Nothing in the UI needs localised number formatting.
  std::setlocale(LC_NUMERIC, "C");
  return utf8;
}
