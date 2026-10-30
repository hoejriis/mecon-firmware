# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the management channel binding (MANAGEMENT_CHANNEL.md): never the public channel, stale when the slot changed,
# confirm-or-revert for anyone but the cable, a revert that restores the key, and the Companion's hook wired
MGMT_SIM=sim/tests/test_mgmt_channel.py
mutate mgmt_public_channel_bindable src/helpers/iface/MgmtChannel.cpp '  if (sameFp(fp, h.public_fp)) { c.error = "public-channel"; return ST_FAILED; }   // everyone holds that key' '' unit:test_mgmt_channel
mutate mgmt_stale_check_skipped src/helpers/iface/MgmtChannel.cpp '  if (rec->slot >= h.channels->capacity || !h.slot_fp(h.user, rec->slot, fp) || !sameFp(fp, rec->fp)) return MGMT_STALE;' '  if (rec->slot >= h.channels->capacity) return MGMT_STALE;' unit:test_mgmt_channel
mutate mgmt_remote_without_confirm src/helpers/iface/MgmtChannel.cpp '  if (conf < 0 && !cable) { c.error = "confirm-required"; return ST_INVALID; }' '' unit:test_mgmt_channel
mutate mgmt_revert_skips_the_slot src/helpers/iface/MgmtChannel.cpp '    if (applied && !untouched) ok = h.channels->set(h.channels->user, s->slot, s->name, s->key);' '    if (false) ok = false;' unit:test_mgmt_channel
mutate mgmt_snapshot_without_the_key src/helpers/iface/MgmtChannel.cpp '  memcpy(out + at, key, CHANNEL_SECRET_BYTES); at += CHANNEL_SECRET_BYTES;' '  memset(out + at, 0, CHANNEL_SECRET_BYTES); at += CHANNEL_SECRET_BYTES;' unit:test_mgmt_channel
mutate mgmt_guard_ignores_the_bound_slot src/helpers/iface/MgmtChannel.cpp ' && (int)v == rec.slot;' ' && false;' unit:test_mgmt_channel
mutate mgmt_revert_overwrites_an_app_change src/helpers/iface/MgmtChannel.cpp '    if (applied && !untouched) ok = h.channels->set(h.channels->user, s->slot, s->name, s->key);' '    if (!untouched) ok = h.channels->set(h.channels->user, s->slot, s->name, s->key);' unit:test_mgmt_channel
mutate mgmt_unneeded_confirm_ignored src/helpers/iface/MgmtChannel.cpp '    if (confirmAsked(c)) { c.error = "confirm-not-needed"; return ST_INVALID; }   // not the bound slot: nothing to protect' '' unit:test_mgmt_channel
mutate mgmt_stale_slot_removed src/helpers/iface/MgmtChannel.cpp '  if (remove && st != MGMT_BOUND) { c.error = "stale"; return ST_FAILED; }' '' unit:test_mgmt_channel
mutate mgmt_unreadable_record_names_slot_0 src/helpers/iface/MgmtChannel.cpp '    rec->slot = MGMT_NO_SLOT;' '' unit:test_mgmt_channel
mutate mgmt_snapshot_kept_after_a_failed_revert src/helpers/iface/MgmtChannel.cpp '  h.snap->discard();' '  if (ok) h.snap->discard();' unit:test_mgmt_channel
mutate mgmt_revert_hook_not_wired examples/companion_radio/MeconDeploy.cpp '  if (Iface::mgmtOwnsJob(j)) {   // the management channel'"'"'s ops, by exact name (MgmtChannel.cpp)' '  if (false) {' $MGMT_SIM::test_a_paired_phone_must_confirm_and_an_unconfirmed_bind_reverts_at_its_deadline
