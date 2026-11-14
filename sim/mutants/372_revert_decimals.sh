# A revert puts back a decimal as the CLI printed it (#553): src/helpers/iface/IfaceRadioKeys.inc
T=sim/tests/test_jobs.py::test_a_revert_puts_back_a_decimal_as_the_device_printed_it
mutate revert_decimal_short src/helpers/iface/IfaceRadioKeys.inc '  {"direct.txdelay", K_STRING, 1, 12,' '  {"direct.txdelay", K_STRING, 1, 8,' $T
