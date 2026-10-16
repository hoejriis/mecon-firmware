#include "WifiProfiles.h"
#include "WifiCore.h"
#include <stdio.h>

static bool parseIndex(const char*& p, uint8_t& idx) {
  if (*p < '0' || *p > '9') return false;
  idx = (uint8_t)(*p - '0');
  p++;
  if (*p != 0 && *p != ' ') return false;   // one digit only
  return true;
}

static int formatProfile(const WifiProfiles& pr, const WifiCore* core, uint8_t i, char* out, size_t cap) {
  return snprintf(out, cap, "%u: %s prio=%u pwd=%s last=%s", (unsigned)i, pr.ssid(i), (unsigned)pr.priority(i),
                  pr.hasPwd(i) ? "set" : "unset", WifiCore::failName(core ? core->lastFail(i) : WifiFail::None));
}

bool wifiProfileCommand(WifiProfiles& pr, const WifiCore* core, const char* command, char* reply, size_t cap,
                        WifiProfileChange& change) {
  change = WifiProfileChange();
  const char* p;

  if (memcmp(command, "set wifi.profile ", 17) == 0) {
    p = command + 17;
    uint8_t idx;
    if (!parseIndex(p, idx) || idx >= WifiProfiles::COUNT) {
      snprintf(reply, cap, "ERR: profile number 0-%u", (unsigned)(WifiProfiles::COUNT - 1));
      return true;
    }
    while (*p == ' ') p++;
    if (strcmp(p, "clear") == 0) {
      pr.clear(idx);
      change.changed = true; change.index = idx; change.credentials = true;
      snprintf(reply, cap, "> profile %u cleared", (unsigned)idx);
    } else if (memcmp(p, "ssid ", 5) == 0) {
      const char* v = p + 5;
      if (!pr.setSsid(idx, v)) snprintf(reply, cap, "ERR: ssid is 1-32 characters");
      else if (!*v) snprintf(reply, cap, "ERR: ssid is 1-32 characters (use clear to remove a profile)");
      else {
        change.changed = true; change.index = idx; change.credentials = true;
        snprintf(reply, cap, "> profile %u ssid is now %s", (unsigned)idx, pr.ssid(idx));
      }
    } else if (strcmp(p, "pwd") == 0 || memcmp(p, "pwd ", 4) == 0) {
      const char* v = p[3] ? p + 4 : "";
      if (!pr.setPwd(idx, v)) snprintf(reply, cap, "ERR: password is at most 63 characters");
      else {
        change.changed = true; change.index = idx; change.credentials = true;
        snprintf(reply, cap, "> profile %u pwd %s", (unsigned)idx, *v ? "updated" : "cleared (open network)");
      }
    } else if (memcmp(p, "priority ", 9) == 0) {
      const char* v = p + 9;
      unsigned n = 0;
      bool ok = *v != 0;
      for (const char* c = v; *c; c++) {
        if (*c < '0' || *c > '9' || (n = n * 10 + (*c - '0')) > 255) { ok = false; break; }
      }
      if (!ok) snprintf(reply, cap, "ERR: priority is 0-255 (higher is tried first)");
      else {
        pr.setPriority(idx, (uint8_t)n);
        change.changed = true; change.index = idx; change.credentials = false;
        snprintf(reply, cap, "> profile %u priority is now %u", (unsigned)idx, n);
      }
    } else {
      snprintf(reply, cap, "ERR: set wifi.profile <n> ssid|pwd|priority <value> | clear");
    }
    return true;
  }

  if (memcmp(command, "get wifi.profile ", 17) == 0) {
    p = command + 17;
    uint8_t idx;
    if (!parseIndex(p, idx) || idx >= WifiProfiles::COUNT) {
      snprintf(reply, cap, "ERR: profile number 0-%u", (unsigned)(WifiProfiles::COUNT - 1));
      return true;
    }
    if (!pr.configured(idx)) { snprintf(reply, cap, "> %u: (not set)", (unsigned)idx); return true; }
    int n = snprintf(reply, cap, "> ");
    formatProfile(pr, core, idx, reply + n, cap - n);
    return true;
  }

  if (strcmp(command, "get wifi.profiles") == 0 || memcmp(command, "get wifi.profiles ", 18) == 0) {
    uint8_t from = 0;
    if (command[17] == ' ') {
      p = command + 18;
      if (!parseIndex(p, from) || from >= WifiProfiles::COUNT) {
        snprintf(reply, cap, "ERR: profile number 0-%u", (unsigned)(WifiProfiles::COUNT - 1));
        return true;
      }
    }
    size_t len = snprintf(reply, cap, ">");
    bool any = false;
    for (uint8_t i = from; i < WifiProfiles::COUNT; i++) {
      if (!pr.configured(i)) continue;
      char line[96];
      int ll = formatProfile(pr, core, i, line, sizeof(line));
      if (ll < 0 || len + 1 + (size_t)ll + 16 >= cap) {   // keep room for the continuation marker
        if (!any) { snprintf(reply + len, cap - len, " (reply too small)"); return true; }
        snprintf(reply + len, cap - len, "\n... next:%u", (unsigned)i);
        return true;
      }
      len += snprintf(reply + len, cap - len, "\n%s", line);
      any = true;
    }
    if (!any) snprintf(reply, cap, "> (no profiles)");
    return true;
  }
  return false;
}
