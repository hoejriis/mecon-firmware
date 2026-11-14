# Room for a whole frame on an nRF52 USB cable (#550): src/helpers/SerialTxGate.h
mutate txgate_waits_forever src/helpers/SerialTxGate.h '    if ((uint32_t)(now_ms() - t0) >= wait_ms) return false;' '    if (false) return false;' unit:test_serial_tx_gate
mutate txgate_late_by_one src/helpers/SerialTxGate.h '    if ((uint32_t)(now_ms() - t0) >= wait_ms) return false;' '    if ((uint32_t)(now_ms() - t0) > wait_ms) return false;' unit:test_serial_tx_gate
mutate txgate_partial_room src/helpers/SerialTxGate.h '  while (port.availableForWrite() < (int)need) {' '  while (port.availableForWrite() < (int)need / 2) {' unit:test_serial_tx_gate
mutate txgate_gives_up_written src/helpers/SerialTxGate.h '    if ((uint32_t)(now_ms() - t0) >= wait_ms) return false;' '    if ((uint32_t)(now_ms() - t0) >= wait_ms) return true;' unit:test_serial_tx_gate
# Not here: the call in ArduinoSerialInterface::writeFrame is nRF52-only and no native build compiles it; it is proved on
# hardware (a host that holds the port open without reading, then mecon.slowpass and status.get cable_dropped).
