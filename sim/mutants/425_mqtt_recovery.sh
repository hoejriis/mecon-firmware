# The reconnect recovery rule (docs/MQTT.md, Issue #651): src/helpers/mqtt/MqttRecovery.h
RU=unit:test_mqtt_recovery
mutate mqtt_recovery_fires_without_a_low_heap src/helpers/mqtt/MqttRecovery.h "if (_fails >= FAILS_NEEDED && heap_low && _armed) {" "if (_fails >= FAILS_NEEDED && _armed) {" $RU
mutate mqtt_recovery_cap_removed src/helpers/mqtt/MqttRecovery.h "if (_fails >= FAILS_NEEDED && heap_low && _armed) {" "if (_fails >= FAILS_NEEDED && heap_low) {" $RU
mutate mqtt_recovery_success_does_not_reset_the_count src/helpers/mqtt/MqttRecovery.h "void onConnected() { _fails = 0; }" "void onConnected() { }" $RU
mutate mqtt_recovery_fires_one_failure_early src/helpers/mqtt/MqttRecovery.h "if (_fails >= FAILS_NEEDED && heap_low && _armed) {" "if (_fails + 1 >= FAILS_NEEDED && heap_low && _armed) {" $RU
mutate mqtt_recovery_hour_not_restarted_after_a_restart src/helpers/mqtt/MqttRecovery.h "      _since = now_ms;
      _armed = false;
      return true;" "      return true;" $RU
mutate mqtt_recovery_wrap_closes_the_cap src/helpers/mqtt/MqttRecovery.h "if (!_armed && (uint32_t)(now_ms - _since) >= MIN_INTERVAL_MS) _armed = true;" "_armed = (uint32_t)(now_ms - _since) >= MIN_INTERVAL_MS;" $RU
# the controller feeds the rule from the real connect path, and the restart keeps its audit entry (sim, end to end)
RS=sim/tests/test_mqtt_recovery.py
mutate mqtt_recovery_controller_ignores_the_heap src/helpers/mqtt/MqttController.cpp "if (!_rec.onFailed(now, _heap_low())) return;" "if (!_rec.onFailed(now, true)) return;" $RS::test_failed_connects_with_a_healthy_heap_never_restart_the_device
mutate mqtt_recovery_no_audit_entry src/helpers/mqtt/MqttController.cpp "  Posture::audit().add(_audit_now ? _audit_now() : 0, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_MQTT_RECOVER);
" "" $RS::test_failed_connects_under_a_low_heap_restart_the_device_and_the_audit_entry_survives
mutate mqtt_recovery_no_restart src/helpers/mqtt/MqttController.cpp "  _restart(_restart_user);
}" "  (void)_restart_user;
}" $RS::test_failed_connects_under_a_low_heap_restart_the_device_and_the_audit_entry_survives
mutate mqtt_recovery_failures_not_counted src/helpers/mqtt/MqttController.cpp "  _core.onConnectFailed(now, why);
  if (!_heap_low || !_restart) return;" "  _core.onConnectFailed(now, why);
  return;" $RS::test_failed_connects_under_a_low_heap_restart_the_device_and_the_audit_entry_survives
mutate mqtt_recovery_audit_not_flushed examples/companion_radio/main.cpp "                                     auditStore().flush(millis());   // the entry reaches flash before the restart
" "" $RS::test_failed_connects_under_a_low_heap_restart_the_device_and_the_audit_entry_survives
mutate mqtt_worker_not_reserved_at_boot examples/companion_radio/main.cpp "    if (mqtt_may_run) mqttPlatformNet().reserve();
" "" $RS::test_the_long_lived_platform_resources_are_taken_before_wifi_starts
mutate mqtt_worker_reserved_after_wifi examples/companion_radio/main.cpp "    if (mqtt_may_run) mqttPlatformNet().reserve();
#endif
    wifiController().begin(&the_mesh.getNodePrefs()->wifi_profiles);" "#endif
    wifiController().begin(&the_mesh.getNodePrefs()->wifi_profiles);
    if (mqtt_may_run) mqttPlatformNet().reserve();" $RS::test_the_long_lived_platform_resources_are_taken_before_wifi_starts
