#!/bin/sh
# Builds the firmware's transfer code for the host and tests it, then runs the
# @synthuxacademy/spotykach-transfer package against it. Needs a C++17 compiler and Node 20+.
# The shared protocol code comes from the lib/usb-transfer submodule (git submodule update --init lib/usb-transfer).
set -e
cd "$(dirname "$0")"
ROOT=../..
SRC=$ROOT/src
USB_TRANSFER=$ROOT/lib/usb-transfer
OUT=build
CXX=${CXX:-c++}
FLAGS="-std=c++17 -g -w -Istub -I$SRC -I$USB_TRANSFER/cpp ${SANITIZE--fsanitize=address,undefined}"
mkdir -p $OUT

$CXX $FLAGS -o $OUT/harness harness.cpp fatfs_stub.cpp \
    $SRC/transfer/service.cpp $USB_TRANSFER/cpp/usb_transfer/*.cpp \
    $SRC/hw/card.cpp $SRC/memory/wav.cpp $SRC/memory/slots.cpp
$CXX $FLAGS -o $OUT/config_dump config_dump.cpp $SRC/core/config.cpp
$CXX $FLAGS -o $OUT/card_load_test card_load_test.cpp fatfs_stub.cpp $SRC/hw/card.cpp $SRC/memory/wav.cpp

echo "# card loader"
CARD=$(mktemp -d)
$OUT/card_load_test "$CARD"
rm -rf "$CARD"

# The package imports @synthuxacademy/usb-transfer; Node finds it here, the page through its import map
mkdir -p $ROOT/node_modules/@synthuxacademy
ln -sfn ../../lib/usb-transfer/js $ROOT/node_modules/@synthuxacademy/usb-transfer

echo "# protocol"
HARNESS=$PWD/$OUT/harness CONFIG_DUMP=$PWD/$OUT/config_dump node --test transfer.test.mjs

echo "# connect"
node --test connect.test.mjs
