/* Streaming, order-independent HTML field extractor. See html_extract.h.
 */

#include "html_extract.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Max bytes allowed between the anchor and the value before giving up on that
// match and searching for the anchor again.
static const uint16_t MAX_GAP = 512;

enum : uint8_t {
  STAGE_ANCHOR = 0, // looking for anchor
  STAGE_TAG,        // anchor found, looking for value_tag
  STAGE_GT,         // looking for the '>' that opens the value
  STAGE_CAPTURE,    // capturing value text until '<'
  STAGE_DONE
};

HtmlFieldExtractor::HtmlFieldExtractor(html_field_t *fields, size_t count)
  : fields_(fields), count_(count)
{
  reset();
}

void HtmlFieldExtractor::reset()
{
  n_ = 0;
  remaining_ = count_;
  for (size_t i = 0; i < count_; ++i)
  {
    fields_[i].value[0] = '\0';
    fields_[i].found = false;
    fields_[i].stage = STAGE_ANCHOR;
    fields_[i].gap = 0;
    fields_[i].len = 0;
  }
}

bool HtmlFieldExtractor::allFound() const
{
  return remaining_ == 0;
}

bool HtmlFieldExtractor::endsWith(const char *s) const
{
  size_t l = strlen(s);
  if (l == 0 || l > WIN_SIZE || l > n_) return false;
  for (size_t i = 0; i < l; ++i)
  {
    if (win_[(n_ - 1 - i) % WIN_SIZE] != s[l - 1 - i]) return false;
  }
  return true;
}

static void finishValue(html_field_t &f)
{
  f.value[f.len] = '\0';
  // trim trailing whitespace
  while (f.len > 0 && isspace((unsigned char)f.value[f.len - 1]))
  {
    f.value[--f.len] = '\0';
  }
  // trim leading whitespace
  size_t lead = 0;
  while (f.value[lead] && isspace((unsigned char)f.value[lead])) ++lead;
  if (lead) memmove(f.value, f.value + lead, f.len - lead + 1);
}

void HtmlFieldExtractor::step(char c)
{
  win_[n_ % WIN_SIZE] = c;
  ++n_;

  for (size_t i = 0; i < count_; ++i)
  {
    html_field_t &f = fields_[i];
    switch (f.stage)
    {
    case STAGE_ANCHOR:
      if (endsWith(f.anchor))
      {
        f.gap = 0;
        f.len = 0;
        if (f.value_tag == nullptr)       f.stage = STAGE_GT;
        else if (f.value_tag[0] == '\0')  f.stage = STAGE_CAPTURE;
        else                              f.stage = STAGE_TAG;
      }
      break;

    case STAGE_TAG:
      if (endsWith(f.value_tag))   f.stage = STAGE_GT;
      else if (++f.gap > MAX_GAP)  f.stage = STAGE_ANCHOR;
      break;

    case STAGE_GT:
      if (c == '>')                { f.stage = STAGE_CAPTURE; f.len = 0; }
      else if (++f.gap > MAX_GAP)  f.stage = STAGE_ANCHOR;
      break;

    case STAGE_CAPTURE:
      if (c == '<' || f.len >= HTML_FIELD_VALUE_SIZE - 1)
      {
        finishValue(f);
        f.found = true;
        f.stage = STAGE_DONE;
        --remaining_;
      }
      else if (!(f.len == 0 && isspace((unsigned char)c)))
      {
        f.value[f.len++] = (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
      }
      break;

    default:
      break;
    }
  }
}

void HtmlFieldExtractor::feed(const char *data, size_t len)
{
  for (size_t i = 0; i < len && remaining_ > 0; ++i)
  {
    step(data[i]);
  }
}

bool htmlParseNumber(const char *s, float &out)
{
  if (!s) return false;
  while (*s && isspace((unsigned char)*s)) ++s;

  char buf[16];
  size_t idx = 0;
  if ((*s == '-' || *s == '+') && isdigit((unsigned char)s[1]))
  {
    buf[idx++] = *s++;
  }
  if (!isdigit((unsigned char)*s)) return false; // "--", "n/d", empty, ...

  bool dot = false;
  while (*s && idx < sizeof(buf) - 1)
  {
    if (isdigit((unsigned char)*s))
    {
      buf[idx++] = *s;
    }
    else if ((*s == '.' || *s == ',') && !dot && isdigit((unsigned char)s[1]))
    {
      buf[idx++] = '.';
      dot = true;
    }
    else
    {
      break;
    }
    ++s;
  }
  buf[idx] = '\0';
  out = strtof(buf, nullptr);
  return true;
}

bool htmlParseWord(const char *s, const char *after, char *out, size_t outSize)
{
  if (!s || !out || outSize == 0) return false;
  out[0] = '\0';
  if (after)
  {
    s = strstr(s, after);
    if (!s) return false;
    s += strlen(after);
  }
  while (*s && !isalpha((unsigned char)*s)) ++s;
  size_t idx = 0;
  while (*s && isalpha((unsigned char)*s) && idx < outSize - 1)
  {
    out[idx++] = *s++;
  }
  out[idx] = '\0';
  return idx > 0;
}

static int monthFromName(const char *w)
{
  static const char *const MONTHS[12] = {
    "gen", "feb", "mar", "apr", "mag", "giu",
    "lug", "ago", "set", "ott", "nov", "dic"
  };
  static const char *const MONTHS_EN[12] = {
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"
  };
  if (strlen(w) < 3) return -1;
  char p[4] = { (char)tolower((unsigned char)w[0]),
                (char)tolower((unsigned char)w[1]),
                (char)tolower((unsigned char)w[2]), '\0' };
  for (int i = 0; i < 12; ++i)
  {
    if (strcmp(p, MONTHS[i]) == 0 || strcmp(p, MONTHS_EN[i]) == 0) return i + 1;
  }
  return -1;
}

bool htmlParseDateTime(const char *s, int &year, int &mon, int &mday,
                       int &hour, int &min)
{
  if (!s) return false;

  // Normalise: "&nbsp;" and ',' become spaces.
  char buf[HTML_FIELD_VALUE_SIZE + 8];
  size_t o = 0;
  for (const char *p = s; *p && o < sizeof(buf) - 1; )
  {
    if (strncmp(p, "&nbsp;", 6) == 0) { buf[o++] = ' '; p += 6; }
    else if (*p == ',')               { buf[o++] = ' '; ++p; }
    else                              { buf[o++] = *p++; }
  }
  buf[o] = '\0';

  year = mon = mday = hour = min = -1;
  int ampm = 0; // 0 none, 1 am, 2 pm

  char *save = nullptr;
  for (char *tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(nullptr, " ", &save))
  {
    if (strchr(tok, ':'))
    {
      int h, m;
      if (sscanf(tok, "%d:%d", &h, &m) == 2) { hour = h; min = m; }
    }
    else if (isdigit((unsigned char)tok[0]))
    {
      int v = atoi(tok);
      if (v >= 1900)      year = v;
      else if (mday < 0)  mday = v;
    }
    else if (strcasecmp(tok, "am") == 0) ampm = 1;
    else if (strcasecmp(tok, "pm") == 0) ampm = 2;
    else
    {
      int m = monthFromName(tok);
      if (m > 0 && mon < 0) mon = m;
    }
  }

  if (ampm == 2 && hour >= 0 && hour < 12) hour += 12;
  if (ampm == 1 && hour == 12) hour = 0;

  return year > 0 && mon >= 1 && mon <= 12 && mday >= 1 && mday <= 31
      && hour >= 0 && hour < 24 && min >= 0 && min < 60;
}
