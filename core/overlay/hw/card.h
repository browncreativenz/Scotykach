/**
 * @file card.h
 * @brief Desktop stand-in for the firmware's src/hw/card.h: no SD card.
 *
 * The same interface, behaving as a module with no card inserted: mounting
 * fails, files don't exist, nothing reads or writes. The firmware's Storage
 * then stays idle, as it does on such a module. The plugin keeps loops in
 * Live's set instead (S2).
 *
 * The build writes this file over the firmware copy's hw/card.h.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "nocopy.h"

namespace spotykach {

class Card
{
  public:
    static constexpr size_t kChunk = 32768; //32KB

    enum class State: uint8_t {
      unmounted,
      mounting,
      idle,
      read_file,
      write_file,
      write_audio,
      read_audio,
      failed
    };

    struct AudioData {
      uint8_t* header;
      size_t header_size;
      uint8_t* body;
      size_t body_size;
      char* root_dir;
      char* deck_dir;
      char* tape_dir;
      char* file_name;
      size_t* cue_points;
      uint8_t* cue_count;
    };

    Card() = default;
    ~Card() = default;

    State state() const { return State::unmounted; }
    float progress() const { return 0.f; }

    void init(uint8_t*) {}
    void recognize() {}
    bool mount() { return false; }
    void unmount() {}

    bool file_exists(const char*) { return false; }

    void init_read_audio(AudioData) {}
    void read_audio() {}
    size_t size_audio() const { return 0; }

    void init_write_audio(AudioData) {}
    void write_audio() {}

    bool write_file(const char*, const uint8_t*, const size_t) { return false; }
    bool read_file(const char*, uint8_t*& out_data, size_t* out_size)
    {
        out_data = nullptr;
        if (out_size) *out_size = 0;
        return false;
    }

    bool notify_finish_processing() { return false; }

    void cancel() {}

  private:
    NOCOPY(Card)
};
};
