"""A small JSON Schema checker for the device interface schemas (docs/contract/schema/), covering only the keywords
they use: type (including null and lists of types), required, properties, additionalProperties (false, or a schema for
the other keys), minProperties, enum, const, pattern, minLength, maxLength, minimum, maximum, items, maxItems, contains
and oneOf. Any standard JSON Schema 2020-12 validator accepts the same files; this one keeps the tests free of a
dependency. Returns a list of problems, empty when the value conforms. A keyword the schemas use and this checker does
not know fails the check (see KNOWN), so a bound can never sit in a schema untested."""
import json
import os
import re

SCHEMA_DIR = os.path.join(os.path.dirname(__file__), "..", "docs", "contract", "schema")
TYPES = {"object": dict, "array": list, "string": str, "boolean": bool, "null": type(None)}
KNOWN = {"type", "required", "properties", "additionalProperties", "minProperties", "enum", "const", "pattern", "minLength",
         "maxLength", "minimum", "maximum", "items", "maxItems", "contains", "oneOf",
         "$schema", "$id", "title", "description"}   # the last four annotate and check nothing


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
    errs = ["%s: the checker does not know %s" % (path, k) for k in schema if k not in KNOWN]
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
    if isinstance(value, str) and "minLength" in schema and len(value) < schema["minLength"]:
        errs.append("%s: %r is shorter than %d" % (path, value, schema["minLength"]))
    if isinstance(value, str) and "maxLength" in schema and len(value) > schema["maxLength"]:
        errs.append("%s: %r is longer than %d" % (path, value, schema["maxLength"]))
    if _is(value, "number") and "minimum" in schema and value < schema["minimum"]:
        errs.append("%s: %r < %r" % (path, value, schema["minimum"]))
    if _is(value, "number") and "maximum" in schema and value > schema["maximum"]:
        errs.append("%s: %r > %r" % (path, value, schema["maximum"]))
    if isinstance(value, dict):
        for k in schema.get("required", []):
            if k not in value:
                errs.append("%s: missing %s" % (path, k))
        for k, sub in schema.get("properties", {}).items():
            if k in value:
                errs += check(value[k], sub, "%s.%s" % (path, k))
        extra = schema.get("additionalProperties")
        for k in value:
            if k in schema.get("properties", {}):
                continue
            if extra is False:
                errs.append("%s: unexpected %s" % (path, k))
            elif isinstance(extra, dict):
                errs += check(value[k], extra, "%s.%s" % (path, k))
        if "minProperties" in schema and len(value) < schema["minProperties"]:
            errs.append("%s: fewer than %d properties" % (path, schema["minProperties"]))
    if isinstance(value, list):
        if "items" in schema:
            for i, v in enumerate(value):
                errs += check(v, schema["items"], "%s[%d]" % (path, i))
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            errs.append("%s: %d items, more than %d" % (path, len(value), schema["maxItems"]))
        if "contains" in schema and not any(not check(v, schema["contains"]) for v in value):
            errs.append("%s: no item matches %r" % (path, schema["contains"]))
    if "oneOf" in schema:
        matching = sum(1 for sub in schema["oneOf"] if not check(value, sub, path))
        if matching != 1:
            errs.append("%s: matches %d of the oneOf branches, not exactly 1" % (path, matching))
    return errs
