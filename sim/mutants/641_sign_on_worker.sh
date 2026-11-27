# A signature computed on the nRF52 worker's stack, not the 4 KB loop task's (Issue #641): src/helpers/iface/SignDispatch.h
mutate sign_always_inline src/helpers/iface/SignDispatch.h '  if (!run(&signJob<Key>, &c, wait_ms)) signOnLoop<Key>(c);' '  signOnLoop<Key>(c);' unit:test_sign_dispatch
mutate sign_no_fallback src/helpers/iface/SignDispatch.h '  if (!run(&signJob<Key>, &c, wait_ms)) signOnLoop<Key>(c);' '  run(&signJob<Key>, &c, wait_ms);' unit:test_sign_dispatch
mutate sign_twice src/helpers/iface/SignDispatch.h '  if (!run(&signJob<Key>, &c, wait_ms)) signOnLoop<Key>(c);' '  run(&signJob<Key>, &c, wait_ms); signOnLoop<Key>(c);' unit:test_sign_dispatch
mutate sign_wait_dropped src/helpers/iface/SignDispatch.h '  if (!run(&signJob<Key>, &c, wait_ms)) signOnLoop<Key>(c);' '  if (!run(&signJob<Key>, &c, 0)) signOnLoop<Key>(c);' unit:test_sign_dispatch
# Not here: Provenance::signMessage choosing the worker (nRF52 only; no native build compiles NrfWorker::run). The stack gate
# holds it: --require 'Iface::Provenance::sign(=NrfWorker::run', and the loop path's depth with the signing chain off it.
