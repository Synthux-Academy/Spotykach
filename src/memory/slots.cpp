#include "slots.h"

#include <stdio.h>

using namespace spotykach;

static const char* kRootDir = "SK";
static const char* kTapeName[kStorageTapeCount] = {
    "B", // B lue
    "G", // G reen
    "P", // P ink
    "R", // R ed
    "T", // T urquose
    "Y"  // Y ellow
};
static const char* kSlotName[kStorageSlotCount] = { "1", "2", "3", "4", "5", "6" };

const char* spotykach::storage_root_dir() { return kRootDir; }
const char* spotykach::storage_tape_name(const uint8_t tape_idx) { return kTapeName[tape_idx]; }
const char* spotykach::storage_slot_name(const uint8_t slot_idx) { return kSlotName[slot_idx]; }

void spotykach::storage_slot_path(const uint8_t tape_idx, const uint8_t slot_idx, const char* ext, char* out_path)
{
    sprintf(out_path, "/%s/%s/%s%s", kRootDir, kTapeName[tape_idx], kSlotName[slot_idx], ext);
}

void spotykach::storage_root_path(char* out_path)
{
    sprintf(out_path, "/%s", kRootDir);
}

void spotykach::storage_tape_path(const uint8_t tape_idx, char* out_path)
{
    sprintf(out_path, "/%s/%s", kRootDir, kTapeName[tape_idx]);
}
