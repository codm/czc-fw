#!/usr/bin/env python3

Import("env")

from subprocess import call
import shutil
import os
import time
from glob import glob
import sys

sys.path.append("./tools")
from func import print_logo

VERSION_HEADER = "version.h"

def extract_version_from_file(file_path):
    try:
        with open(file_path, 'r') as file:
            for line in file:
                if '#define VERSION' in line:
                    version = line.split('"')[1]
                    return version
    except FileNotFoundError:
        print("File not found")
    return None

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
    
    VERSION_NUMBER = extract_version_from_file(VERSION_FILE)
    
    NEW_NAME_BASE = "bin/XZG_" + VERSION_NUMBER
    NEW_NAME_FULL = NEW_NAME_BASE + ".full.bin"
    NEW_NAME_OTA = NEW_NAME_BASE + ".ota.bin"

    shutil.move("bin/XZG.full.bin", NEW_NAME_FULL)
    shutil.move("bin/firmware.bin", NEW_NAME_OTA)

    print("")
    print("--------------------------------------------------------")
    print("{} created !".format(str(NEW_NAME_FULL)))
    print("{} created !".format(str(NEW_NAME_OTA)))
    print("--------------------------------------------------------")
    print("")
    print_logo()
    print("Build " + VERSION_NUMBER)

env.AddPostAction("buildprog", after_build)

firmware_source = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")