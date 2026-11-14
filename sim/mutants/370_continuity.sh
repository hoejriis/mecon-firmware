# The Deployment continuity package (AUTHORITY section 10a; #118): src/helpers/iface/DeployAuth.cpp, DepContinuity.cpp
C=sim/tests/test_continuity.py
F=src/helpers/iface/DeployAuth.cpp
mutate cont_hardened_keeps_it $F '  if (hardened && _cont_key_present) continuityDestroy();' '  (void)hardened;' $C::test_hardened_destroys_the_package_and_normal_does_not_bring_it_back
mutate cont_generation_may_fall $F '  if (continuityRead(sealed, Dep::CONT_MAX, &old_n, &old_gen) && (uint32_t)gen64 <= old_gen) {' '  if (false) {' $C::test_only_a_signed_member_with_a_rising_generation_stores_one_and_only_the_cable_exports
mutate cont_export_to_a_signature $F '  const bool cable = !_signed && _ctx.transport == Cli::T_USB' '  const bool cable = _ctx.transport == Cli::T_USB' $C::test_only_a_signed_member_with_a_rising_generation_stores_one_and_only_the_cable_exports
mutate cont_reset_keeps_it $F '  if (ok) continuityDestroy();   // a package belongs to the Deployment it was made for' '  (void)0;' $C::test_a_deployment_reset_removes_the_package
mutate cont_export_unreachable $F '  if (strcmp(op.name, "deployment.continuity.export") == 0) return PRE_ALLOW;' '  ;' $C::test_a_member_stores_the_package_and_the_cable_exports_it_unchanged_across_a_reboot
mutate cont_stored_in_clear $F '  if (!len || !_env.cont_blob->save(sealed, len)) {' '  if (!len || !_env.cont_blob->save(pkg, (size_t)n)) {' $C::test_a_member_stores_the_package_and_the_cable_exports_it_unchanged_across_a_reboot
mutate cont_tag_ignored src/helpers/deployment/DepContinuity.cpp '  const bool ok = constTimeEqual(tag, in + CONT_HDR + m, CONT_TAG);' '  const bool ok = true;' unit:test_dep_continuity
mutate cont_header_unsigned src/helpers/deployment/DepContinuity.cpp '  hmacSha256(k_mac, 32, out, CONT_HDR + n, out + CONT_HDR + n);' '  hmacSha256(k_mac, 32, out + CONT_HDR, n, out + CONT_HDR + n);' unit:test_dep_continuity
