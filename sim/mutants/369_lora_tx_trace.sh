# The LoRa management send trace (#542): src/helpers/iface/LoraTxTrace.h, examples/companion_radio/MyMesh.cpp
T=sim/tests/test_lora_management.py::test_a_companions_whole_status_reaches_a_companion_manager
mutate lora_tx_sent_not_counted src/helpers/iface/LoraTxTrace.h '    Slot* s = find(hash);' '    Slot* s = nullptr;' $T
mutate lora_tx_reply_first_misread examples/companion_radio/MyMesh.cpp '  traceQueued(reply, n > 5 ? frame[5] : 0xFF);' '  traceQueued(reply, n > 5 ? frame[4] : 0xFF);' $T
mutate lora_tx_request_untraced examples/companion_radio/MyMesh.cpp '  traceQueued(pkt, n > 2 ? frame[2] : 0xFF);' '  (void)pkt;' $T
