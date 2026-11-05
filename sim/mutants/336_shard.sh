# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# The suite's shards: splitting it must not quietly drop a test file
mutate shard_drops_a_file sim/shard.py '    for f in sorted(files, key=lambda f: (-SLOW.get(f, DEFAULT), f)):' '    for f in sorted(files, key=lambda f: (-SLOW.get(f, DEFAULT), f))[1:]:' sim/tests/test_shard.py::test_every_file_is_in_exactly_one_shard
mutate shard_one_takes_all sim/shard.py '        i = load.index(min(load))' '        i = 0' sim/tests/test_shard.py::test_no_shard_carries_most_of_the_load
