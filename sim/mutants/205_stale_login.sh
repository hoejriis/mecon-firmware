# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- issue #112: a stale login (right password, older timestamp) is audited as stale-login
mutate stale_login_not_named_repeater examples/simple_repeater/MyMesh.cpp '                           Posture::W_STALE_LOGIN);' '                           Posture::W_BAD_PASSWORD);' sim/tests/test_stale_login.py::test_a_stale_login_is_refused_and_audited
mutate stale_login_not_named_room examples/simple_room_server/MyMesh.cpp '                             Posture::W_STALE_LOGIN);' '                             Posture::W_BAD_PASSWORD);' sim/tests/test_stale_login.py::test_a_stale_login_is_refused_and_audited
