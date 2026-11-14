# packet.transmit's checks on a manager-supplied native packet (#537): src/helpers/iface/PacketTransmit.h
mutate raw_any_version src/helpers/iface/PacketTransmit.h '  if (((header >> PH_VER_SHIFT) & PH_VER_MASK) != PAYLOAD_VER_1) return "malformed";' '  if (false) return "malformed";' unit:test_packet_transmit
mutate raw_codes_unchecked src/helpers/iface/PacketTransmit.h '    if (n - i < 4) return "malformed";' '    if (false) return "malformed";' unit:test_packet_transmit
mutate raw_payload_unbounded src/helpers/iface/PacketTransmit.h '  if (payload_len < 1 || payload_len > MAX_PACKET_PAYLOAD) return "malformed";' '  if (payload_len < 1) return "malformed";' unit:test_packet_transmit
mutate raw_adverts_allowed src/helpers/iface/PacketTransmit.h '    case PAYLOAD_TYPE_GRP_TXT: case PAYLOAD_TYPE_GRP_DATA:' '    case PAYLOAD_TYPE_ADVERT: case PAYLOAD_TYPE_GRP_TXT: case PAYLOAD_TYPE_GRP_DATA:' unit:test_packet_transmit
# raw_path_unchecked is equivalent: path bytes are read only once every check has passed, and without it the wrapped payload length fails the payload bound.
# The Repeater operation (stage 2): src/helpers/iface/ConsoleIface.cpp, examples/simple_repeater/MyMesh.cpp
P=sim/tests/test_packet_transmit.py
mutate ptx_unsigned_cable_may src/helpers/iface/ConsoleIface.cpp '  if (_ctx.auth != Cli::A_SIGNED) { c.error = "signature-required"; return ST_REFUSED; }' '  if (false) { c.error = "signature-required"; return ST_REFUSED; }' $P::test_only_a_signed_caller_may_and_only_allowed_well_formed_packets_go
mutate ptx_seen_not_checked src/helpers/iface/ConsoleIface.cpp '  if (_raw.seen(_raw.user, pkt)) { c.error = "already-heard"; return ST_REFUSED; }' '  if (false) { c.error = "already-heard"; return ST_REFUSED; }' $P::test_a_repeater_transmits_a_captured_packet_once_and_a_stock_companion_receives_it
mutate ptx_sent_not_marked_seen examples/simple_repeater/MyMesh.cpp '  getTables()->markSeen(q);' '  (void)q;' $P::test_a_repeater_transmits_a_captured_packet_once_and_a_stock_companion_receives_it
mutate ptx_sent_reported_failed src/helpers/iface/ConsoleIface.cpp '  rawFinish(ok ? JOB_APPLIED : JOB_FAILED, ok ? "transmitted" : "failed");' '  rawFinish(JOB_FAILED, "failed");' $P::test_a_repeater_transmits_a_captured_packet_once_and_a_stock_companion_receives_it
# ptx_same_job_sent_again is equivalent: packet.transmit takes signed callers only, and DeployAuth answers the same signed
# job id from its result cache (or duplicate/replay) before the handler runs, so the handler's FOUND branch is never reached.
