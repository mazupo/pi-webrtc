#!/usr/bin/env python3
"""Fails when an include in src/ points up the module layers or two headers include each other.

A module is a top-level folder of src/; the files directly in src/ form the "root" module.
"""
import os
import re
import sys

# Top to bottom. A module may include its own headers and those of lower layers only.
LAYERS = [
    ["root"],  # main, parser, config.h: the composition layer
    ["signaling"],
    ["rtc"],
    ["recorder", "track"],
    ["capturer", "ipc", "player"],
    ["codecs"],
    ["v4l2"],
    ["common"],
]

# (file, included file) pairs allowed to break the layers. Remove each once it is fixed.
EXCEPTIONS = {
    ("rtc/conductor.h", "config.h"),  # until Conductor moves to src/
}

SRC = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
SOURCE_EXTS = (".h", ".hpp", ".cpp", ".cc", ".cu")
HEADER_EXTS = (".h", ".hpp")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def module_of(path):
    return path.split("/")[0] if "/" in path else "root"


def resolve(include, file_dir):
    # Same lookup order as the compiler: the including file's folder, then src/.
    for base in (file_dir, SRC):
        candidate = os.path.normpath(os.path.join(base, include))
        if os.path.isfile(candidate) and candidate.startswith(SRC + os.sep):
            return os.path.relpath(candidate, SRC).replace(os.sep, "/")
    return None  # outside src/, e.g. generated protobuf headers


def scan():
    """Returns {file: [(line, included file)]} for the quoted includes that resolve inside src/."""
    includes = {}
    for dirpath, dirnames, filenames in os.walk(SRC):
        dirnames.sort()
        for name in sorted(filenames):
            if not name.endswith(SOURCE_EXTS):
                continue
            path = os.path.join(dirpath, name)
            found = []
            with open(path, encoding="utf-8", errors="replace") as f:
                for line_no, line in enumerate(f, 1):
                    match = INCLUDE_RE.match(line)
                    target = match and resolve(match.group(1), dirpath)
                    if target:
                        found.append((line_no, target))
            includes[os.path.relpath(path, SRC).replace(os.sep, "/")] = found
    return includes


def header_cycles(includes):
    """Returns the groups of headers that include each other (Tarjan's SCC)."""
    graph = {
        f: [t for _, t in incs if t.endswith(HEADER_EXTS)]
        for f, incs in includes.items()
        if f.endswith(HEADER_EXTS)
    }
    index, low, on_stack, stack, cycles = {}, {}, set(), [], []

    def visit(node):
        index[node] = low[node] = len(index)
        stack.append(node)
        on_stack.add(node)
        for nxt in graph.get(node, []):
            if nxt not in index:
                visit(nxt)
                low[node] = min(low[node], low[nxt])
            elif nxt in on_stack:
                low[node] = min(low[node], index[nxt])
        if low[node] == index[node]:
            group = []
            while True:
                member = stack.pop()
                on_stack.discard(member)
                group.append(member)
                if member == node:
                    break
            if len(group) > 1 or node in graph.get(node, []):
                cycles.append(sorted(group))

    sys.setrecursionlimit(10000)
    for node in sorted(graph):
        if node not in index:
            visit(node)
    return cycles


def main():
    rank = {module: i for i, layer in enumerate(LAYERS) for module in layer}
    includes = scan()
    errors = []
    unknown = {}
    used_exceptions = set()

    for path, incs in sorted(includes.items()):
        module = module_of(path)
        if module not in rank:
            unknown.setdefault(module, path)
            continue
        for line_no, target in incs:
            target_module = module_of(target)
            if target_module == module or rank.get(target_module, -1) > rank[module]:
                continue
            if (path, target) in EXCEPTIONS:
                used_exceptions.add((path, target))
                continue
            errors.append((path, line_no, f"{module} must not include {target}: "
                           f"{target_module} is not in a lower layer"))

    for module, path in sorted(unknown.items()):
        errors.append((path, 1, f"module '{module}' is missing from LAYERS"))
    for path, target in sorted(EXCEPTIONS - used_exceptions):
        errors.append((path, 1, f"exception ({path}, {target}) is no longer needed; remove it"))
    for group in header_cycles(includes):
        errors.append((group[0], 1, "headers include each other: " + ", ".join(group)))

    github = os.environ.get("GITHUB_ACTIONS") == "true"
    for path, line_no, message in errors:
        if github:
            print(f"::error file=src/{path},line={line_no}::{message}")
        else:
            print(f"src/{path}:{line_no}: {message}")
    if errors:
        print(f"{len(errors)} module dependency error(s)")
        return 1
    print(f"Module dependencies OK ({len(includes)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
