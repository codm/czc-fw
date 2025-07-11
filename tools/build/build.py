#!/usr/bin/env python3

Import("env")

import subprocess
from subprocess import call
import shutil
import os
import time
from glob import glob
import sys

sys.path.append("./tools")
from func import print_logo
from func import print_colored

VERSION_HEADER = "version.h"


def get_last_git_tag():
    command = ['git', 'describe', '--exact-match', '--tags']
    tmp = subprocess.check_output(command)
    return tmp[1:-1]

def after_build(source, target, env):
    time.sleep(2)
    shutil.copy(firmware_source, "bin/firmware.bin")
    for f in glob("bin/XZG*.bin"):
        os.unlink(f)

    exit_code = call(
        "python tools/build/merge_bin_esp.py --output_folder ./bin --output_name XZG.full.bin --bin_path bin/bootloader_dio_40m.bin bin/firmware.bin bin/partitions.bin --bin_address 0x1000 0x10000 0x8000",
        shell=True,
    )

    VERSION_FILE = "src/" + VERSION_HEADER
    
    VERSION_NUMBER = get_last_git_tag()
    VERSION_NUMBER = str(VERSION_NUMBER, "utf-8")   # Version Number --> String | VN = b"V2..." type Byte

    NEW_NAME_BASE = "bin/czc_fw_" + VERSION_NUMBER

    build_env = env['PIOENV']
    if "debug" in build_env:
        NEW_NAME_BASE += "_" + build_env
    
    NEW_NAME_FULL = NEW_NAME_BASE + ".full.bin"
    NEW_NAME_OTA = NEW_NAME_BASE + ".ota.bin"

    shutil.move("bin/XZG.full.bin", NEW_NAME_FULL)
    shutil.move("bin/firmware.bin", NEW_NAME_OTA)

    print("")
    print_colored("--------------------------------------", "yellow")
    print_colored("{} created !".format(str(NEW_NAME_FULL)), "blue")
    print_colored("{} created !".format(str(NEW_NAME_OTA)), "magenta")
    print_colored("--------------------------------------", "yellow")
    print_logo()
    print_colored("Build " + VERSION_NUMBER, "cyan")
    print("")

env.AddPostAction("buildprog", after_build)

firmware_source = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")

