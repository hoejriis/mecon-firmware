# pioarduino's hybrid compile copies the libraries one level under esp-idf/<component>/ into the Arduino package. mbedTLS builds
# its own three a level deeper, in esp-idf/mbedtls/mbedtls/library/, so they stayed prebuilt: a CONFIG_MBEDTLS_* option changed the
# headers and not the code. This copies them too. The hybrid pass and the stub's own Arduino compile both end here; only the
# first has these libraries.
import glob
import os
import shutil

Import("env")  # noqa: F821


def copy_mbedtls(source, target, env):
    src = os.path.join(env.subst("$BUILD_DIR"), "esp-idf", "mbedtls", "mbedtls", "library", "*.a")
    dst = os.path.join(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"),
                       env.BoardConfig().get("build.mcu"), "lib")
    found = glob.glob(src)
    if not found and os.path.isdir(os.path.join(env.subst("$BUILD_DIR"), "esp-idf")):
        env.Exit("idf5_mbedtls_libs: the hybrid pass left no mbedTLS libraries in " + src)
    for f in found:
        # The package keeps mbedTLS's TLS library as libmbedtls_2.a: libmbedtls.a there is the ESP-IDF component's own.
        name = os.path.basename(f)
        shutil.copyfile(f, os.path.join(dst, "libmbedtls_2.a" if name == "libmbedtls.a" else name))
    print("idf5_mbedtls_libs: copied %d from %s" % (len(found), os.path.dirname(src)))


env.AddPostAction("checkprogsize", copy_mbedtls)  # noqa: F821
