#pragma once

// One flash write for a batch of settings (#231). config.apply runs one CLI line per key, and each line saves the whole
// prefs file atomically: a nine-key broker profile took ten seconds and, cut by a power loss, kept some keys and not
// others. While a batch is open a role's savePrefs() only notes that a save is owed; the batch's end does it once.
namespace PrefsBatch {

typedef void (*SaveFn)(void* user);
struct State {
  bool open = false;
  SaveFn save = nullptr;
  void* user = nullptr;
};
inline State& state() {
  static State s;
  return s;
}

inline void begin() { state() = State(); state().open = true; }
// First thing in a role's savePrefs(): true means the save is now owed, and the caller stops there.
inline bool defer(SaveFn save, void* user) {
  State& s = state();
  if (!s.open) return false;
  s.save = save;
  s.user = user;
  return true;
}
// Closes the batch and does the save it owes, if any.
inline void end() {
  const State s = state();
  state() = State();
  if (s.save) s.save(s.user);
}

}  // namespace PrefsBatch
