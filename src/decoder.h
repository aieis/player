#ifndef _DECODER_H_
#define _DECODER_H_

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>

struct frame_t {
    uint8_t* data;

    frame_t(const frame_t &frame) {
        data = frame.data;
    }

    frame_t() {
        data = nullptr;
    }

    ~frame_t() {
        /* free(data); */
        /* data = nullptr; */
    }
};

inline void frame_free(frame_t frame) { free(frame.data);}

struct DecoderData {
    double tt;
    double decode_time;
    double queue_size;
};

typedef std::function<void(DecoderData)> decdata_f;
typedef std::function<void(std::string)> addstr_f;

#endif
