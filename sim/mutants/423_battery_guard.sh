# The battery guard's settings, CLI and dry run: src/helpers/BatteryGuard.h, BatteryGuardCfg.h, BatteryGuardPrefs.h
TOPIC=423_battery_guard
BG=unit:test_battery_guard
BS=sim/tests/test_battery_guard.py
# validating the proposal's fields one by one against the stored others (the earlier firmware's behaviour), not the combination
mutate guard_validates_against_stored src/helpers/BatteryGuard.h '  const char* e = validate(c);
  if (e) return e;' '  Cfg a = bl.settings(), b = bl.settings();
  a.low_mv = c.low_mv; a.wake_s = c.wake_s; a.margin_mv = c.margin_mv;
  b.critical_mv = c.critical_mv;
  const char* e = validate(a);
  if (!e) e = validate(b);
  if (e) return e;' $BG
mutate guard_validates_against_stored_sim src/helpers/BatteryGuard.h '  const char* e = validate(c);
  if (e) return e;' '  Cfg a = bl.settings(), b = bl.settings();
  a.low_mv = c.low_mv; a.wake_s = c.wake_s; a.margin_mv = c.margin_mv;
  b.critical_mv = c.critical_mv;
  const char* e = validate(a);
  if (!e) e = validate(b);
  if (e) return e;' $BS::test_the_interface_checks_the_whole_value_and_refuses_it_all_or_changes_it_all
# low equal to critical, or less than 100 above it, accepted
mutate guard_low_not_above_critical src/helpers/BatteryGuardCfg.h 'c.low_mv < c.critical_mv + Range::MIN_GAP' 'c.low_mv < c.critical_mv' $BG
mutate guard_low_not_above_critical_sim src/helpers/BatteryGuardCfg.h 'c.low_mv < c.critical_mv + Range::MIN_GAP' 'c.low_mv < c.critical_mv' $BS::test_the_interface_checks_the_whole_value_and_refuses_it_all_or_changes_it_all
# the would-sleep-now refusal removed
mutate guard_would_sleep_now_removed src/helpers/BatteryGuard.h 'if (decide(c, bl, sup, external).would_sleep) return "would-sleep-now";' '(void)external;' $BG
mutate guard_would_sleep_now_removed_sim src/helpers/BatteryGuard.h 'if (decide(c, bl, sup, external).would_sleep) return "would-sleep-now";' '(void)external;' $BS::test_arming_at_critical_is_refused_would_sleep_now
# a dry run that stores the proposal
mutate guard_dry_writes src/helpers/BatteryGuard.h '    const char* refusal = check(arg, bl, supported(), external, &c);' '    const char* refusal = check(arg, bl, supported(), external, &c);
    if (!refusal) bl.settings() = c;' $BG
mutate guard_dry_writes_sim src/helpers/BatteryGuard.h '    const char* refusal = check(arg, bl, supported(), external, &c);' '    const char* refusal = check(arg, bl, supported(), external, &c);
    if (!refusal) bl.settings() = c;' $BS::test_the_dry_run_answers_and_changes_nothing
# an unsigned write allowed on an enrolled device: the set line classed as a read
mutate guard_unsigned_write_allowed src/helpers/cli/CliRegistry.h '    // reads
' '    // reads
    {"set battery.guard",      M_WORD,    ROLE_COMPANION | ROLE_REPEATER, C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
' $BS::test_once_enrolled_an_unsigned_write_is_refused_and_a_signed_one_confirms
# the settings not persisted: the thresholds missing from the prefs group
mutate guard_thresholds_not_persisted src/helpers/BatteryGuardPrefs.h '    def("low", c.low_mv);
    def("crit", c.critical_mv);' '    (void)c;' $BG
mutate guard_thresholds_not_persisted_sim src/helpers/BatteryGuardPrefs.h '    def("low", c.low_mv);
    def("crit", c.critical_mv);' '    (void)c;' $BS::test_defaults_and_the_cli_round_trip
mutate guard_enabled_not_persisted src/helpers/BatteryGuardPrefs.h '    def("en", c.enabled);' '    (void)c.enabled;' $BG
mutate guard_enabled_not_persisted_sim src/helpers/BatteryGuardPrefs.h '    def("en", c.enabled);' '    (void)c.enabled;' $BS::test_a_radio_reset_keeps_the_guard_and_a_factory_reset_clears_it
# the classifier ignoring the stored margin, or not re-classifying when the settings change
mutate guard_classifier_fixed_margin src/helpers/BatteryLevel.h 'const uint16_t up_to_ok = c.low_mv + c.margin_mv;' 'const uint16_t up_to_ok = c.low_mv + 100;' $BG
mutate guard_not_reclassified src/helpers/BatteryLevel.h '    _level = classify(_mean, _cfg, true, _level);' '    (void)_mean;' $BG
# the whole-request check before any write removed from config.apply
mutate guard_apply_precheck_removed src/helpers/iface/IfaceConfig.cpp 'if (allowed && strcmp(k->key, "battery.guard") == 0) {' 'if (false && strcmp(k->key, "battery.guard") == 0) {' $BS::test_the_interface_checks_the_whole_value_and_refuses_it_all_or_changes_it_all
# the key losing its confirm flag (the Companion's entry; the Repeater's is one comment further down)
mutate guard_confirm_dropped src/helpers/iface/IfaceConfig.cpp '  // Since v0.3.12. One composite value, checked as a whole on the device against the stored one; this release never sleeps.
  {"battery.guard", K_STRING, 1, 63, nullptr, KF_WRITABLE | KF_CONFIRM,' '  // Since v0.3.12. One composite value, checked as a whole on the device against the stored one; this release never sleeps.
  {"battery.guard", K_STRING, 1, 63, nullptr, KF_WRITABLE,' unit:test_iface_config
# the dry run of config.apply writing through the normal path
mutate guard_dry_apply_not_dry src/helpers/iface/IfaceConfig.cpp '      return applyDry(c, h, set);' '      (void)applyDry;' $BS::test_the_dry_run_answers_and_changes_nothing
# the health stream claiming the guard acts
mutate guard_health_effective src/helpers/iface/Streams.h 'w.key("effective"); w.str("report-only");' 'w.key("effective"); w.str(bl.settings().enabled ? "active" : "report-only");' $BS::test_the_health_stream_carries_battery_guard
