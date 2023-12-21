#include <functional>
#include <string>
#include <cstdint>

#include <stdlib.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gstbuffer.h>

#include "readerwriterqueue.h"

#include "decoder.h"
#include "parse_spec.h"

struct pipe_t {
    GstElement* pipeline;
    GstElement* src;
    GstElement* dec;
    GstElement* flip;
    GstElement* conv;
    GstElement* scale;
    GstElement* sink;

    GstBus* bus;
    guint bus_watch_id;
};

class Decoder
{
    pipe_t pipe;
    decdata_f submit_data;
    addstr_f send_msg;
    addstr_f clip_changed;
    
    
    clip_t** sequences;
    addr_t start_address;
    int width;
    int height;
    double framerate;
    std::string format;

    bool running;

    size_t qmax;
    moodycamel::BlockingReaderWriterQueue<frame_t> frames;
    
 public:

    Decoder(std::string movie, int flip_mehtod, clip_t** clips, addr_t start_address, size_t q_size, decdata_f submit_data, addstr_f msg_hist, addstr_f clip_hist);
    ~Decoder();
    
    bool init();
    
    void play();
    void stop();
    
    int get_width() {return width;};
    int get_height() {return height;};
    double get_framerate() {return framerate;};
    int get_queue_size() {return frames.size_approx();}
    bool pop(frame_t &frame);
};
