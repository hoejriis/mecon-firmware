"""PlatformIO extra script: stop a Heltec V3 or V4 build at once when its rebuilt IDF libraries are not installed (#254).

These envs build on Arduino IDF libraries rebuilt for their custom_sdkconfig (docs/PLATFORM.md). pioarduino rebuilds them
inside whichever env finds them missing, and in a real env that pass cannot work: the ../examples source entries escape
it, and it dies far from the cause with `helpers/iface/IfaceDispatch.h: No such file or directory`. bin/idf5_libs.sh
builds them in a stub env instead. The test here is the one pioarduino makes (builder/frameworks/arduino.py,
matching_custom_sdkconfig): the libraries carry an sdkconfig, and the project's sdkconfig.defaults starts with the hash
of this env's custom_sdkconfig and chip.
"""
import hashlib
import os


def libs_ready(libs_dir, project_dir, custom_sdkconfig, mcu):
    if not libs_dir or not os.path.exists(os.path.join(libs_dir, "sdkconfig")):
        return False
    defaults = os.path.join(project_dir, "sdkconfig.defaults")
    if not os.path.exists(defaults):
        return False
    with open(defaults) as f:
        line = f.readline()
    if not line.startswith("# TASMOTA__"):
        return False
    want = hashlib.md5((custom_sdkconfig.strip() + mcu).encode("utf-8")).hexdigest()[:16]
    return line.split("__")[1].strip() == want


if "Import" in globals():
    Import("env")   # noqa: F821
    name = env["PIOENV"]
    config = env.GetProjectConfig()
    # The stub envs are what builds the libraries; clean and IDE dumps build nothing.
    if (not name.startswith("idf5_hybrid_libs") and config.has_option("env:" + name, "custom_sdkconfig")
            and not env.IsCleanTarget() and not env.IsIntegrationDump()):
        if not libs_ready(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"),
                          env.subst("$PROJECT_DIR"), env.GetProjectOption("custom_sdkconfig"),
                          env.BoardConfig().get("build.mcu", "esp32")):
            print("\n*** This env's rebuilt IDF libraries are not installed. Run this first, then build again:\n"
                  "***   bin/idf5_libs.sh %s\n"
                  "*** It is needed before the first build, and after building another board in the same PlatformIO\n"
                  "*** directory (docs/PLATFORM.md, \"Building\").\n" % name)
            env.Exit(1)
