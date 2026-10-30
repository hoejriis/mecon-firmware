# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the nRF52 provisioned-marker page (DepMarkerPage.h): fail closed, and the log's bounds
mutate marker_page_garbage_reads_absent src/helpers/deployment/DepMarkerPage.h '    if (ended || !markerSlotValid(s, &st)) return {MK_PRESENT, p.size(), false};   // data after the end, or not ours' '    if (ended || !markerSlotValid(s, &st)) continue;' unit:test_dep_marker_page
mutate marker_page_write_not_read_back src/helpers/deployment/DepMarkerPage.h '  return markerPageRead(p) == (present ? MK_PRESENT : MK_ABSENT);' '  return true;' unit:test_dep_marker_page
mutate marker_page_never_erased src/helpers/deployment/DepMarkerPage.h '  if (!sc.clean || sc.next + MARKER_SLOT > p.size()) {' '  if (false) {' unit:test_dep_marker_page
