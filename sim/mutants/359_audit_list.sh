# audit.list: the audit ring's history over the interface (#491)
T=sim/tests/test_audit_list.py
F=src/helpers/iface/AuditOps.cpp
mutate audit_list_no_posture_check $F '  if (!allow || !allow(user, Cli::C_POSTURE)) { c.error = "not-permitted"; return ST_REFUSED; }' '' "$T"
mutate audit_list_oldest_first $F '    const Posture::AuditEntry& a = r.e[total - 1 - i];   // e[n-1] is the newest' '    const Posture::AuditEntry& a = r.e[i];' "$T"
mutate audit_list_next_never_set $F '  if (i < total) w.unum(i); else w.null();' '  w.null();' "$T"
