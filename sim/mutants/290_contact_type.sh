# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- issue #165: CMD_ADD_UPDATE_CONTACT refuses type 0, the anonymous slot's type
mutate contact_type_0_accepted examples/companion_radio/MyMesh.cpp '  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1 && cmd_frame[1 + PUB_KEY_SIZE] == ADV_TYPE_NONE) {' '  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1 && false) {' sim/tests/test_contact_type.py
