#include "gstdecoder.h"

#include "decoder.h"

#include "gst/app/gstappsink.h"
#include "gst/base/gstbasesink.h"
#include "gst/gstbuffer.h"
#include "gst/gstbus.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstelementfactory.h"
#include "gst/gstformat.h"
#include "gst/gstmessage.h"
#include "gst/gstpad.h"
#include "gst/gststructure.h"
#include "gst/gstutils.h"

#include "spdlog/spdlog.h"

#include <chrono>
#include <cstdint>
#include <stdio.h>
#include <sys/time.h>

#include <gst/gstobject.h>
#include <thread>
#include <unistd.h>

static bool set_pipeline_state(GstElement* element, GstState target_state, int timeout_ms);

static std::string bus_to_text (GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
        return "End of stream\n";

    case GST_MESSAGE_ERROR: {
        gchar  *debug;
        GError *error;

        gst_message_parse_error (msg, &error, &debug);
        g_free (debug);

        std::string err(error->message);
        g_error_free (error);

        return "Error: " + err;
    }

    default:
        return GST_MESSAGE_TYPE_NAME(msg);
    }

    return GST_MESSAGE_TYPE_NAME(msg);
}

void bus_handle_msgs(GstBus* bus, std::function<void(std::string)> send_msg)
{
    while (true) {
        GstMessage* msg = gst_bus_pop(bus);

	if (!msg) {
	    break;
	}

	std::string msgval = bus_to_text(msg);
        send_msg(msgval);
        gst_message_unref(msg);
    }
}


Decoder::Decoder(std::string i_movie, int flip_method, Base_SM* i_sm, size_t q_size)
{
    width = 0;
    height = 0;
    running = false;
    format = "";

    movie = i_movie;
    state_machine = i_sm;

    qmax = q_size;
    frames = moodycamel::BlockingReaderWriterQueue<frame_t>(qmax);

    reset();
}

Decoder::~Decoder()
{
    if (pipe.pipeline != NULL) {
        set_pipeline_state(pipe.pipeline, GST_STATE_NULL, 5000);
        gst_object_unref(pipe.bus);
        gst_object_unref(pipe.sink);
        gst_object_unref(pipe.pipeline);
        pipe.pipeline = NULL;
    }

    frame_t frame;
    while (spares.try_dequeue(frame)) {
        frame_free(frame);
    }
}

void Decoder::reset() {
    if (pipe.pipeline != NULL) {
        set_pipeline_state(pipe.pipeline, GST_STATE_NULL, 5000);
        gst_object_unref(pipe.bus);
        gst_object_unref(pipe.sink);
        gst_object_unref(pipe.pipeline);
        pipe.pipeline = NULL;
    }

#ifdef __aarch64__
    const char *pipe_args_fmt =
        "filesrc location=%s name=filesrc"
        " ! qtdemux ! h264parse ! nvv4l2decoder"
        " ! nvvidconv ! video/x-raw(memory:NVMM) ! nvvidconv ! video/x-raw,format=(string)RGBA"
        " ! appsink name=sink async=true sync=false drop=false";
#else

    const char *pipe_args_fmt =
        "filesrc location=%s name=filesrc"
        " ! decodebin"
        " ! videoconvert ! video/x-raw,format=(string)RGBA"
        " ! appsink name=sink async=true sync=false drop=false";
#endif

    char pipe_args[2048];
    sprintf(pipe_args, pipe_args_fmt, movie.c_str());

    spdlog::info("{}", pipe_args);

    pipe.pipeline = gst_parse_launch(pipe_args, NULL);

    pipe.sink = gst_bin_get_by_name(GST_BIN(pipe.pipeline), "sink");

    set_pipeline_state(pipe.pipeline, GST_STATE_PLAYING, 0.2*GST_SECOND);

    pipe.bus = gst_element_get_bus(pipe.pipeline);
}

bool Decoder::init()
{
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(pipe.sink));
    while (!sample) {
        spdlog::warn("sample is NULL");
        sample = gst_app_sink_pull_sample(GST_APP_SINK(pipe.sink));
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);

    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    int numer;
    int denom;

    gst_structure_get_fraction(structure, "framerate", &numer, &denom);
    framerate = (double)numer/denom;

    const gchar * format_local = gst_structure_get_string(structure, "format");
    format = std::string(format_local);
    gst_sample_unref(sample);

    spdlog::info("Video properties: {}x{} {} FPS {}", width, height, framerate, format);
    return true;
}

static bool set_pipeline_state(GstElement* element, GstState target_state, int timeout_ms) {
    auto now = std::chrono::steady_clock::now();
    auto end = now + std::chrono::milliseconds(timeout_ms);
    GstState current_state = GST_STATE_NULL, pending;
    gst_element_set_state(element, target_state);

    do {
        gst_element_get_state(element, &current_state, &pending, 0.01 * GST_SECOND);
        now = std::chrono::steady_clock::now();
    } while (now < end && current_state != target_state);

    return current_state == target_state;
}

static GstSample* wait_for_sample(GstAppSink* sink, double start_ts)
{
    int attempts = 0;
    while (attempts < 25) {
        GstSample* sample_frame = gst_app_sink_try_pull_sample(sink, 0.33 * GST_SECOND);
        if (!sample_frame) {
            spdlog::warn("Waiting for sample. Null received.");
            attempts += 5;
            continue;
        }

        GstBuffer *buffer =  gst_sample_get_buffer(sample_frame);
        GstClockTime position = GST_BUFFER_TIMESTAMP(buffer);

        spdlog::info("Current position: {} \t DesiredPosition {}", position, start_ts * GST_SECOND);

        auto diff = position - start_ts * GST_SECOND;

        if (diff < 0.01 * GST_SECOND && diff > -0.01 * GST_SECOND) {
            return sample_frame;
        }

        gst_sample_unref(sample_frame);
        attempts += 1;
    }

    return nullptr;
}

void Decoder::submit_frame(GstSample* sample_frame)
{
    size_t frame_size = 4 * width * height * sizeof(uint8_t);

    GstBuffer *buffer =  gst_sample_get_buffer(sample_frame);
    if (buffer) {
        GstMapInfo map;
        gst_buffer_map (buffer, &map, GST_MAP_READ);

        frame_t frame;
        if (!spares.try_dequeue(frame)) {
	    frame.data = reinterpret_cast<uint8_t*>(malloc(frame_size));
        }

        memcpy(frame.data, map.data, frame_size);
        gst_buffer_unmap(buffer, &map);
        frames.enqueue(frame);
    }

}

void Decoder::play(decdata_f submit_data, addstr_f send_msg, addstr_f clip_changed)
{
    Clip cclip = state_machine->current();
    int start = cclip.start;
    int end = cclip.end;
    int current_frame = start;

    double start_ts = ((double) current_frame - 1) / framerate;


    spdlog::info("Starting playback with segment {}: {} - {}.", cclip.name, start, end);

    gst_element_seek (pipe.pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                      start_ts * GST_SECOND,
                      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

    GstSample* sample_frame = wait_for_sample(GST_APP_SINK(pipe.sink), start_ts);
    if (!sample_frame) {
        spdlog::critical("Fatal error encountered. Could not seek to start point.");
    }

    submit_frame(sample_frame);
    gst_sample_unref(sample_frame);


    running = true;

    double total_time = 0;
    auto t1 = std::chrono::steady_clock::now();
    auto t2 = t1;
    double elapsed_time;

    bool paused = false;

    int count = 0;

    while (running) {
        bus_handle_msgs(pipe.bus, send_msg);
        GstSample *sample_frame = gst_app_sink_try_pull_sample(GST_APP_SINK(pipe.sink), 0.33 * GST_SECOND);

        if (!sample_frame) {
            if (paused && frames.size_approx() < qmax * 0.75) {
                set_pipeline_state(pipe.pipeline, GST_STATE_PLAYING, 100);
                paused = false;
            } else if (!paused){
                spdlog::error("Failed to pull sample.");
                count++;

                if (count > 2) {
                    spdlog::critical("Resetting the pipeline.");
		    count = 0;
		    reset();
		    start_ts = ((double)current_frame - 1) / framerate;
		    spdlog::info("Seeking frame {} => {}", current_frame, start_ts);

                    gst_element_seek (pipe.pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                                      start_ts * GST_SECOND,
                                      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

                    GstSample* sample_frame = wait_for_sample(GST_APP_SINK(pipe.sink), start_ts);
                    if (!sample_frame) {
                        spdlog::critical("Fatal error encountered. Could not seek to previous point. Going back as far as possible.");
                        reset();
                        return play(submit_data, send_msg, clip_changed);
                    }

                    submit_frame(sample_frame);
                    gst_sample_unref(sample_frame);

                }
            }

            continue;
        }

	count = 0;

        submit_frame(sample_frame);
        gst_sample_unref(sample_frame);

        t2 = std::chrono::steady_clock::now();
        elapsed_time = (double) std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() / 1000;
        t1 = t2;
        total_time += elapsed_time;

        double qsize = frames.size_approx();

        submit_data({total_time, elapsed_time, qsize});

        current_frame++;

        if (current_frame == end) {
            cclip = state_machine->next();
            start = cclip.start;
            current_frame = start;
            start_ts = ((double)current_frame - 1) / framerate;

	    spdlog::info("State {{name: '{}', start: {}, end: {} }}", cclip.name, cclip.start, cclip.end);
	    spdlog::info("Seeking frame {} => {}", start, start_ts);

            if (!gst_element_seek(pipe.pipeline, 1.0, GST_FORMAT_TIME,
                                  GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                                  start_ts * GST_SECOND, GST_SEEK_TYPE_NONE,
                                  GST_CLOCK_TIME_NONE)) {
                spdlog::critical("Seek failed!");
            }

            spdlog::info("Seeking successful {} => {}", start, start_ts);
            clip_changed(cclip.name);
            end = cclip.end;
        } else if (frames.size_approx() >= qmax * 1.25 ) {
            set_pipeline_state(pipe.pipeline, GST_STATE_PAUSED, 100);
            paused = true;
        }
    }
}

void Decoder::stop()
{
    running = false;
    gst_element_set_state (pipe.pipeline, GST_STATE_NULL);
}

bool Decoder::pop(frame_t &frame)
{
    return frames.try_dequeue(frame);
}

bool Decoder::return_frame(frame_t frame) {
    return spares.enqueue(frame);
}
