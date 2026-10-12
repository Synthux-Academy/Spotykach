// Host stand-in for libDaisy, just enough for the transfer and card code.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "ff.h"
#define DSY_SDRAM_BSS
inline void __disable_irq() {}
inline void __enable_irq() {}
namespace daisy {
struct System { static uint32_t GetNow(); static void Delay(uint32_t) {} };
struct SdmmcHandler { enum class Speed { MEDIUM_SLOW }; enum class BusWidth { BITS_1 };
  struct Config { Speed speed; BusWidth width; void Defaults() {} }; void Init(Config) {} };
struct FatFSInterface { struct Config { enum Media { MEDIA_SD }; };
  void Init(Config::Media) {} void DeInit() {} const char* GetSDPath() { return "0:/"; }
  FATFS& GetSDFileSystem() { static FATFS fs; return fs; } };
class UsbHandle { public:
  enum UsbPeriph { FS_INTERNAL, FS_EXTERNAL, FS_BOTH }; enum class Result { OK, ERR };
  typedef void (*ReceiveCallback)(uint8_t*, uint32_t*);
  void Init(UsbPeriph) {} void DeInit(UsbPeriph) {}
  Result TransmitExternal(uint8_t*, size_t); void SetReceiveCallback(ReceiveCallback, UsbPeriph); };
}
