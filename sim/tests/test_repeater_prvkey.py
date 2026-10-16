"""`set prv.key` replaces the node's identity. Like `get prv.key` and `erase` it is a local-console
command: a remote admin (logged in over the air with the admin password) must be refused."""
from sim.harness import CliClient, CompanionClient

RTC = 1760000000


def _setup(ether, nodes):
    rep = nodes("repeater", "rp", seed=41, rtc=RTC, ether_path=ether.path).start()
    adm = nodes("companion", "ad", seed=42, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["rp", "ad"])
    ether.set_link("rp", "ad")
    cli = CliClient(rep)
    pub_hex = cli.command("get public.key").lstrip("> ").strip()
    assert len(pub_hex) == 64, pub_hex
    c = CompanionClient(adm)
    c.device_query()
    c.app_start()
    assert c.add_contact(bytes.fromhex(pub_hex), "rep", ctype=2)[0] == 0
    assert c.login(bytes.fromhex(pub_hex), "password"), "admin login failed"
    return rep, cli, c, bytes.fromhex(pub_hex)


def test_remote_admin_cannot_replace_identity(ether, nodes):
    rep, cli, c, pub = _setup(ether, nodes)
    # positive: the remote admin session works for an ordinary command
    assert "Sim Repeater" in c.remote_admin(pub, "get name")
    new_key = c.export_private_key().hex()          # a valid private key (the admin node's own)
    reply = c.remote_admin(pub, "set prv.key " + new_key)
    assert "reboot to apply" not in reply and "New pubkey" not in reply, reply
    assert "local" in reply.lower(), reply
    rep.restart()
    cli = CliClient(rep)
    assert cli.command("get public.key").lstrip("> ").strip().lower() == pub.hex()      # identity unchanged


def test_local_console_can_still_replace_identity(ether, nodes):
    rep, cli, c, pub = _setup(ether, nodes)
    new_key = c.export_private_key()
    reply = cli.command("set prv.key " + new_key.hex())
    assert reply.startswith("OK, reboot to apply"), reply
    rep.restart()
    cli = CliClient(rep)
    assert cli.command("get public.key").lstrip("> ").strip().lower() == c.pub_key.hex()
