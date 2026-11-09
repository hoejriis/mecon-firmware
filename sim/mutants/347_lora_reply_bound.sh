# A reply up to the LoRa bound is reassembled though its frames, all full, would pass it (#457)
TS=sim/tests/test_lora_management.py
mutate lora_bound_from_full_frames src/helpers/iface/LoraLink.h '        (size_t)(cnt - 1) * payload + (idx + 1 == cnt ? blen : 1) > LORA_MSG_MAX) {' '        (size_t)(cnt - 1) * payload + blen > LORA_MSG_MAX) {' "$TS::test_a_companions_whole_status_reaches_a_companion_manager"
