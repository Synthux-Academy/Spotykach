#!/bin/sh
# Builds the firmware's transfer code for the host and tests it, then runs the
# @synthuxacademy/spotykach-transfer package against it. Needs a C++17 compiler and Node 20+.
set -e
cd "$(dirname "$0")"
ROOT=../..
SRC=$ROOT/src
OUT=build
CXX=${CXX:-c++}
FLAGS="-std=c++17 -g -w -Istub -I$SRC ${SANITIZE--fsanitize=address,undefined}"
mkdir -p $OUT

$CXX $FLAGS -o $OUT/harness harness.cpp fatfs_stub.cpp \
    $SRC/transfer/service.cpp $SRC/transfer/link.cpp $SRC/hw/card.cpp $SRC/memory/wav.cpp $SRC/memory/slots.cpp
$CXX $FLAGS -o $OUT/card_load_test card_load_test.cpp fatfs_stub.cpp $SRC/hw/card.cpp $SRC/memory/wav.cpp

echo "# card loader"
CARD=$(mktemp -d)
$OUT/card_load_test "$CARD"
rm -rf "$CARD"

echo "# protocol"
HARNESS=$PWD/$OUT/harness node --test transfer.test.mjs

echo "# connect"
node --test connect.test.mjs
