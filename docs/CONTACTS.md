# Adaptive contact memory

Stock MeshCore 1.18 keeps every contact in one static array, `ContactInfo contacts[MAX_CONTACTS + MAX_ANON_CONTACTS]` in `BaseChatMesh`, about 184 bytes each. The Heltec V3, T1000-E and Wio Tracker L1 builds set `MAX_CONTACTS=350`, so about 64 KB of RAM is reserved whether used or not, and the table is saved as one file (`/contacts3`) written in place.

This firmware replaces the array with a **persistent contact store** on the board's filesystem and a **RAM cache** of resident contacts, behind the same `BaseChatMesh` API. This page records the access map of the stock code that was read before the change, then the design.

## 1. Access map of the stock code (read from 1.18.0 before the change)

Everything that touched `contacts[]`, `num_contacts` or `sort_array`, and everything that reaches contacts through the public API.

### Inside `BaseChatMesh` (`src/helpers/BaseChatMesh.h`, `.cpp`)

| Function | What it does with the table | Why it matters for a cache |
|---|---|---|
| constructor, `resetContacts()` | zero the first `MAX_ANON_CONTACTS` (8) slots, `num_contacts = 8` | the 8 anon slots are transient (type `ADV_TYPE_NONE`), never saved, used for anonymous requests (login to a repeater, room) |
| `bootstrapRTCfromContacts()` | scan all for the newest `lastmod` | needs `lastmod` of every contact, not their bodies |
| `allocateContactSlot(transient_only)` | transient: oldest anon slot; else append, or (if `shouldOverwriteWhenFull()`) replace the non-favourite with the oldest `lastmod` and raise `onContactOverwrite()` before reuse; `NULL` when full and nothing may be replaced | the only place eviction happens |
| `onAdvertRecv()` | scan all for `id.matches()`, replay check on `last_advert_timestamp`, allocate for a new contact, fill it in place through the returned pointer | holds a `ContactInfo*` across `allocate` and the callbacks |
| `searchPeersByHash()` | scan all, store the matching **indexes** in `matching_peer_indexes[]` (up to 24) for the later `getPeerSharedSecret(peer_idx)` / `onPeerDataRecv(sender_idx)` calls of the same packet | an index must stay meaningful between the three calls |
| `getPeerSharedSecret()`, `onPeerDataRecv()`, `onPeerPathRecv()` | read the contact by index; a plain text message updates `lastmod` and may call `handleReturnPathRetry()`; a path update rewrites `out_path` and `lastmod` | in-place mutation through a reference |
| `lookupContactByPubKey(prefix, len)` | linear scan, anon slots first, returns a pointer | returned pointer is used right away by every caller, and mutated (flags, path, name) |
| `searchContactsByPrefix(name)` | linear scan by name | needs names |
| `scanRecentContacts(n, visitor)` | sorts indexes by `last_advert_timestamp` into `sort_array`, visits each | only `simple_secure_chat` uses it |
| `addContact()` / `removeContact()` | allocate and copy / find by id and shift the array down | order of the array is insertion order |
| `getNumContacts()`, `getTotalContactSlots()`, `getContactByIdx()`, `startContactsIterator()` + `ContactsIterator::hasNext()` | count, indexed read, cursor read (cursor starts after the anon slots) | the app sync and the save both walk the table |
| `checkConnections()`, `checkConnectionsAck()` | `lookupContactByPubKey()` by the stored server key | one lookup per call |
| `sendMessage()`, `sendCommandData()`, `sendLogin()`, `sendAnonReq()`, `sendRequest()`, `shareContactZeroHop()`, `exportContact()`, `importContact()` | take a `ContactInfo&` or `const ContactInfo&`; the table is not touched (import goes through `onAdvertRecv`) | the outbound message is the place to note "a direct message went out" |

### Companion (`examples/companion_radio`)

| Where | Access |
|---|---|
| `MyMesh::begin()` | `resetContacts()` then `_store->loadContacts(this)` (whole file `/contacts3`, each record into `addContact()`), then `bootstrapRTCfromContacts()` |
| `DataStore::loadContacts()`, `saveContacts()` | the stock file: 152-byte records (key 32, name 32, type, flags, unused, `sync_since` 4, `out_path_len`, `last_advert_timestamp` 4, `out_path` 64, `lastmod` 4, lat 4, lon 4), written **in place** with `"w"`, skipping the anon slots (`save_filter`) |
| `MyMesh::saveContacts()` | called by the lazy timer (`dirty_contacts_expiry`, `LAZY_CONTACTS_WRITE_DELAY`), by `CMD_REBOOT` when dirty, and after a clock set |
| `CMD_GET_CONTACTS` + `checkSerialInterface()` | count = `getNumContacts()`, then one contact per loop pass through a `ContactsIterator`, filtered by `lastmod > since` |
| `CMD_ADD_UPDATE_CONTACT`, `CMD_REMOVE_CONTACT`, `CMD_RESET_PATH`, `CMD_SHARE_CONTACT`, `CMD_GET_CONTACT_BY_KEY`, `CMD_EXPORT_CONTACT`, and every send command | `lookupContactByPubKey()` by 6-byte prefix or 32-byte key; mutate in place; schedule the lazy save |
| `CMD_IMPORT_PRIVATE_KEY` | `resetContacts()` + reload, to drop the cached ECDH shared secrets |
| `CMD_DEVICE_QUERY` | reports `MAX_CONTACTS / 2` in one byte |
| `onContactOverwrite()` | deletes the contact's advert blob and pushes `PUSH_CODE_CONTACT_DELETED` |
| `ui-new/UITask.cpp` | one `lookupContactByPubKey()` for the sender name of a message |

### Not users of the table

The Repeater, Room server and Sensor derive from `mesh::Mesh`, not `BaseChatMesh`: they keep clients in `ClientACL` and are unchanged. `simple_secure_chat` derives from `BaseChatMesh` and keeps its own file; it works on the RAM-only mode of the store below.

### Properties the design has to keep

1. Callers hold a `ContactInfo*` for the length of one operation and write through it.
2. `searchPeersByHash` hands out indexes that are used by two later calls for the same packet.
3. The app sees contacts in insertion order and a count that matches what it then receives.
4. Eviction raises `onContactOverwrite()` with the key before the slot is reused.
5. Anon slots are never persisted.

## 2. Design

### Two tiers behind the same API

`BaseChatMesh` keeps its public and protected API. Its table is replaced by

* the **8 anon slots**, a plain array as before (transient, never saved);
* a **`ContactStore`** (`src/helpers/ContactStore.h`), which owns everything else.

Callers still get a `ContactInfo*` from `lookupContactByPubKey()` and write through it. A pointer stays valid until its cache slot is replaced, which takes at least `CONTACT_WORKING_SET` (8) further distinct contacts being touched; no code path of the Companion touches more than three contacts per operation. The three calls of one received packet (`searchPeersByHash`, `getPeerSharedSecret`, `onPeerDataRecv`) pass **handles** (anon slot, or record id plus 8) instead of array indexes, and load the contact only when the call is made, so up to 24 hash matches never have to be resident together.

### The persistent store

| | |
|---|---|
| Record | 160 bytes: the 152-byte stock `/contacts3` record byte for byte, then the time of the last direct message (4) and a CRC32 over the first 156 bytes (4). A record of all zeros is free. |
| Bucket | 8 records, one file `/ct<N>` of 1280 bytes (`CONTACT_RECORD_BUCKET`). Record `r` is in bucket `r / 8`. |
| Atomic write | A changed bucket is rewritten through `AtomicFile::save()` (temp file, flush, read-back of length and CRC, swap), the same path the prefs, identity and channel table use. A power cut leaves the previous bucket or the new one, never a mix; the 8 records of a bucket change together. An empty bucket file is removed. |
| Why buckets, not one file or one file per contact | Neither ESP32 SPIFFS nor the Arduino `File` API offers a portable in-place write at an offset, so a single record file cannot be updated record by record. A file per contact costs a file-system entry each (and a flash page or block each). A bucket keeps the number of files at a few dozen to a few hundred, and bounds the temporary space of a save to one bucket. |
| Index | 16 bytes per record, always in RAM: the first 4 key bytes, `lastmod`, last-message time, a cache slot number and flags (used, favourite, has a path). A lookup by key prefix or by the one-byte path hash is answered from the index; only a candidate is read. |
| Capacity | At boot: `min(store ceiling, (free file-system space + our own bucket files - reserve) / 200 bytes per record, heap limit)`, never below what is already stored, never below the board's `MAX_CONTACTS` while the file system has room, rounded up to whole buckets. The heap limit is half of the contact memory's heap share divided by 16 bytes per index entry. It is reported by `get contacts.capacity`. |

**Temporary flash space.** A contact save needs one bucket of temporary space, 1280 bytes plus the file system's overhead, instead of a full second copy of the table: 53,200 bytes for 350 contacts, which does not fit twice into a 28 KB nRF52 internal file system (7 pages of 4 KB, 128-byte blocks). That is the reason the table is split, and the capacity rule keeps `fs_reserve` bytes (8 KB by default) free for it and for the other atomic swaps. The `CONTACTS_STOCK_ARRAY` fallback saves the whole table atomically and so does need the second copy; that is stated rather than hidden.

### The RAM cache

* **Size**, decided once at boot from the free heap the board measures (`ESP.getFreeHeap()`, `dbgHeapFree()`, `rp2040.getFreeHeap()`; a board that cannot measure gets the floor): `slots = (heap share - index) / sizeof(slot)`, where the heap share is 40 % of the free heap above a 40 KB margin that is left for the BLE or Wi-Fi stack that starts later. The result is clamped to `CONTACT_CACHE_FLOOR` (16) .. `CONTACT_CACHE_CEILING` (96), and never above the capacity. A slot is about 200 bytes.
* **Favourites** are resident. At boot they are loaded first and pinned; a favourite flag set later through a pointer pins its slot the next time a slot is chosen. At most `slots - 8` slots are pinned, so that eight are always left for the working set; a favourite beyond that is never evicted from the store but can be replaced in the cache like any other and is read back on demand.
* **Everything else** is loaded on demand and the least recently used slot is replaced first. A slot whose record changed is written (its bucket is rewritten) before it is reused; if that write fails the slot is kept and the new contact is not taken, so a change is never dropped to make room.
* **Changes by pointer** are found without hooking every writer: each resident slot remembers the CRC32 of its record image as loaded or written, and `flush()` (the existing lazy timer, a reboot, `rebuild`) rewrites exactly the buckets whose resident records differ.

### Eviction: two separate rules

1. **From the cache**: least recently used, favourites pinned (above).
2. **From the store**, only when the store is full **and** the caller allows overwriting (`shouldOverwriteWhenFull()`, the existing "overwrite oldest" auto-add setting): the lowest score goes, never a favourite. The score is ordered by class, then by time:

   | Class | Meaning |
   |---|---|
   | 2 | a direct message sent or received in the last 30 days |
   | 1 | an older direct message, or a known path |
   | 0 | advert only |

   Within a class the older of the last message and the last heard goes first; a tie goes to the lowest record number. A stranger's advert therefore replaces other strangers before it can touch anyone we have talked to.

   The stock overwrite event is raised, with the key, before the record is reused (`onContactOverwrite()`: the advert blob is deleted and `PUSH_CODE_CONTACT_DELETED` goes to the app). With overwriting off, a full store refuses as stock does (`onContactsFull()`, `ERR_CODE_TABLE_FULL`). **Adding to a store whose every contact is a favourite is refused explicitly**: the app gets `ERR_CODE_TABLE_FULL`, nothing is evicted, and `get contacts.capacity` counts it under `refused`.

A direct message is noted for text messages the Companion sends (`sendMessage`) and plain text messages it receives (`onPeerDataRecv`). Contacts migrated from a stock file have no message history until they are used.

### App compatibility

* `CMD_GET_CONTACTS` streams from the store, one contact per loop pass as before, so the app sees every stored contact, not only the cached ones. The cursor is a record number, so adds and removals between steps do not break the walk. The count in the first reply is the stored count.
* The order of the stream is record order, not insertion order of the old array; the app does not rely on either (it uses `lastmod` and the final `since` value).
* `CMD_DEVICE_QUERY` reports `capacity / 2` in its one byte, capped at 255, where stock reported `MAX_CONTACTS / 2`.
* After `CMD_IMPORT_PRIVATE_KEY` the cached ECDH shared secrets of the anon slots and the resident contacts are invalidated; the secrets are never stored.

### Status

`get contacts.capacity` (USB console, BLE console, and a remote CLI reader as a read):

```
> adaptive store 412/1000 cache 38/48 pinned 6 fav 6 evicted 12 refused 0+0 loads 91 damaged 0 werr 0 ram 28113
```

store used / capacity, cache used / slots, pinned favourites, favourites in the store, evictions, refusals (store full without overwriting + only favourites left), records read from the file system since boot, damaged records skipped at boot, bucket write errors, and the heap the contact memory holds in bytes. A build with `CONTACTS_STOCK_ARRAY` answers `ram-only`.

### Migration from a stock file

On the first boot a stock `/contacts3` file is read into the store; the file is removed **only after the last bucket has been written and verified**, so a power cut at any point leaves it in place and the next boot continues. An empty store takes the file in bulk (one write per bucket, bypassing the cache); a store that already holds contacts (a migration that was cut short, or a board that went to stock firmware and back) merges it, keeping the store's version of a contact that is in both. The stock record format is the first 152 bytes of a store record, so a migrated contact is unchanged. If the file holds more contacts than the store can take, the most meaningful stay.

Going back to a stock firmware afterwards finds no `/contacts3` and starts with no contacts; the bucket files take file-system space it does not know about. Nothing here writes a stock-format copy.

### Rescue console `rebuild`

`rebuild` works from the persistent store, not from the cache, and does not format the file system (that would erase the contacts that are not in the cache). It writes the identity, preferences and channels again from memory, then for every bucket file: reads it, re-verifies each record's CRC, keeps every valid record (cached or not; a changed cached record is taken from RAM), and rewrites the bucket through the atomic swap, so a power cut during the command leaves the previous bucket or the new one. A record whose CRC fails is left out of the rewritten bucket and counted; before such a bucket is touched, its bytes are kept as `/ct<N>.bad`, and if that copy cannot be written the bucket is left as it is. The RAM index is rebuilt from what the buckets hold. It prints `rebuilt N contacts, M damaged`. A bucket file that cannot be read at all is replaced if it holds a cached contact, but it cannot be copied aside. The `CONTACTS_STOCK_ARRAY` build keeps the stock behaviour (format, then write the table again).

### Opt-out

`-D CONTACTS_STOCK_ARRAY` gives a board the stock behaviour: a static array of `MAX_CONTACTS` records, no heap use, the whole table in `/contacts3`, the oldest `lastmod` evicted whatever the history. The file is saved atomically in this mode as well. It is built and tested in the `sim_companion_stockcontacts` environment.

### Build flags

| Flag | Default | Meaning |
|---|---|---|
| `CONTACTS_STOCK_ARRAY` | off | the opt-out above |
| `CONTACT_CACHE_FLOOR`, `CONTACT_CACHE_CEILING` | 16, 96 | fewest and most cache slots |
| `CONTACT_STORE_CEILING` | 1000 | most records, whatever the room (at least `MAX_CONTACTS`) |
| `CONTACT_HEAP_MARGIN`, `CONTACT_HEAP_PERCENT` | 40000, 40 | heap left alone, and the share of the rest that the contact memory may use |
| `CONTACT_FS_RESERVE` | 8192 | file-system bytes left alone for the other files and the swaps |
| `CONTACT_RECORD_BUCKET` | 8 | records per bucket file |

## 3. Tests

* `test/test_contact_store` (native unit tests, in-memory backend): the record image against the stock layout and against every single flipped byte, lookups by prefix and hash, flush granularity, the cache size rule, favourites resident at boot and under churn, a changed record written before its slot is reused (and never dropped when the write fails), capacity from the file system and the heap, each eviction rule, the overwrite event, `rebuild`, the refusal with only favourites, bulk import, failed and half-finished flushes.
* `sim/tests/test_contacts.py` (simulated Companions): migration of a stock file and a cut during it, merge into a filled store, app sync beyond the cache, capacity and cache following the reported heap, RAM against the stock array, favourites resident and reachable under a small heap over the radio, eviction order and the overwrite push, message history either way, refusal with only favourites, power cuts at any byte of a bucket's temp file and between write and rename, a damaged bucket, the rescue `rebuild` (more stored contacts than cache slots, a damaged record isolated and its bucket kept aside, a power cut during the command), and the stock-array build.

A pass is simulation: the file system is a host directory and the heap is a number the test passes. Flash wear, flash write latency and the real free heap of a board are not exercised and need a run on the hardware.
