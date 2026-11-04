# The stack gate's recursion check (tools/stack_callgraph.py, --recursion).
mutate stack_recursion_ignored tools/stack_callgraph.py 'if stray:' 'if False:' sim/tests/test_stack_callgraph.py
mutate stack_self_call_not_a_recursion tools/stack_callgraph.py 'if len(scc) > 1 or v in graph[v]:' 'if len(scc) > 1:' sim/tests/test_stack_callgraph.py
mutate stack_recursion_any_member_allows tools/stack_callgraph.py 'stray = [f for f in funcs if not matches(allowed, f)]' 'stray = [] if any(matches(allowed, f) for f in funcs) else funcs' sim/tests/test_stack_callgraph.py
mutate stack_recursion_root_vanishes tools/stack_callgraph.py 'graph = reachable(roots, callees, no_reentry, matches)' 'graph = reachable(roots[:0], callees, no_reentry, matches)' sim/tests/test_stack_callgraph.py
