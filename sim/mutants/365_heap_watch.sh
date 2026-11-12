# What the heap was doing at its lowest (#513; src/helpers/HeapWatch.h)
F=src/helpers/HeapWatch.h
U=unit:test_heap_watch
T=sim/tests/test_heap_watch.py
mutate hw_largest_never_lower $F '    if (m.largest && (!_largest.seen || m.largest < _largest.bytes)) set(_largest, m.largest, uptime_s);' '    if (m.largest && !_largest.seen) set(_largest, m.largest, uptime_s);' "$U"
mutate hw_largest_never_lower_sim $F '    if (m.largest && (!_largest.seen || m.largest < _largest.bytes)) set(_largest, m.largest, uptime_s);' '    if (m.largest && !_largest.seen) set(_largest, m.largest, uptime_s);' "$T"
mutate hw_min_free_untracked_counted $F '    if (m.min_free && (!_min_free.seen || m.min_free < _min_free.bytes)) set(_min_free, m.min_free, uptime_s);' '    if (!_min_free.seen || m.min_free < _min_free.bytes) set(_min_free, m.min_free, uptime_s);' "$U"
mutate hw_min_free_untracked_counted_sim $F '    if (m.min_free && (!_min_free.seen || m.min_free < _min_free.bytes)) set(_min_free, m.min_free, uptime_s);' '    if (!_min_free.seen || m.min_free < _min_free.bytes) set(_min_free, m.min_free, uptime_s);' "$T"
mutate hw_background_ignored $F '  const char* current() const { return _tag ? _tag : _bg ? _bg : "idle"; }' '  const char* current() const { return _tag ? _tag : "idle"; }' "$U"
mutate hw_unknown_counted $F '    if (!m.known) return;' '' "$U"
mutate hw_runtime_lows_dropped src/helpers/iface/Streams.cpp '      w.key("bytes"); w.unum(lows[i]->bytes);' '      w.key("bytes"); w.unum(0);' "$T"
mutate hw_tick_never_looks $F '    heapWatch().observe(memStats(), now / 1000);' '' "$T"
