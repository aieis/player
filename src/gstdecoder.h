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
#include "sm.h"


struct pipe_t {
    GstElement* pipeline = NULL;
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

    std::string movie;
    int width;
    int height;
    double framerate;
    std::string format;

    Base_SM* state_machine;


    bool running;

    size_t qmax;
    moodycamel::BlockingReaderWriterQueue<frame_t> frames;
    moodycamel::BlockingReaderWriterQueue<frame_t> spares;

 public:

    Decoder(std::string movie, int flip_mehtod, Base_SM* state_machine, size_t q_size);
    ~Decoder();

    void reset();
    bool init();

    void play(decdata_f submit_data, addstr_f msg_hist, addstr_f clip_hist);
    void stop();
    void submit_frame(GstSample* sample_frame);

    int get_width() {return width;};
    int get_height() {return height;};
    double get_framerate() {return framerate;};

    int get_queue_size() {return frames.size_approx();}

    bool pop(frame_t &frame);
    bool return_frame(frame_t frame);
};
