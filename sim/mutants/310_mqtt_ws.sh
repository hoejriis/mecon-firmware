# MQTT over WebSocket (docs/MQTT.md, "WebSocket"): the frame and handshake rules, the client, and the profile fields
W=src/helpers/mqtt/MqttWs.cpp
WT=sim/tests/test_mqtt_ws.py
mutate ws_masked_server_frame_taken $W '      if (b & 0x80) break;   // a server never masks' '' unit:test_mqtt_ws
mutate ws_text_frame_taken $W '        break;                     // text (MQTT is binary) or a reserved data opcode' '        ;' unit:test_mqtt_ws
mutate ws_reserved_bit_taken $W '      if (b & 0x70) break;   // a reserved bit: no extension was agreed' '' unit:test_mqtt_ws
mutate ws_long_control_frame_taken $W '      if (_op >= 8 && l > 125) break;' '' unit:test_mqtt_ws
mutate ws_fragmented_control_frame_taken $W '        if ((_op != OP_CLOSE && _op != OP_PING && _op != OP_PONG) || !_fin) break;' '        if (_op != OP_CLOSE && _op != OP_PING && _op != OP_PONG) break;' unit:test_mqtt_ws
mutate ws_continuation_of_nothing_taken $W '        if (!_in_message) break;   // a continuation of nothing' '' unit:test_mqtt_ws
mutate ws_new_message_inside_one_taken $W '        if (_in_message) break;    // a new message inside a fragmented one' '' unit:test_mqtt_ws
mutate ws_accept_unchecked $W '    else if (eqNoCase(p, nn, "sec-websocket-accept")) accept = vn == 28 && memcmp(v, want, 28) == 0;' '    else if (eqNoCase(p, nn, "sec-websocket-accept")) accept = true;' unit:test_mqtt_ws
mutate ws_protocol_unchecked $W '  if (!protocol) return Answer::Protocol;' '' unit:test_mqtt_ws
mutate ws_extension_ignored $W '  if (extension) return Answer::Extension;' '' unit:test_mqtt_ws
mutate ws_status_unchecked $W '    return Answer::Status;' '    ;' unit:test_mqtt_ws
mutate ws_connection_token_unchecked $W '    else if (eqNoCase(p, nn, "connection")) connection = hasToken(v, vn, "upgrade");' '    else if (eqNoCase(p, nn, "connection")) connection = true;' unit:test_mqtt_ws
mutate ws_sha1_padding_one_block_short $W '  const size_t total = rest + 1 + 8 <= 64 ? 64 : 128;' '  const size_t total = rest + 1 <= 64 ? 64 : 128;' unit:test_mqtt_ws
mutate ws_16_bit_length_one_short $W '  } else if (len <= 0xFFFF) {' '  } else if (len < 0xFFFF) {' unit:test_mqtt_ws
mutate ws_frames_unmasked $W '    out[1] = (uint8_t)(0x80 | len);' '    out[1] = (uint8_t)len;' unit:test_mqtt_ws
mutate ws_host_without_port $W '  if (port != (tls ? 443 : 80)) snprintf(port_text, sizeof(port_text), ":%u", (unsigned)port);' '' unit:test_mqtt_ws
mutate ws_ping_not_answered src/helpers/mqtt/MqttWsClient.cpp '        if (!sendFrame(MqttWs::OP_PONG, _dec.ctrl(), _dec.ctrlLen())) { _open = false; return; }' '        ;' "$WT::test_fragmented_messages_with_pings_between_them"
mutate ws_upgrade_skipped src/helpers/mqtt/MqttController.cpp '  if (j.ws) {
    // The upgrade runs' '  if (false) {
    // The upgrade runs' "$WT::test_mqtt_over_plain_websocket_connects_and_the_profile_survives_a_reboot"
mutate ws_path_unchecked src/helpers/mqtt/MqttProfiles.cpp 'static bool pathOk(const char* s) {' 'static bool pathOk(const char* s) {
  return true;' unit:test_mqtt_prefs
mutate ws_on_a_discovery_profile src/helpers/mqtt/MqttProfiles.h '  bool useWs() const { return kind == Static && ws != 0; }' '  bool useWs() const { return ws != 0; }' unit:test_mqtt_prefs
mutate ws_listing_hides_the_transport src/helpers/mqtt/MqttProfiles.cpp '  const char* ws = q.ws ? " transport=ws" : "";' '  const char* ws = "";' unit:test_mqtt_prefs
