#!/bin/bash

# Thank god for inline python scripts in bash
# Read the version number from the application's json configuration
app_version=$(python3 -c\
"\
import os, json;\
app_config_file = os.path.join(os.getcwd(), 'mbed_app.json');\
f = open(app_config_file, 'r');\
print(json.loads(f.read())['config']['version-number']['value'].replace('\"',''));\
f.close();\
")

# Get the build timestamp
build_ts=$(python3 -c\
"\
import os, re;\
linker_file = os.path.join(os.getcwd(), 'BUILD', 'NRF52840_DK', 'GCC_ARM', '.profile-ld');\
f = open(linker_file, 'r');\
ts = [x for x in f.readlines() if 'MBED_BUILD_TIMESTAMP' in x][0].strip();\
ts = round(float(re.search('=(\d+.\d+)', ts)[1]));\
print(ts);\
f.close();\
")

mkdir OUTPUTS

echo "Signing application with version: $app_version (build timestamp: $build_ts)"
cp ./BUILD/NRF52840_DK/GCC_ARM/ble-fota-example.hex ./OUTPUTS/unsigned.hex
imgtool sign -k ../mbed-mcuboot-demo/signing-keys.pem --align 4 -v "$app_version+$build_ts" --header-size 0x1000 --pad-header -S 0xC0000 --pad ./OUTPUTS/unsigned.hex ./OUTPUTS/signed.hex

echo "Signed application saved to signed.hex"
echo "Generating OTA update binary..."

# Get the size of the unsigned hex
pad_size=$(hexinfo.py OUTPUTS/unsigned.hex | python3 -c\
"\
import sys, re;\
from math import ceil;\
print(hex((ceil((int(re.search('length:\s+(0x[0-9A-Za-z]+)', sys.stdin.read())[1], 16)/0x1000))+1)*0x1000));\
")

echo "Padding to size $pad_size..."

imgtool sign -k ../mbed-mcuboot-demo/signing-keys.pem --align 4 -v "$app_version+$build_ts" --header-size 0x1000 --pad-header -S "$pad_size" OUTPUTS/unsigned.hex OUTPUTS/signed-update.hex | grep 'Error' &> /dev/null

if [ $? != 0 ]; then
    echo "Failed to by pad 0x1000, padding by 0x2000..."
    # Padding of 0x1000 was not sufficient... pad by 0x2000
    # I'm too lazy to write another python script to do this right now so:
    pad_size=$(hexinfo.py OUTPUTS/unsigned.hex | python3 -c\
"\
import sys, re;\
from math import ceil;\
print(hex((ceil((int(re.search('length:\s+(0x[0-9A-Za-z]+)', sys.stdin.read())[1], 16)/0x1000))+2)*0x1000));\
")
    
    echo "Padding to size $pad_size..."
    
    imgtool sign -k ../mbed-mcuboot-demo/signing-keys.pem --align 4 -v "$app_version+$build_ts" --header-size 0x1000 --pad-header -S "$pad_size" OUTPUTS/unsigned.hex OUTPUTS/signed-update.hex
fi

echo "Update binary saved to signed-update.hex"
echo "Converting to raw binary..."
arm-none-eabi-objcopy -I ihex -O binary OUTPUTS/signed-update.hex OUTPUTS/signed-update.bin
echo "Raw update binary saved to signed-update.bin"
