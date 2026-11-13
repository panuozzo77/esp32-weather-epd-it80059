/* Streaming, order-independent HTML field extractor.
 *
 * The page is fed in arbitrary chunks (as it arrives from the socket). Each
 * field is located by an "anchor" string, optionally followed by a "value tag"
 * string; the value is the text between the next '>' and the following '<'.
 * All fields are searched for simultaneously, so the order in which they
 * appear in the page does not matter and a missing field does not prevent
 * the others from being found.
 *
 * This file has no Arduino dependencies so it can be unit-tested on a host.
 */

#ifndef __HTML_EXTRACT_H__
#define __HTML_EXTRACT_H__

#include <stddef.h>
#include <stdint.h>

#define HTML_FIELD_VALUE_SIZE 48

typedef struct html_field {
  // Text identifying the field. Must be <= 63 characters.
  const char *anchor;
  // Optional text that must follow the anchor (within a few hundred bytes)
  // before the value. nullptr: the value starts after the next '>'.
  // "" (empty string): the value starts immediately after the anchor.
  const char *value_tag;
  // Extracted text (trimmed). Valid when found == true.
  char     value[HTML_FIELD_VALUE_SIZE];
  bool     found;
  // internal state
  uint8_t  stage;
  uint16_t gap;
  uint8_t  len;
} html_field_t;

class HtmlFieldExtractor
{
public:
  HtmlFieldExtractor(html_field_t *fields, size_t count);
  void reset();
  void feed(const char *data, size_t len);
  bool allFound() const;

private:
  static const size_t WIN_SIZE = 64;
  html_field_t *fields_;
  size_t        count_;
  size_t        remaining_;
  char          win_[WIN_SIZE];
  size_t        n_;

  bool endsWith(const char *s) const;
  void step(char c);
};

// Parses the first number in s ("18,6 °C", "-2.5", "87 %"). ',' is accepted as
// decimal separator. Returns false for placeholders like "--" or "n/d".
bool htmlParseNumber(const char *s, float &out);

// Copies the first run of letters found after `after` (or from the start when
// after == nullptr) into out, e.g. "0,9 kts (1,7 Km/h) direz. ENE" -> "ENE".
bool htmlParseWord(const char *s, const char *after, char *out, size_t outSize);

// Parses an Italian/English date-time such as
//   "11&nbsp;novembre&nbsp;2026 ore 23:40" or "Novembre 13, 2026 9:25 am"
// into broken-down local time. Returns false if any component is missing.
bool htmlParseDateTime(const char *s, int &year, int &mon, int &mday,
                       int &hour, int &min);

#endif // __HTML_EXTRACT_H__
