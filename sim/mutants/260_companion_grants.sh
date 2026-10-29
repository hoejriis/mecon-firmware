# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the Companion frame gate on contact grants, the device time and an untrusted contact's modification time
CG=sim/tests/test_companion_frame_gate.py
mutate frame_gate_time_ungated examples/companion_radio/MyMesh.cpp '      return _clock_set ? Cli::FR_APP_WRITE : Cli::FR_NONE;' '      return Cli::FR_NONE;' $CG::test_setting_a_set_clock_is_refused_over_tcp
mutate frame_gate_time_always_gated examples/companion_radio/MyMesh.cpp '      return _clock_set ? Cli::FR_APP_WRITE : Cli::FR_NONE;' '      return Cli::FR_APP_WRITE;' $CG::test_the_first_set_after_a_reboot_with_contacts_is_open
mutate frame_gate_time_set_never_noted examples/companion_radio/MyMesh.cpp '      _clock_set = true;' '' $CG::test_setting_a_set_clock_is_refused_over_tcp
mutate frame_gate_contact_grant_ungated examples/companion_radio/MyMesh.cpp '& ~held & CONTACT_GRANT_FLAGS) ? Cli::FR_APP_WRITE' '& 0) ? Cli::FR_APP_WRITE' "$CG::test_granting_a_contact_flag_is_refused_over_tcp[remote-cli]"
mutate frame_gate_contact_held_grant_ignored examples/companion_radio/MyMesh.cpp '      const uint8_t held = had ? had->flags : 0;' '      const uint8_t held = 0; (void)had;' $CG::test_a_grant_held_from_the_cable_survives_tcp_edits_and_can_be_taken_away
mutate frame_gate_contact_telemetry_ungated examples/companion_radio/MyMesh.cpp '#define CONTACT_GRANT_FLAGS       0x1E' '#define CONTACT_GRANT_FLAGS       0x10' "$CG::test_granting_a_contact_flag_is_refused_over_tcp[telemetry]"
mutate contact_untrusted_lastmod_not_held examples/companion_radio/MyMesh.cpp '    const bool clamp = !serialTrusted();' '    const bool clamp = false;' $CG::test_an_untrusted_contact_time_is_held_at_the_clock
mutate contact_cable_lastmod_held examples/companion_radio/MyMesh.cpp '    const bool clamp = !serialTrusted();' '    const bool clamp = true;' $CG::test_a_cable_contact_time_is_kept
