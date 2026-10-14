#pragma once

#include <stdint.h>

namespace spotykach {

/*
Sample slots on the card: /SK/<tape>/<slot>.wav, e.g. /SK/G/3.wav.
Shared by both decks.
*/
static constexpr uint8_t kStorageTapeCount = 6;
static constexpr uint8_t kStorageSlotCount = 6;

const char* storage_root_dir();                     // "SK"
const char* storage_tape_name(const uint8_t tape_idx); // "B", "G", ...
const char* storage_slot_name(const uint8_t slot_idx); // "1" ... "6"

/* "/SK/<tape>/<slot><ext>". out_path needs 16 bytes. */
void storage_slot_path(const uint8_t tape_idx, const uint8_t slot_idx, const char* ext, char* out_path);
/* "/SK" and "/SK/<tape>". out_path needs 8 bytes. */
void storage_root_path(char* out_path);
void storage_tape_path(const uint8_t tape_idx, char* out_path);

};
