# mqtt.profile.anchor in pieces (#654): the console's staging with an offset and a size and SHA-256 promised at begin
T=unit:test_net_profile_ops
F=src/helpers/mqtt/MqttProfiles.cpp
G=src/helpers/iface/NetProfileOps.cpp
mutate anchor_piece_out_of_order_accepted $F '          if (off != pr.stage_len_) {' '          if (false) {' "$T"
mutate anchor_end_without_hash_check $F '    if ((uint16_t)n != size || memcmp(got, sha, sizeof(got)) != 0) return CA_MISMATCH;' '    if ((uint16_t)n != size) return CA_MISMATCH;' "$T"
mutate anchor_end_without_size_check $F '    if ((uint16_t)n != size || memcmp(got, sha, sizeof(got)) != 0) return CA_MISMATCH;' '    if (memcmp(got, sha, sizeof(got)) != 0) return CA_MISMATCH;' "$T"
mutate anchor_stage_never_times_out $F '  if (stage_ && (uint32_t)(now - stage_at_) >= STAGE_TIMEOUT_MS) dropStage();' '  if (false) dropStage();' "$T"
mutate anchor_add_does_not_refresh_the_timeout $F '        pr.stage_len_ = (uint16_t)(pr.stage_len_ + n);
        pr.stage_at_ = millis();' '        pr.stage_len_ = (uint16_t)(pr.stage_len_ + n);' "$T"
mutate anchor_refused_end_keeps_the_buffer $F '        const MqttProfiles::CaResult r = pr.installCa(idx, pr.stage_, pr.stage_expect_, pr.stage_size_, pr.stage_sha_);
        pr.dropStage();' '        const MqttProfiles::CaResult r = pr.installCa(idx, pr.stage_, pr.stage_expect_, pr.stage_size_, pr.stage_sha_);
        if (r == MqttProfiles::CA_OK) pr.dropStage();' "$T"
mutate anchor_op_sends_pieces_without_the_offset $G '"set mqtt.profile %u ca add @%u %.*s", idx, (unsigned)(off + at), (int)k, b64 + at' '"set mqtt.profile %u ca add %.*s", idx, (int)k, b64 + at' "$T"
mutate anchor_op_begin_drops_the_hash $G '"set mqtt.profile %u ca begin %u %s", idx, (unsigned)size, hex' '"set mqtt.profile %u ca begin", idx' "$T"
mutate anchor_pieces_in_use_ignored $G '  if (!h.cable && in_use != NET_NONE && (unsigned)n == in_use) { c.error = "in-use"; return ST_REFUSED; }' '' "$T"
mutate anchor_not_staged_unnamed $G '    if (strstr(reply, "ca begin first")) c.error = "not-staged";' '    if (false) c.error = "not-staged";' "$T"
mutate anchor_offset_unnamed $G '    else if (strstr(reply, "is not the staged length")) c.error = "offset";' '' "$T"
