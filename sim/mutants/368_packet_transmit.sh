# packet.transmit's checks on a manager-supplied native packet (#537): src/helpers/iface/PacketTransmit.h
mutate raw_any_version src/helpers/iface/PacketTransmit.h '  if (((header >> PH_VER_SHIFT) & PH_VER_MASK) != PAYLOAD_VER_1) return "malformed";' '  if (false) return "malformed";' unit:test_packet_transmit
mutate raw_codes_unchecked src/helpers/iface/PacketTransmit.h '    if (n - i < 4) return "malformed";' '    if (false) return "malformed";' unit:test_packet_transmit
mutate raw_path_unchecked src/helpers/iface/PacketTransmit.h '  if (n - i < path_bytes) return "malformed";' '  if (false) return "malformed";' unit:test_packet_transmit
mutate raw_payload_unbounded src/helpers/iface/PacketTransmit.h '  if (payload_len < 1 || payload_len > MAX_PACKET_PAYLOAD) return "malformed";' '  if (payload_len < 1) return "malformed";' unit:test_packet_transmit
mutate raw_adverts_allowed src/helpers/iface/PacketTransmit.h '    case PAYLOAD_TYPE_GRP_TXT: case PAYLOAD_TYPE_GRP_DATA:' '    case PAYLOAD_TYPE_ADVERT: case PAYLOAD_TYPE_GRP_TXT: case PAYLOAD_TYPE_GRP_DATA:' unit:test_packet_transmit
