#pragma once
// Contacts over the device interface (docs/contract/OPERATIONS.md section 5.1): contact.list, contact.get, contact.put,
// contact.remove and contact.protect, on the native contact table so there is one store. A result carries the entry the
// inventory stream carries (Streams.h): never a shared secret and never path bytes. A put cannot grant: a new contact
// has no grant flags and an existing one keeps its own. The table is injected, so the logic runs in the native unit
// tests against a fake one.
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

const unsigned CONTACT_NAME_MAX = 31;   // bytes of a name; the native record holds 32 without a terminator
const unsigned CONTACT_PAGE = 6;        // contacts per contact.list page, at most; fewer when the result would not fit
const size_t CONTACT_ENTRY_WORST = 280; // the longest entry: a 64-digit key, a name of 31 escaped quotes, a position
const size_t CONTACT_LIST_BUDGET = 1250;// bytes of a page before the signature, under the interface's result limit

// What a read shows of a contact (the native ContactInfo less its secret and its path bytes).
struct ContactRec {
  uint8_t key[32];
  char name[CONTACT_NAME_MAX + 1];
  uint8_t type;           // 1 chat, 2 repeater, 3 room, 4 sensor (ADV_TYPE_*); never 0, the anonymous slot's
  bool fav;               // the native favourite flag: protected from the oldest-first eviction
  uint32_t last_advert;
  int16_t hops;           // -1: no known path
  bool has_pos;
  int32_t lat_e6, lon_e6;
};

enum ContactWrite { CW_OK, CW_FULL, CW_FAILED };

struct ContactHost {
  int capacity;                                                   // most stored contacts; reported, and bounds the cursor
  int count;                                                      // stored contacts now
  bool (*get)(void* user, const uint8_t key[32], ContactRec& out);   // false when it is not stored
  // The next stored contact at or after the cursor, in record order; the cursor moves past it. False after the last.
  bool (*next)(void* user, int& cursor, ContactRec& out);
  // Adds or updates the contact with these name, type and position, as CMD_ADD_UPDATE_CONTACT does (an update keeps the
  // flags and the path; a new contact has neither), and schedules the lazy save. CW_FULL: the table has no room.
  ContactWrite (*put)(void* user, const ContactRec& in);
  int (*remove)(void* user, const uint8_t key[32]);               // 1 removed, 0 not stored
  int (*protect)(void* user, const uint8_t key[32], bool on);     // 1 flag changed, 0 already so, -1 not stored
  void* user;
};

// OpContext.user is a ContactHost.
Status opContactList(OpContext& c);
Status opContactGet(OpContext& c);
Status opContactPut(OpContext& c);
Status opContactRemove(OpContext& c);
Status opContactProtect(OpContext& c);

}  // namespace Iface
