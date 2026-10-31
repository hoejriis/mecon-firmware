"""UPDATES.md section 6: an update.manifest or update.offer is at most 1,536 bytes over MQTT. The device's client buffer is
1,600 bytes for the whole PUBLISH: at most 5 bytes of fixed header, 2 of topic length and the 42 of
mecon/<device_id>/req, which leaves 1,551 for the request. The case that set the bound: a Backend staging the longest
manifest (the `longest` vector, 637 bytes) as a signed request, which an unverified broker needs. The wrapper carries
the body as an escaped string around the manifest's own escaped text, so it comes to about 1,480 bytes."""
import json
import os

from test_deployment_authority import provision_args, wrap
from test_device_interface import broker, mqtt_boot  # noqa: F401  (broker is a fixture)

VECTORS = os.path.join(os.path.dirname(__file__), "..", "..", "test", "fixtures", "updates_public_v1.json")


def test_a_signed_stage_of_the_longest_manifest_crosses_the_broker(nodes, tmp_path, broker):  # noqa: F811
    b = broker()
    n, c, i, dev = mqtt_boot(nodes, tmp_path, "mb", b, anchor=False)   # TLS without an anchor: not verified
    node_key = i.request("hello")["result"]["node_key"]
    assert i.request("deployment.provision", provision_args(node_key))["status"] == "ok"
    longest = json.load(open(VECTORS))["longest"]
    # update.manifest is not a plain read (AUTHORITY.md 7.1), so a signed stage carries a job id.
    env = wrap(node_key, "update.manifest", {"manifest": longest["manifest"], "manifest_sig": longest["manifest_sig"]},
               job=1, rid="mb-1")
    size = len(json.dumps(env))
    assert 1450 <= size <= 1536, size
    unsigned = b.iface_request(dev, {"v": 1, "id": "mb-0", "op": "update.manifest",
                                     "args": {"manifest": longest["manifest"], "manifest_sig": longest["manifest_sig"]}})
    assert (unsigned["status"], unsigned.get("error")) == ("refused", "broker-not-verified"), unsigned
    r = b.iface_request(dev, env)
    assert r is not None, "a %d-byte signed request is answered over MQTT" % size
    assert (r["status"], r.get("error")) == ("refused", "wrong-target"), r   # read whole, verified, then refused

