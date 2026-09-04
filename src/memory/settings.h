#pragma once

#include "hw/hardware.h"
#include "nocopy.h"

namespace spotykach {

class Settings {
public:
    static constexpr uint8_t kDataVersion = 5;

    Settings() = default;
    ~Settings() = default;

    struct Data {
        Data() { memset(&offset, 0, sizeof(float) * Hardware::CV_LAST); }
        ~Data() = default;

        float offset[Hardware::CV_LAST];
        infrasonic::CalibratedVOct::ReferenceReadings v_oct_a;
        infrasonic::CalibratedVOct::ReferenceReadings v_oct_b;
        uint8_t version = 1;

        bool operator != (const Data& other) const { return true; }
    };

    void init(Hardware& hw) {
        _storage.Init(hw.seed.qspi, _data, storage_version);
    }

    inline bool is_user_defined() 
    {
        return static_cast<int>(_storage.GetState()) == 2; //USER
    }

    inline Data& data() { 
        return _data; 
    }

    void read() 
    {
        _data = _storage.GetSettings();
    }

    bool write() 
    {
        _storage.Save(_data);
        read();
        return is_user_defined();
    }


private:
    NOCOPY(Settings)

    static constexpr char slug[4] = "cal";
    static constexpr uint8_t storage_version = 1;
    daisy::PersistentStorage<Data, slug, storage_version> _storage;
    Data _data;
};

};
