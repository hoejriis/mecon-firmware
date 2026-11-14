# A management request to a target with no known path goes zero-hop first (#542): src/helpers/iface/RfRoute.h, examples/companion_radio/MyMesh.cpp
T1=sim/tests/test_lora_management.py::test_a_request_to_a_contact_with_no_path_is_not_rebroadcast_into_its_reply
T2=sim/tests/test_lora_management.py::test_a_target_beyond_zero_hop_is_reached_by_flood_after_the_wait
mutate rfroute_always_flood examples/companion_radio/MyMesh.cpp '    else sendZeroHop(pkt);' '    else sendFloodScoped(*recipient, pkt);' $T1
mutate rfroute_never_flood src/helpers/iface/RfRoute.h '    return e->mode == FLOOD;' '    return false;' $T2
mutate rfroute_answer_misread src/helpers/iface/RfRoute.h '    e->mode = e->pending_flood ? FLOOD : ZERO_HOP;' '    e->mode = FLOOD;' unit:test_rf_route
mutate rfroute_no_wait src/helpers/iface/RfRoute.h '    if (e->pending && !e->pending_flood && (uint32_t)(now - e->pending_ms) >= WAIT_MS) {' '    if (e->pending && !e->pending_flood) {' unit:test_rf_route
