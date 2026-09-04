# Copy companion firmware binaries into the SD pack after a companion env build.
Import("env")
import os
import shutil

DST_DIR = os.path.join(env.subst("$PROJECT_DIR"), "tools", "sd_pack", "support_files", "companions")


def after_build(source, target, env):
    name = env.subst("$PIOENV")
    if not name.startswith("companion-"):
        return
    src = env.subst("$BUILD_DIR/firmware.bin")
    if not os.path.isfile(src):
        return
    os.makedirs(DST_DIR, exist_ok=True)
    dst = os.path.join(DST_DIR, name + ".bin")
    shutil.copy2(src, dst)
    print("copy_companions:", dst)


env.AddPostAction("$BUILD_DIR/firmware.bin", after_build)
