"""CMD_ADD_UPDATE_CONTACT refuses type 0 (ADV_TYPE_NONE), the anonymous slot's type: a contact added with it would be
unlisted and overwritten by the next unknown sender."""
import os

from sim.harness import CompanionClient

ERR_CODE_ILLEGAL_ARG = 6


def _key(i):
    return bytes([0x40 + i]) + os.urandom(31)


def test_a_new_contact_of_type_0_is_refused_and_not_stored(nodes):
    n = nodes("companion", "t0new", seed=3).start()
    c = CompanionClient(n)
    c.app_start()
    k = _key(1)
    r = c.add_contact(k, "Zero", ctype=0)
    assert r[0] == 1 and r[1] == ERR_CODE_ILLEGAL_ARG, r.hex()       # RESP_CODE_ERR, illegal argument
    _, listed = c.get_contacts()
    assert all(x["key"] != k for x in listed)


def test_an_update_to_type_0_is_refused_and_the_contact_keeps_its_type(nodes):
    n = nodes("companion", "t0upd", seed=3).start()
    c = CompanionClient(n)
    c.app_start()
    k = _key(2)
    assert c.add_contact(k, "Chat", ctype=1)[0] == 0                # RESP_CODE_OK
    r = c.add_contact(k, "Chat", ctype=0)
    assert r[0] == 1 and r[1] == ERR_CODE_ILLEGAL_ARG, r.hex()
    _, listed = c.get_contacts()
    mine = [x for x in listed if x["key"] == k]
    assert len(mine) == 1 and mine[0]["type"] == 1, mine
