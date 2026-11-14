#!/usr/bin/env bash
# Checks that an OpenWeatherMap API key works for the endpoints the firmware
# uses, and explains what is wrong if it does not.
#
# Usage:
#   ./check_owm.sh                 # key from platformio/include/secrets.h
#   ./check_owm.sh <api_key>       # explicit key
#   OWM_APIKEY=<key> ./check_owm.sh
#   LAT=40.78 LON=14.36 ./check_owm.sh   # override coordinates
#
# Exit code: 0 if One Call 3.0 works (what the firmware needs), 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECRETS_H="$SCRIPT_DIR/platformio/include/secrets.h"
CONFIG_CPP="$SCRIPT_DIR/platformio/src/config.cpp"

if [ -t 1 ]; then
  GREEN=$'\e[32m'; RED=$'\e[31m'; YELLOW=$'\e[33m'; BOLD=$'\e[1m'; RESET=$'\e[0m'
else
  GREEN=""; RED=""; YELLOW=""; BOLD=""; RESET=""
fi
ok()   { echo "  ${GREEN}OK${RESET}    $*"; }
fail() { echo "  ${RED}FAIL${RESET}  $*"; }
warn() { echo "  ${YELLOW}WARN${RESET}  $*"; }

command -v curl >/dev/null || { echo "curl is required"; exit 2; }
HAVE_JQ=0; command -v jq >/dev/null && HAVE_JQ=1

# --- API key -----------------------------------------------------------------
KEY="${1:-${OWM_APIKEY:-}}"
if [ -z "$KEY" ] && [ -f "$SECRETS_H" ]; then
  KEY="$(sed -n 's/^#define[[:space:]]*SECRET_OWM_APIKEY[[:space:]]*"\([^"]*\)".*/\1/p' "$SECRETS_H")"
  [ -n "$KEY" ] && KEY_SRC="secrets.h"
fi
KEY_SRC="${KEY_SRC:-argument/env}"
if [ -z "$KEY" ]; then
  echo "No API key: pass it as an argument, set OWM_APIKEY, or create $SECRETS_H"
  exit 2
fi
if ! [[ "$KEY" =~ ^[0-9a-f]{32}$ ]]; then
  warn "key does not look like a 32-char hex OWM key"
fi
MASKED="${KEY:0:4}…${KEY: -4}"

# --- Coordinates (same as the firmware unless overridden) ---------------------
cfg() { sed -n "s/^const String $1[[:space:]]*=[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$CONFIG_CPP" 2>/dev/null; }
LAT="${LAT:-$(cfg LAT)}"; LAT="${LAT:-40.7869}"
LON="${LON:-$(cfg LON)}"; LON="${LON:-14.3681}"

echo "${BOLD}OpenWeatherMap key check${RESET}"
echo "  key:      $MASKED (from $KEY_SRC)"
echo "  location: lat=$LAT lon=$LON"
echo

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# request <name> <url>: sets CODE and writes body to $TMP/<name>.json
request() {
  CODE="$(curl -sS -m 20 -o "$TMP/$1.json" -w '%{http_code}' "$2" 2>"$TMP/$1.err")"
  [ -z "$CODE" ] && CODE="000"
}

# Prints the "message" field of an error response.
message() {
  if [ "$HAVE_JQ" = 1 ]; then
    jq -r '.message // empty' "$TMP/$1.json" 2>/dev/null
  else
    sed -n 's/.*"message"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$TMP/$1.json"
  fi
}

explain() { # <name> <code>
  case "$2" in
    000) fail "no response (network/DNS problem): $(head -c 200 "$TMP/$1.err")" ;;
    401) fail "HTTP 401: $(message "$1")" ;;
    429) fail "HTTP 429: rate limit exceeded (too many calls today/minute)" ;;
    *)   fail "HTTP $2: $(message "$1")" ;;
  esac
}

BASE="https://api.openweathermap.org"

# 1. Key validity: Current Weather 2.5 works for any active key.
echo "${BOLD}1. Key active?${RESET}  (Current Weather 2.5)"
request weather "$BASE/data/2.5/weather?lat=$LAT&lon=$LON&units=metric&appid=$KEY"
KEY_OK=0
if [ "$CODE" = 200 ]; then
  KEY_OK=1
  if [ "$HAVE_JQ" = 1 ]; then
    ok "$(jq -r '"\(.name): \(.main.temp) °C, \(.main.humidity) %, \(.main.pressure) hPa, \(.weather[0].description)"' "$TMP/weather.json")"
  else
    ok "key is active"
  fi
else
  explain weather "$CODE"
  [ "$CODE" = 401 ] && echo "        -> key is wrong, deleted, or not activated yet (new keys can take up to ~2 h)"
fi
echo

# 2. One Call 3.0: what the firmware actually uses.
echo "${BOLD}2. One Call 3.0 subscription?${RESET}  (used by the firmware)"
request onecall "$BASE/data/3.0/onecall?lat=$LAT&lon=$LON&exclude=minutely&units=metric&appid=$KEY"
ONECALL_OK=0
if [ "$CODE" = 200 ]; then
  ONECALL_OK=1
  if [ "$HAVE_JQ" = 1 ]; then
    ok "$(jq -r '"today: \(.daily[0].temp.min)–\(.daily[0].temp.max) °C, rain chance \((.daily[0].pop*100)|round) %, \(.daily[0].weather[0].description); \(.hourly|length) hourly / \(.daily|length) daily entries"' "$TMP/onecall.json")"
  else
    ok "One Call 3.0 works"
  fi
else
  explain onecall "$CODE"
  if [ "$CODE" = 401 ] && [ "$KEY_OK" = 1 ]; then
    echo "        -> the key works but One Call 3.0 is not enabled for it. Check:"
    echo "           * https://home.openweathermap.org/subscriptions lists \"One Call by Call\" as active"
    echo "             (it needs billing info even for the free 1000 calls/day; set the daily limit to 1000)"
    echo "           * the key belongs to the same account as the subscription"
    echo "           * activation can take from a few minutes up to ~2 h after subscribing"
  fi
fi
echo

# 3. Air Pollution 2.5 (used by the upstream firmware screens).
echo "${BOLD}3. Air Pollution API?${RESET}"
request air "$BASE/data/2.5/air_pollution?lat=$LAT&lon=$LON&appid=$KEY"
if [ "$CODE" = 200 ]; then
  if [ "$HAVE_JQ" = 1 ]; then
    ok "AQI $(jq -r '.list[0].main.aqi' "$TMP/air.json") (1=good … 5=very poor)"
  else
    ok "Air Pollution API works"
  fi
else
  explain air "$CODE"
fi
echo

if [ "$ONECALL_OK" = 1 ]; then
  echo "${GREEN}${BOLD}Result: the firmware's OpenWeatherMap requests will work.${RESET}"
  exit 0
elif [ "$KEY_OK" = 1 ]; then
  echo "${YELLOW}${BOLD}Result: key is active, but One Call 3.0 is not available yet: the forecast card will stay on cached data.${RESET}"
  exit 1
else
  echo "${RED}${BOLD}Result: the key does not work.${RESET}"
  exit 1
fi
