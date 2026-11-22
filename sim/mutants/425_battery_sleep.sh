# The battery guard's protective sleep: src/helpers/BatterySleep.h, BusyGate.h, BatterySleepRun.h, the boot guard's place in
# both roles' setup(), the update preflight's battery-low and the bench offset
TOPIC=425_battery_sleep
U=unit:test_battery_sleep
BS=sim/tests/test_battery_sleep.py
# the guard off and still sleeping at a critical level
mutate sleep_with_the_guard_off src/helpers/BatterySleep.h 'if (!c.enabled) { _eff = OFF; _crit = 0; return false; }' 'if (!c.enabled) { _eff = OFF; }' $U
mutate sleep_with_the_guard_off_sim src/helpers/BatterySleep.h 'if (!c.enabled) { _eff = OFF; _crit = 0; return false; }' 'if (!c.enabled) { _eff = OFF; }' $BS::test_with_the_guard_off_a_flat_battery_never_sleeps
# sleeping while a job is open: the busy gate's job input dropped (the gate, and the role's reading of the job table)
mutate sleep_through_an_open_job src/helpers/BusyGate.h '  if (in.job_open) return "job-open";' '' $U
mutate sleep_through_an_open_job_sim src/helpers/BatterySleepRun.h '  in.job_open = t && t->openJob();' '  in.job_open = false;
  (void)t;' $BS::test_an_open_job_holds_the_sleep_until_it_is_confirmed
# a save in progress no longer holding the emergency sleep
mutate emergency_through_a_save src/helpers/BusyGate.h '  if (in.flash_write) return "flash-write";
  if (emergency) return nullptr;' '  if (emergency) return nullptr;
  if (in.flash_write) return "flash-write";' $U
# sleeping at an unknown level, in the loop and on a wake that cannot measure
mutate sleep_at_unknown_level src/helpers/BatterySleep.h 'if (lv == BatteryLevel::UNKNOWN) { _why = "level-unknown"; return false; }' 'if (lv == BatteryLevel::UNKNOWN) { _emergency = false; return true; }' $U
mutate sleep_at_unknown_wake src/helpers/BatterySleep.h '      if (!m) {            // cannot measure' '      if (false) {         // cannot measure' $U
# resuming on one burst, a rebound taken for a recovery
mutate resume_on_one_burst src/helpers/BatterySleep.h 'static const uint8_t BURSTS = 3; ' 'static const uint8_t BURSTS = 1; ' $U
# a confirmed critical level not waited for: one window is enough
mutate no_confirmation_windows src/helpers/BatterySleep.h 'static const uint8_t CONFIRM_WINDOWS = 3; ' 'static const uint8_t CONFIRM_WINDOWS = 1; ' $U
# a timer that cannot be armed taken for a sleep: on a wake, and in the loop
mutate timer_failure_still_sleeps src/helpers/BatterySleep.h '  if (io.sleep(io.user, s.interval_s)) return true;' '  io.sleep(io.user, s.interval_s);
  return true;' $U
mutate timer_failure_ignored_in_the_loop src/helpers/BatterySleepRun.h '    if (!deepSleep(secs, true)) controller().sleepFailed(s);' '    (void)deepSleep(secs, true);' $BS::test_a_timer_that_cannot_be_armed_keeps_the_device_up
# a flash write on every wake: the boot writing a file before the guard decides
mutate flash_write_per_wake examples/companion_radio/main.cpp '  BatterySleep::bootGuard();
#endif

#ifdef HAS_EXTERNAL_WATCHDOG' '  spiffsBeginOnBoot();
  { File f = SPIFFS.open("/guard_wake", "w"); if (f) { f.print("1"); f.close(); } }
  BatterySleep::bootGuard();
#endif

#ifdef HAS_EXTERNAL_WATCHDOG' "$BS::test_critical_sleeps_a_wake_below_recovery_sleeps_again_touching_no_file_and_three_bursts_resume[companion_standalone]"
# the Repeater's boot guard after the file system and the radio, where a wake would start everything first
mutate boot_guard_after_the_file_system examples/simple_repeater/main.cpp '    board.begin();
    BatterySleep::bootGuard();
    delay(1000);' '    board.begin();
    SPIFFS.begin(true);
    (void)SPIFFS.exists("/prefs.json");   // the prefs looked up, as a boot past the file system mount does
    BatterySleep::bootGuard();
    delay(1000);' "$BS::test_critical_sleeps_a_wake_below_recovery_sleeps_again_touching_no_file_and_three_bursts_resume[repeater]"
# a relapse in probation not counted: no back-off
mutate relapse_not_counted src/helpers/BatterySleep.h '  if (s.phase == PH_PROBATION) {
    if (s.failed < FAIL_CAP) s.failed++;' '  if (false) {
    if (s.failed < FAIL_CAP) s.failed++;' $U
mutate relapse_not_counted_sim src/helpers/BatterySleep.h '  if (s.phase == PH_PROBATION) {
    if (s.failed < FAIL_CAP) s.failed++;' '  if (false) {
    if (s.failed < FAIL_CAP) s.failed++;' "$BS::test_a_relapse_in_probation_doubles_the_interval_and_raises_the_bar[repeater]"
# a damaged block trusted
mutate damaged_block_trusted src/helpers/BatterySleep.h 'inline bool valid(const State& s) { return s.magic == MAGIC && s.version == VERSION && s.crc == crcOf(s); }' 'inline bool valid(const State& s) { return s.magic == MAGIC && s.version == VERSION; }' $U
# an update offered at a low battery with the guard on
mutate update_preflight_battery_ignored src/helpers/updates/UpdService.cpp '  if (batteryLevel().settings().enabled && batteryLevel().level() != BatteryLevel::OK) {' '  if (false) {' $BS::test_an_update_is_refused_while_the_battery_is_not_ok
# the bench flag in a release environment
# Not run: dropping the Companion handler's cable check (MyMesh.cpp, `Cli::principalOf(_cli_ctx) == Cli::P_CABLE`) is
# equivalent for every caller a test can reach. The CLI gate refuses the line first for a paired link and every remote
# caller (the registry entry is destructive and cable-only: `ERR: not permitted`, checked by
# test_the_bench_offset_is_refused_over_a_paired_link), and no operation runs an arbitrary CLI line for a signed
# request, the one caller the gate's cable-only flag lets through. The handler check stays as the second guard for that.
mutate bench_offset_in_a_release_build variants/heltec_v3/platformio.ini '  -D BATTERY_SLEEP_BOARD   ; and its protective sleep: a timed deep sleep (src/helpers/BatterySleep.h)' '  -D BATTERY_SLEEP_BOARD   ; and its protective sleep: a timed deep sleep (src/helpers/BatterySleep.h)
  -D MECON_BATTERY_TEST_OFFSET' $BS::test_no_release_build_defines_the_bench_offset
