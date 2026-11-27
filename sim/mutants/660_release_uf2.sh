# The recovery UF2 of an nRF52 release (tools/mecon_release_set.py): the index and verify refuse a UF2 that is not its zip's application
T=tools/tests/test_mecon_release_set.py
F=tools/mecon_release_set.py
mutate uf2_bytes_not_compared $F '    if payload[:len(fw)] != fw or len(pad) >= 256 or set(pad) not in (set(), {0xFF}, {0x00}):' '    if False:' "$T"
mutate uf2_padding_unchecked $F '    if payload[:len(fw)] != fw or len(pad) >= 256 or set(pad) not in (set(), {0xFF}, {0x00}):' '    if payload[:len(fw)] != fw:' "$T"
mutate uf2_family_unchecked $F '    if family != UF2_FAMILY_NRF52840:' '    if False:' "$T"
mutate uf2_gap_allowed $F '        elif addr != base + len(payload):' '        elif False:' "$T"
mutate uf2_not_required_for_dfu $F '            if not os.path.isfile(uf2_path):' '            if False:' "$T"
mutate uf2_required_for_app $F '        if v["kind"] == "bootloader-dfu":
            uf2_path' '        if True:
            uf2_path' "$T::test_the_index_needs_a_uf2_for_every_bootloader_dfu_variant_and_none_for_an_app"
mutate verify_skips_uf2_compare $F '            if "0x%x" % uf2_checked(uf2, image, e["recovery_uf2"]) != e["recovery_uf2_base"]:' '            if False:' "$T"
mutate verify_skips_uf2_hash $F '            if hashlib.sha256(uf2).hexdigest() != e["recovery_uf2_sha256"]:' '            if False:' "$T"
mutate verify_allows_missing_uf2 $F '        if e["kind"] == "bootloader-dfu" and "recovery_uf2" not in e:' '        if False:' "$T"
