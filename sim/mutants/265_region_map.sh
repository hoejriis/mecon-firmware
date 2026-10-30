# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- issue #165: the region table is listed without recursion, to a bounded depth, and id 0 or a self-parent is not a child
RM_SIM=sim/tests/test_region_map.py::test_a_damaged_table_with_a_self_parent_and_a_cycle_is_listed_without_overflowing
mutate region_print_depth_unbounded src/helpers/RegionMap.cpp '    if (depth + 1 > MAX_REGION_ENTRIES) continue;   // deeper than a valid tree: a cycle' '' $RM_SIM
mutate region_print_takes_a_self_parent src/helpers/RegionMap.cpp '!(regions[i].parent == lv.node->id && regions[i].id != 0 && regions[i].id != lv.node->id)' '!(regions[i].parent == lv.node->id)' $RM_SIM
mutate region_print_unbounded_in_time src/helpers/RegionMap.cpp '  while (depth >= 0 && listed < num_regions) {' '  while (depth >= 0) {' sim/tests/test_region_map.py::test_duplicated_ids_that_branch_at_every_level_are_listed_at_once
