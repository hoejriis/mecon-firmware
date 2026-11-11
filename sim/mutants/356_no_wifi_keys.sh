# A Companion on a board without Wi-Fi lists no Wi-Fi key and no connectivity mode (#485)
T=sim/tests/test_no_wifi_companion.py
mutate nowifi_keys_unguarded src/helpers/iface/IfaceConfig.cpp '#ifdef ENABLE_WIFI_INTERFACE   // a board without Wi-Fi has neither the mode switch nor the profiles: `invalid`, as any key it lacks' '#if 1' "$T"
