# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the authority layer's Ed25519 verify (DepVerify.h, #158): the Crypto library path the sim and the ESP32 builds run
mutate dep_verify_accepts_any_signature src/helpers/deployment/DepVerify.h '  return Ed25519::verify(sig, key, msg, len);' '  return true;' sim/tests/test_deployment_authority.py::test_bad_signatures_wrong_targets_and_unknown_issuers_are_refused
mutate dep_verify_malleable_accepted src/helpers/deployment/DepVerify.h '  if (!canonicalS(sig + 32) || !canonicalPoint(sig)) return false;' '' unit:test_dep_verify
