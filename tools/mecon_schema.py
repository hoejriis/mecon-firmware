"""A small JSON Schema checker for the device interface schemas (docs/contract/schema/), covering only the keywords
they use: type (including null and lists of types), required, properties, additionalProperties (false only), enum,
const, pattern, minimum, items, contains and oneOf. Any standard JSON Schema 2020-12 validator accepts the same files; this one keeps the tests free of a
dependency. Returns a list of problems, empty when the value conforms."""
import json
import os
import re

SCHEMA_DIR = os.path.join(os.path.dirname(__file__), "..", "docs", "contract", "schema")
TYPES = {"object": dict, "array": list, "string": str, "boolean": bool, "null": type(None)}


def load(name):
    with open(os.path.join(SCHEMA_DIR, name)) as f:
        return json.load(f)


def _is(v, t):
    if t == "integer":
        return isinstance(v, int) and not isinstance(v, bool)
    if t == "number":
        return isinstance(v, (int, float)) and not isinstance(v, bool)
    return isinstance(v, TYPES[t])


def check(value, schema, path="$"):
    errs = []
    t = schema.get("type")
    if t is not None:
        ts = t if isinstance(t, list) else [t]
        if not any(_is(value, x) for x in ts):
            return ["%s: %r is not %s" % (path, value, "/".join(ts))]
    if "const" in schema and value != schema["const"]:
        errs.append("%s: %r is not %r" % (path, value, schema["const"]))
    if "enum" in schema and value not in schema["enum"]:
        errs.append("%s: %r not in %r" % (path, value, schema["enum"]))
    if isinstance(value, str) and "pattern" in schema and not re.search(schema["pattern"], value):
        errs.append("%s: %r does not match %s" % (path, value, schema["pattern"]))
    if _is(value, "number") and "minimum" in schema and value < schema["minimum"]:
        errs.append("%s: %r < %r" % (path, value, schema["minimum"]))
    if isinstance(value, dict):
        for k in schema.get("required", []):
            if k not in value:
                errs.append("%s: missing %s" % (path, k))
        for k, sub in schema.get("properties", {}).items():
            if k in value:
                errs += check(value[k], sub, "%s.%s" % (path, k))
        if schema.get("additionalProperties") is False:
            for k in value:
                if k not in schema.get("properties", {}):
                    errs.append("%s: unexpected %s" % (path, k))
    if isinstance(value, list):
        if "items" in schema:
            for i, v in enumerate(value):
                errs += check(v, schema["items"], "%s[%d]" % (path, i))
        if "contains" in schema and not any(not check(v, schema["contains"]) for v in value):
            errs.append("%s: no item matches %r" % (path, schema["contains"]))
    if "oneOf" in schema:
        matching = sum(1 for sub in schema["oneOf"] if not check(value, sub, path))
        if matching != 1:
            errs.append("%s: matches %d of the oneOf branches, not exactly 1" % (path, matching))
    return errs
