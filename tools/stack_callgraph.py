#!/usr/bin/env python3
"""Worst-case static stack depth from named roots, from GCC's call-graph files (#158).

    python3 tools/stack_callgraph.py <build dir> --root 'loop_task=3840' --root 'NrfWorker.*body=7936' \
        [--indirect 'CALLER_REGEX=TARGET_REGEX' ...] [--show N]

Build the env with `-fstack-usage -fcallgraph-info=su`: every object then has a .ci file with each function's frame
and its calls. A root is a regex over function names (the demangled label, or the symbol); its limit is in bytes. The
depth of a function is its own frame plus the deepest of its callees. An indirect call (a function pointer or a virtual
call) cannot be followed statically: --indirect names the functions a caller may reach that way (both regexes over
names), and every other indirect call counts as 0 and is listed, so a reader can see what the number leaves out.
Recursion is reported and counted once. --no-reentry 'OUTER=INNER' says a function matching INNER is never entered
beneath one matching OUTER (both regexes over names): a library that holds a lock for the whole call, such as
LittleFS, cannot be re-entered from its own driver callbacks, so a mapped indirect call that would do so is not a path.
Callees are walked in sorted order, so the result does not depend on Python's hash seed.
--require 'FROM=TO' fails (exit 1) unless a function matching FROM reaches one matching TO, under the same --indirect
and --no-reentry rules: for a hand-off that must stay in place however much room the task would have without it.

Exits 1 when a root is over its limit, 2 when a root matches nothing (a renamed function must not pass the gate by
vanishing), 0 otherwise. Prints each root's worst path.
"""
import argparse
import glob
import os
import re
import sys

NODE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')
STACK = re.compile(r'\\n(\d+) bytes \((static|dynamic|dynamic,bounded)\)')


def load(build):
    frames, names, edges, dynamic = {}, {}, {}, set()
    files = glob.glob(os.path.join(build, "**", "*.ci"), recursive=True)
    for path in files:
        for line in open(path, errors="replace"):
            m = NODE.search(line)
            if m:
                title, label = m.group(1), m.group(2)
                names.setdefault(title, label.split("\\n")[0])
                s = STACK.search(label)
                if s:
                    frames[title] = int(s.group(1))
                    if s.group(2) != "static":
                        dynamic.add(title)
                continue
            m = EDGE.search(line)
            if m:
                edges.setdefault(m.group(1), set()).add(m.group(2))
    # A call to a function defined in another unit names it by its symbol; a file-local one is "file:symbol".
    by_symbol = {}
    for t in frames:
        by_symbol.setdefault(t.split(":")[-1], []).append(t)
    return files, frames, names, edges, dynamic, by_symbol


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--root", action="append", default=[])
    ap.add_argument("--indirect", action="append", default=[])
    ap.add_argument("--no-reentry", action="append", default=[])
    ap.add_argument("--require", action="append", default=[])
    ap.add_argument("--show", type=int, default=12)
    a = ap.parse_args()
    files, frames, names, edges, dynamic, by_symbol = load(a.build)
    if not files:
        sys.exit("no .ci files under %s: build with -fstack-usage -fcallgraph-info=su" % a.build)
    indirect = []
    for spec in a.indirect:
        c, t = spec.split("=", 1)
        targets = [f for f in frames if re.search(t, names.get(f, f)) or re.search(t, f)]
        indirect.append((re.compile(c), sorted(targets)))
    no_reentry = [tuple(re.compile(x) for x in spec.split("=", 1)) for spec in a.no_reentry]

    def matches(rx, f):
        return rx.search(names.get(f, f)) or rx.search(f)

    def resolve(t):
        if t in frames:
            return [t]
        return by_symbol.get(t.split(":")[-1], [])

    memo, unfollowed, cycles = {}, {}, set()

    def depth(f, stack, inside=frozenset()):
        # inside: the --no-reentry rules whose OUTER is on the current path
        inside = inside | frozenset(i for i, (o, _) in enumerate(no_reentry) if matches(o, f))
        key = (f, inside)
        if key in memo:
            return memo[key]
        if f in stack:
            cycles.add(names.get(f, f))
            return (0, [])
        stack.add(f)
        best, path = 0, []

        def visit(g):
            nonlocal best, path
            if any(matches(no_reentry[i][1], g) for i in inside):
                return
            d, p = depth(g, stack, inside)
            if d > best:
                best, path = d, p

        for t in sorted(edges.get(f, ())):
            if t == "__indirect_call":
                hit = False
                for c, targets in indirect:
                    if matches(c, f):
                        hit = True
                        for g in targets:
                            visit(g)
                if not hit:
                    unfollowed[names.get(f, f)] = unfollowed.get(names.get(f, f), 0) + 1
                continue
            for g in sorted(resolve(t)):
                visit(g)
        stack.discard(f)
        r = (frames.get(f, 0) + best, [f] + path)
        memo[key] = r
        return r

    status = 0
    for spec in a.root:
        rx, limit = spec.rsplit("=", 1)
        limit = int(limit)
        roots = sorted(f for f in frames if re.search(rx, names.get(f, f)) or re.search(rx, f))
        if not roots:
            print("ROOT NOT FOUND: %s" % rx)
            status = max(status, 2)
            continue
        worst = max((depth(r, set()) for r in roots), key=lambda x: x[0])
        verdict = "OVER" if worst[0] > limit else "ok"
        print("%s  %s: worst %d B of %d" % (verdict, rx, worst[0], limit))
        for f in worst[1][: a.show]:
            print("    %6d  %s%s" % (frames.get(f, 0), names.get(f, f)[:110], "  (dynamic)" if f in dynamic else ""))
        if len(worst[1]) > a.show:
            print("    ... %d more" % (len(worst[1]) - a.show))
        if worst[0] > limit:
            status = max(status, 1)
    def callees(f, inside):
        out = []
        for t in sorted(edges.get(f, ())):
            gs = []
            if t == "__indirect_call":
                for c, targets in indirect:
                    if matches(c, f):
                        gs += targets
            else:
                gs = sorted(resolve(t))
            out += [g for g in gs if not any(matches(no_reentry[i][1], g) for i in inside)]
        return out

    for spec in a.require:
        rx, target = (re.compile(x) for x in spec.split("=", 1))
        roots = sorted(f for f in frames if matches(rx, f))
        if not roots:
            print("ROOT NOT FOUND: %s" % rx.pattern)
            status = max(status, 2)
            continue
        found, seen = None, set()
        todo = [(r, frozenset(), [r]) for r in roots]
        while todo and not found:
            f, inside, path = todo.pop()
            inside = inside | frozenset(i for i, (o, _) in enumerate(no_reentry) if matches(o, f))
            if (f, inside) in seen:
                continue
            seen.add((f, inside))
            for g in callees(f, inside):
                if matches(target, g):
                    found = path + [g]
                    break
                todo.append((g, inside, path + [g]))
        if found:
            print("ok  %s reaches %s" % (rx.pattern, target.pattern))
        else:
            print("MISSING  %s does not reach %s" % (rx.pattern, target.pattern))
            status = max(status, 1)
    if cycles:
        print("recursion, counted once: " + "; ".join(sorted(cycles))[:400])
    if unfollowed:
        top = sorted(unfollowed.items(), key=lambda x: -x[1])[:8]
        print("indirect calls not followed (%d callers): %s" % (len(unfollowed), "; ".join("%s x%d" % kv for kv in top))[:600])
    sys.exit(status)


if __name__ == "__main__":
    main()
