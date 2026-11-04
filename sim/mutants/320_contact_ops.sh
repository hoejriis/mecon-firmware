# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# contact.* on the native contact table: paging by record, a repeat writes nothing, the flags stay the contact's own, a
# full table refuses, every change reaches the lazy save, and a write class that an unsigned caller does not have
mutate contact_list_always_has_next src/helpers/iface/ContactOps.cpp '  w.key("next"); if (more) w.num(at); else w.null();' '  w.key("next"); w.num(at);' unit:test_contact_ops
mutate contact_list_next_skips_the_pending_contact src/helpers/iface/ContactOps.cpp '  w.key("next"); if (more) w.num(at); else w.null();' '  w.key("next"); if (more) w.num(cur); else w.null();' unit:test_contact_ops
mutate contact_list_page_ignores_the_result_budget src/helpers/iface/ContactOps.cpp ' && w.size() + CONTACT_ENTRY_WORST <= CONTACT_LIST_BUDGET) {' ') {' unit:test_contact_ops
mutate contact_put_rewrites_an_unchanged_contact src/helpers/iface/ContactOps.cpp '  if (changed) {   // a repeat of a put' '  if (true) {   // a repeat of a put' unit:test_contact_ops
mutate contact_put_new_contact_without_a_type src/helpers/iface/ContactOps.cpp '  if (!existed && (!r.name[0] || !r.type)) return ST_INVALID;' '  if (!existed && !r.name[0]) return ST_INVALID;' unit:test_contact_ops
mutate contact_put_full_table_reported_as_write_failed src/helpers/iface/ContactOps.cpp '    if (st == CW_FULL) { c.error = "table-full"; return ST_FAILED; }' '' unit:test_contact_ops
mutate contact_remove_of_an_absent_key_fails src/helpers/iface/ContactOps.cpp '  const bool removed = h->remove(h->user, key) > 0;   // absent is ok: a retry of a remove that took effect says so' '  const bool removed = h->remove(h->user, key) > 0;
  if (!removed) { c.error = "not-found"; return ST_FAILED; }' unit:test_contact_ops
mutate contact_put_clears_the_contacts_flags examples/companion_radio/MeconIface.cpp '    have->type = in.type;
' '    have->type = in.type;
    have->flags = 0;
' sim/tests/test_contact_ops.py::test_a_put_keeps_the_grants_and_the_path_of_a_contact_and_cannot_grant
mutate contact_new_put_not_scheduled_for_save examples/companion_radio/MeconIface.cpp '  if (!added) return CW_FULL;
  mesh->markContactsDirty();' '  if (!added) return CW_FULL;' sim/tests/test_contact_ops.py::test_every_change_is_written_by_the_lazy_save_without_a_reboot
mutate contact_remove_not_scheduled_for_save examples/companion_radio/MeconIface.cpp '  mesh->deleteContactBlob(key);
  mesh->markContactsDirty();' '  mesh->deleteContactBlob(key);' sim/tests/test_contact_ops.py::test_every_change_is_written_by_the_lazy_save_without_a_reboot
mutate contact_protect_not_scheduled_for_save examples/companion_radio/MeconIface.cpp '  have->flags = on ? (uint8_t)(have->flags | 0x01) : (uint8_t)(have->flags & ~0x01);
  mesh->markContactsDirty();' '  have->flags = on ? (uint8_t)(have->flags | 0x01) : (uint8_t)(have->flags & ~0x01);' sim/tests/test_contact_ops.py::test_every_change_is_written_by_the_lazy_save_without_a_reboot
mutate contact_unprotect_clears_every_flag examples/companion_radio/MeconIface.cpp ': (uint8_t)(have->flags & ~0x01);' ': (uint8_t)0;' sim/tests/test_contact_ops.py::test_a_put_keeps_the_grants_and_the_path_of_a_contact_and_cannot_grant
mutate contact_put_is_a_read examples/companion_radio/MeconIface.cpp '  {"contact.put", opContPut, PRIV_WRITE, true},' '  {"contact.put", opContPut, PRIV_READ, true},' sim/tests/test_deployment_authority.py::test_an_unsigned_contact_write_is_refused_once_enrolled_and_contact_reads_work
mutate contact_protect_is_a_read examples/companion_radio/MeconIface.cpp '  {"contact.protect", opContProtect, PRIV_WRITE, true},' '  {"contact.protect", opContProtect, PRIV_READ, true},' sim/tests/test_deployment_authority.py::test_an_unsigned_contact_write_is_refused_once_enrolled_and_contact_reads_work
mutate contact_remove_is_a_read examples/companion_radio/MeconIface.cpp '  {"contact.remove", opContRemove, PRIV_WRITE, true},' '  {"contact.remove", opContRemove, PRIV_READ, true},' sim/tests/test_deployment_authority.py::test_an_unsigned_contact_write_is_refused_once_enrolled_and_contact_reads_work
mutate contact_put_not_kept_for_a_retry examples/companion_radio/MeconIface.cpp '  {"contact.put", opContPut, PRIV_WRITE, true},' '  {"contact.put", opContPut, PRIV_WRITE, false},' sim/tests/test_contact_ops.py::test_a_repeated_request_is_the_same_state_and_the_same_answer
mutate contact_update_not_scheduled_for_save examples/companion_radio/MeconIface.cpp '    have->lastmod = now;
    mesh->markContactsDirty();' '    have->lastmod = now;' sim/tests/test_contact_ops.py::test_every_change_is_written_by_the_lazy_save_without_a_reboot
mutate hello_capabilities_not_paged examples/companion_radio/MeconIface.cpp '    if (w.size() + strlen(cap) + 3 > room) { next = (int)idx - 1; return; }' '' sim/tests/test_contact_ops.py::test_hello_with_a_nonce_on_an_enrolled_device_pages_its_capabilities_instead_of_failing
mutate hello_cursor_ignored examples/companion_radio/MeconIface.cpp '    if (idx++ < (unsigned)from || next >= 0) return;' '    if (idx++ < 0u || next >= 0) return;' sim/tests/test_contact_ops.py::test_hello_with_a_nonce_on_an_enrolled_device_pages_its_capabilities_instead_of_failing
