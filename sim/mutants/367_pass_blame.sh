# Which section of the longest loop pass took its time (#519; src/helpers/PassBlame.h)
F=src/helpers/PassBlame.h
U=unit:test_pass_blame
mutate pb_shorter_pass_replaces $F '    if (_slowest.seen && pass <= _slowest.pass_ms) return;' '    if (false) return;' "$U"
mutate pb_last_section_wins $F '    if (!_by || ms > _longest) { _longest = ms; _by = what; }' '    if (true) { _longest = ms; _by = what; }' "$U"
mutate pb_unscoped_misnamed $F '    _slowest.by = _by ? _by : "unscoped";' '    _slowest.by = _by ? _by : "mesh";' "$U"
