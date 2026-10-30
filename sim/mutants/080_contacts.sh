# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- adaptive contact memory (docs/CONTACTS.md): undo each rule and its tests must go red
CS=src/helpers/ContactStore.cpp
CT_SIM=sim/tests/test_contacts.py
mutate contacts_bucket_written_in_place examples/companion_radio/DataStore.cpp '  bucketPath(path, sizeof(path), b);
  return AtomicFile::save(_getContactsChannelsFS(), path, [buf, len](Stream& s) { return s.write(buf, len) == len; }, is_new);
}

bool DataStore::removeBucket' '  bucketPath(path, sizeof(path), b);
  File f = openWrite(_getContactsChannelsFS(), path); if (!f) return false; bool ok = f.write(buf, len) == len; f.close(); return ok;
}

bool DataStore::removeBucket' $CT_SIM::test_power_cut_in_the_temp_file_keeps_the_previous_record $CT_SIM::test_contacts_are_never_written_in_place
mutate contacts_stock_file_written_in_place examples/companion_radio/DataStore.cpp 'AtomicFile::save(_getContactsChannelsFS(), "/contacts3", [host, filter](Stream& s) {' 'auto inPlaceSave = [](FILESYSTEM* fs, const char* path, auto writer) { File f = openWrite(fs, path); if (f) { writer(f); f.close(); } }; inPlaceSave(_getContactsChannelsFS(), "/contacts3", [host, filter](Stream& s) {' $CT_SIM::test_the_stock_array_build_is_ram_only_and_atomic_and_evicts_the_oldest
mutate contacts_flush_skipped examples/companion_radio/MyMesh.cpp '  flushContacts();   // rewrites the buckets that hold a changed record, each one atomically' '' $CT_SIM::test_power_cut_between_write_and_rename
mutate contacts_old_file_kept_after_migration examples/companion_radio/MyMesh.cpp '  if (ok) _store->removeLegacyContacts();' '' $CT_SIM::test_stock_contacts_file_migrates_unchanged
mutate contacts_merge_overwrites_store examples/companion_radio/MyMesh.cpp '    if (contactStore().find(contact.id.pub_key, PUB_KEY_SIZE)) return true;   // the store'"'"'s version is newer' '' $CT_SIM::test_a_stock_file_beside_a_filled_store_is_merged_not_duplicated
mutate contacts_bulk_import_into_filled_store examples/companion_radio/MyMesh.cpp '  bool bulk = cs.count() == 0;' '  bool bulk = true;' $CT_SIM::test_a_stock_file_beside_a_filled_store_is_merged_not_duplicated
mutate contacts_record_crc_unchecked $CS '  if (crc32(in, 156) != want) return false;' '' unit:test_contact_store
mutate contacts_record_crc_unchecked_sim $CS '  if (crc32(in, 156) != want) return false;' '' $CT_SIM::test_a_damaged_bucket_loses_only_what_is_in_it
mutate contacts_record_layout_changed $CS '  out[64] = c.type;
  out[65] = c.flags;' '  out[64] = c.flags;
  out[65] = c.type;' unit:test_contact_store
mutate contacts_record_layout_changed_sim $CS '  out[64] = c.type;
  out[65] = c.flags;' '  out[64] = c.flags;
  out[65] = c.type;' $CT_SIM::test_stock_contacts_file_migrates_unchanged
mutate contacts_key_prefilter_fixed_width $CS '  int n = prefix_len < 4 ? prefix_len : 4;' '  int n = 4;' unit:test_contact_store
mutate contacts_hash_lookup_inverted $CS '    if (memcmp(e.key, hash, PATH_HASH_SIZE) == 0) out[n++] = r;' '    if (memcmp(e.key, hash, PATH_HASH_SIZE) != 0) out[n++] = r;' unit:test_contact_store
mutate contacts_flush_writes_every_bucket $CS '    if (_bdirty[b >> 3] & (1u << (b & 7))) {' '    if (true) {' unit:test_contact_store
mutate contacts_cache_ignores_heap $CS '    uint32_t slots = cache_budget / sizeof(Slot);' '    uint32_t slots = cfg.cache_ceiling;' unit:test_contact_store
mutate contacts_cache_ignores_heap_sim $CS '    uint32_t slots = cache_budget / sizeof(Slot);' '    uint32_t slots = cfg.cache_ceiling;' $CT_SIM::test_capacity_and_cache_follow_the_heap_the_board_reports
mutate contacts_boot_probes_every_bucket $CS '    if (_bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' '    if (false && _bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' $CT_SIM::test_boot_file_system_calls_do_not_grow_with_the_capacity_empty_store $CT_SIM::test_boot_file_system_calls_follow_the_files_present_with_350_contacts
mutate contacts_boot_probes_every_bucket_unit $CS '    if (_bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' '    if (false && _bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' unit:test_contact_store
mutate contacts_boot_recovers_every_bucket examples/companion_radio/DataStore.cpp '      if (!(swap[b >> 3] & (1u << (b & 7)))) continue;
' '' $CT_SIM::test_boot_file_system_calls_do_not_grow_with_the_capacity_empty_store
mutate contacts_boot_swap_not_recovered examples/companion_radio/DataStore.cpp '      AtomicFile::recover(fs, path);
      if (fs->exists(path)) present' '      if (fs->exists(path)) present' $CT_SIM::test_a_swap_left_by_a_power_cut_in_a_high_bucket_is_recovered_at_boot
mutate contacts_capacity_ignores_filesystem $CS '    if (cap > cap_fs) cap = (uint32_t)cap_fs;' '' unit:test_contact_store
mutate contacts_capacity_ignores_filesystem_sim $CS '    if (cap > cap_fs) cap = (uint32_t)cap_fs;' '' $CT_SIM::test_a_full_store_evicts_the_least_meaningful_and_never_a_favourite
mutate contacts_capacity_below_what_is_stored $CS '    if (cap < (uint32_t)(max_bucket + 1) * K) cap = (uint32_t)(max_bucket + 1) * K;   // never smaller than what is stored' '' unit:test_contact_store
mutate contacts_favourites_not_pinned_on_use $CS '  if (fav && !s.pinned && _pinned < _pin_limit) { s.pinned = 1; _pinned++; }' '  if (false) { s.pinned = 1; _pinned++; }' unit:test_contact_store
mutate contacts_favourites_not_pinned_on_replace $CS '    if (s.rec >= 0 && !s.pinned && (s.ci.flags & 0x01)) { s.pinned = 1; _pinned++; }' '    if (false) { s.pinned = 1; _pinned++; }' unit:test_contact_store
mutate contacts_pinned_slot_replaced_sim $CS '    if (_slots[i].pinned) continue;' '' $CT_SIM::test_favourites_stay_resident_and_reachable_under_a_small_heap
mutate contacts_pinned_slot_replaced $CS '    if (_slots[i].pinned) continue;' '' unit:test_contact_store
mutate contacts_pinned_favourites_fill_the_cache $CS '  _pin_limit = _nslots > CONTACT_WORKING_SET ? _nslots - CONTACT_WORKING_SET : 0;' '  _pin_limit = _nslots;' unit:test_contact_store
mutate contacts_dirty_slot_dropped $CS '  if (slotDirty(_slots[victim])) {' '  if (false) {' unit:test_contact_store
mutate contacts_failed_write_still_evicts $CS '    if (!flushBucket(bucketOf(_slots[victim].rec))) return -1;   // cannot let a changed record go' '    flushBucket(bucketOf(_slots[victim].rec));' unit:test_contact_store
mutate contacts_sync_only_cached $CS '    if (readRecord(r, out, lm)) return true;' '    continue;' $CT_SIM::test_app_sync_returns_every_stored_contact_beyond_the_cache
mutate contacts_sync_only_cached_unit $CS '    if (readRecord(r, out, lm)) return true;' '    continue;' unit:test_contact_store
mutate contacts_sync_count_is_the_cache src/helpers/BaseChatMesh.h '  int getNumContacts() const { return cstore.count(); }' '  int getNumContacts() const { return cstore.cacheSlots(); }' $CT_SIM::test_app_sync_returns_every_stored_contact_beyond_the_cache
mutate contacts_evict_without_permission $CS '    if (!allow_evict) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' '    if (false) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' $CT_SIM::test_without_the_overwrite_setting_a_full_store_refuses_and_keeps_everything
mutate contacts_evict_without_permission_unit $CS '    if (!allow_evict) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' '    if (false) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' unit:test_contact_store
mutate contacts_favourite_evictable $CS '    if (!(e.flags & F_USED) || (e.flags & F_FAV)) continue;' '    if (!(e.flags & F_USED)) continue;' unit:test_contact_store
mutate contacts_favourite_evictable_sim $CS '    if (!(e.flags & F_USED) || (e.flags & F_FAV)) continue;' '    if (!(e.flags & F_USED)) continue;' $CT_SIM::test_a_full_store_evicts_the_least_meaningful_and_never_a_favourite
mutate contacts_all_favourites_refusal_not_counted $CS '    if (rec < 0) { _refused_fav++; _refusal = REFUSED_ALL_FAVOURITES; return NULL; }' '    if (rec < 0) { _refusal = REFUSED_ALL_FAVOURITES; return NULL; }' $CT_SIM::test_a_store_of_only_favourites_refuses_a_new_favourite_and_says_so
mutate contacts_overwrite_event_missing $CS '      _listener->onContactEvicted(key);                    // the stock overwrite event, before the record goes' '' unit:test_contact_store
mutate contacts_overwrite_event_missing_sim $CS '      _listener->onContactEvicted(key);                    // the stock overwrite event, before the record goes' '' $CT_SIM::test_a_full_store_evicts_the_least_meaningful_and_never_a_favourite
mutate contacts_message_history_ignored $CS '  if (e.last_msg && (now < e.last_msg || now - e.last_msg <= CONTACT_RECENT_SECS)) return 2;
  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.flags & F_PATH) return 1;' unit:test_contact_store
mutate contacts_message_history_ignored_sim $CS '  if (e.last_msg && (now < e.last_msg || now - e.last_msg <= CONTACT_RECENT_SECS)) return 2;
  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.flags & F_PATH) return 1;' $CT_SIM::test_a_contact_with_message_history_outlives_an_advert_only_one
mutate contacts_old_message_counts_for_nothing $CS '  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.flags & F_PATH) return 1;' unit:test_contact_store
mutate contacts_recent_message_window_ignored $CS '  if (e.last_msg && (now < e.last_msg || now - e.last_msg <= CONTACT_RECENT_SECS)) return 2;' '' unit:test_contact_store
mutate contacts_known_path_ignored $CS '  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.last_msg) return 1;' unit:test_contact_store
mutate contacts_stock_policy_always $CS '    if (_cfg.stock_policy) { cls = 0; t = e.lastmod; }' '    if (true) { cls = 0; t = e.lastmod; }' $CT_SIM::test_a_contact_with_message_history_outlives_an_advert_only_one
mutate contacts_stock_policy_always_unit $CS '    if (_cfg.stock_policy) { cls = 0; t = e.lastmod; }' '    if (true) { cls = 0; t = e.lastmod; }' unit:test_contact_store
mutate contacts_stock_array_build_evicts_by_score src/helpers/BaseChatMesh.cpp '  cfg.stock_policy = true;' '  cfg.stock_policy = false;' $CT_SIM::test_the_stock_array_build_is_ram_only_and_atomic_and_evicts_the_oldest
mutate contacts_message_sent_not_noted src/helpers/BaseChatMesh.cpp '  noteDirectMessage(recipient);   // we wrote to them: they are worth keeping' '' $CT_SIM::test_a_contact_with_message_history_outlives_an_advert_only_one
mutate contacts_message_received_not_noted src/helpers/BaseChatMesh.cpp '      noteDirectMessage(from);' '' $CT_SIM::test_a_contact_that_wrote_to_us_outlives_advert_only_ones
mutate contacts_message_time_not_kept $CS '  _idx[rec].last_msg = now;' '' unit:test_contact_store
mutate contacts_secrets_not_invalidated $CS '  for (int i = 0; i < _nslots; i++) if (_slots && _slots[i].rec >= 0) _slots[i].ci.shared_secret_valid = false;' '' unit:test_contact_store
mutate contacts_bulk_import_writes_each_record src/helpers/ContactStore.cpp '  if (b != _import_bucket) {' '  if (true) {' unit:test_contact_store
mutate contacts_rebuild_probes_each_bucket src/helpers/ContactStore.cpp '  const bool have_list = listed && _be->listBuckets((uint16_t)_nbuckets, listed);' '  const bool have_list = false;' unit:test_contact_store
mutate contacts_rebuild_probes_each_bucket_sim src/helpers/ContactStore.cpp '  const bool have_list = listed && _be->listBuckets((uint16_t)_nbuckets, listed);' '  const bool have_list = false;' sim/tests/test_contacts.py::test_rebuild_looks_up_no_bucket_that_is_not_there
# rescue "rebuild" works from the store (docs/CONTACTS.md)
CT_KEEP=$CT_SIM::test_rebuild_keeps_every_stored_contact_beyond_the_cache
CT_ISO=$CT_SIM::test_rebuild_isolates_a_damaged_record_and_keeps_all_the_others
CT_CUT=$CT_SIM::test_a_power_cut_during_rebuild_keeps_the_previous_buckets
mutate contacts_rebuild_cache_only $CS '        if (!zero && decode(r, c, lm)) {' '        if (false) {' "$CT_KEEP $CT_ISO"
mutate contacts_rebuild_cache_only_unit $CS '        if (!zero && decode(r, c, lm)) {' '        if (false) {' unit:test_contact_store
mutate contacts_rebuild_damaged_bucket_not_kept_aside $CS '      if (!zero && !decode(r, c, lm)) any_bad = true;' '' "$CT_ISO"
mutate contacts_rebuild_damaged_bucket_not_kept_aside_unit $CS '      if (!zero && !decode(r, c, lm)) any_bad = true;' '' unit:test_contact_store
mutate contacts_rebuild_damaged_record_kept $CS '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }
          memset(r, 0, REC);' '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }' "$CT_ISO"
mutate contacts_rebuild_damaged_record_kept_unit $CS '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }
          memset(r, 0, REC);' '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }' unit:test_contact_store
mutate contacts_rebuild_rewritten_after_remove $CS '    if (any) ok = writeB(b);
    else ok = have ? removeB(b) : true;
    if (!ok) { _write_errors++; continue; }' '    removeB(b);
    if (any) ok = writeB(b);
    else ok = have ? removeB(b) : true;
    if (!ok) { _write_errors++; continue; }' "$CT_CUT"
mutate contacts_rebuild_formats_filesystem examples/companion_radio/MyMesh.cpp '      int damaged = 0, isolated = 0;' '      _store->formatFileSystem();
      int damaged = 0, isolated = 0;' "$CT_KEEP $CT_ISO"
mutate contacts_rebuild_damaged_not_reported $CS '  if (damaged_out) *damaged_out = damaged;
  if (isolated_out)' '  if (isolated_out)' "$CT_ISO"
mutate contacts_rebuild_damaged_not_reported_unit $CS '  if (damaged_out) *damaged_out = damaged;
  if (isolated_out)' '  if (isolated_out)' unit:test_contact_store
mutate contacts_device_query_wraps examples/companion_radio/MyMesh.cpp '      out_frame[i++] = half > 255 ? 255 : half;   // v3+' '      out_frame[i++] = half;   // v3+' $CT_SIM::test_device_query_reports_half_the_capacity_capped_at_one_byte
