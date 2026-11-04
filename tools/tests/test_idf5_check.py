"""bin/idf5_check.py's test for the rebuilt IDF libraries, against the state pioarduino leaves on disk (#254)."""
import hashlib
import importlib.util
import os

spec = importlib.util.spec_from_file_location(
    "idf5_check", os.path.join(os.path.dirname(__file__), "..", "..", "bin", "idf5_check.py"))
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

OPTS = "\nCONFIG_ESP_IPC_TASK_STACK_SIZE=2048\n"


def tree(tmp_path, libs=True, defaults_opts=OPTS, mcu="esp32s3"):
    """libs: the rebuilt package's sdkconfig exists. defaults_opts: what sdkconfig.defaults was hashed from, or None."""
    d = tmp_path / "libs"
    d.mkdir()
    if libs:
        (d / "sdkconfig").write_text("x\n")
    p = tmp_path / "project"
    p.mkdir()
    if defaults_opts is not None:
        digest = hashlib.md5((defaults_opts.strip() + mcu).encode("utf-8")).hexdigest()[:16]
        (p / "sdkconfig.defaults").write_text("# TASMOTA__%s\nCONFIG_X=y\n" % digest)
    return str(d), str(p)


def test_libraries_built_for_this_env_are_ready(tmp_path):
    assert check.libs_ready(*tree(tmp_path), OPTS, "esp32s3")


def test_a_fresh_core_directory_is_not_ready(tmp_path):
    assert not check.libs_ready(*tree(tmp_path, libs=False), OPTS, "esp32s3")


def test_a_package_not_installed_is_not_ready(tmp_path):
    assert not check.libs_ready(None, tree(tmp_path)[1], OPTS, "esp32s3")


def test_a_fresh_clone_on_a_cached_core_is_not_ready(tmp_path):
    # pioarduino reinstalls here too: no sdkconfig.defaults in the project
    assert not check.libs_ready(*tree(tmp_path, defaults_opts=None), OPTS, "esp32s3")


def test_another_boards_library_set_is_not_ready(tmp_path):
    # the V3 set installed, building a V4 env
    assert not check.libs_ready(*tree(tmp_path, defaults_opts="CONFIG_OTHER=y"), OPTS, "esp32s3")


def test_another_chip_is_not_ready(tmp_path):
    assert not check.libs_ready(*tree(tmp_path), OPTS, "esp32c6")


def test_a_defaults_file_without_the_hash_is_not_ready(tmp_path):
    d, p = tree(tmp_path)
    open(os.path.join(p, "sdkconfig.defaults"), "w").write("CONFIG_X=y\n")
    assert not check.libs_ready(d, p, OPTS, "esp32s3")
