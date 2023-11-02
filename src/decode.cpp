#include "decode.h"
#include "gst/gstbuffer.h"

#include <chrono>
#include <cstdint>
#include <stdio.h>
#include <sys/time.h>

#include <gst/gstobject.h>
#include <thread>

clip_t find_next(clip_t** sequences, clip_t clip)
{
    int jind = rand() % clip.njumps;
    int addr[2];
    addr[0] = clip.addresses[jind * 2];
    addr[1] = clip.addresses[jind * 2 + 1];
     clip_t next = get_clip(sequences, &addr);

    printf("Transition: (s%d.%d %d %d => s%d.%d %d %d)\n", clip.address[0], clip.address[1], clip.start, clip.end
           , next.address[0], next.address[1], next.start, next.end);
    return next;
}

static gboolean
bus_call (GstBus     *bus, GstMessage *msg, gpointer    data)
{
    switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
        g_print ("End of stream\n");
        break;

    case GST_MESSAGE_ERROR: {
        gchar  *debug;
        GError *error;

        gst_message_parse_error (msg, &error, &debug);
        g_free (debug);

        g_printerr ("Error: %s\n", error->message);
        g_error_free (error);
        break;
    }
    default:
        //printf("%sn\n", GST_MESSAGE_TYPE_NAME(msg));
        break;
    }

    return TRUE;
}

Decoder::Decoder(std::string movie, int flip_method, clip_t** isequences, addr_t i_start_address, size_t q_size, decdata_f i_submit_data)
{
    width = 0;
    height = 0;
    running = false;
    format = "";

    sequences = isequences;
    submit_data = i_submit_data;
    start_address = i_start_address;

    qmax = q_size;
    frames = moodycamel::BlockingReaderWriterQueue<frame_t>(qmax);

    const char* pipe_args_fmt =
        "filesrc location=%s name=filesrc"
        " ! decodebin"
        " ! videoflip video-direction=%d"
        " ! videoconvert ! video/x-raw,format=RGBA ! videoconvert ! queue ! appsink name=sink sync=false max-buffers=%lu";

    char pipe_args[2048];
    sprintf(pipe_args, pipe_args_fmt, movie.c_str(), flip_method, q_size);

    pipeline = gst_parse_launch(pipe_args, NULL);
    GstBus* bus = gst_element_get_bus(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    bus_watch_id = gst_bus_add_watch (bus, bus_call, NULL);
    gst_object_unref(bus);
}

Decoder::~Decoder()
{
    g_source_remove (bus_watch_id);
    if (pipeline != NULL) {
        gst_object_unref(pipeline);
    }
}

bool Decoder::init()
{
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sink) {
        printf("sink is NULL\n");
        return false;
    }

    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "filesrc");
    if (!src) {
        printf("Src is NULL\n");
        return false;
    }

    GstAppSink* appsink = GST_APP_SINK(sink);
    if (!appsink) {
        printf("appsink is NULL\n");
        return false;
    }


    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) {
        printf("sample is NULL\n");
        return false;
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);

    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    const gchar * format_local = gst_structure_get_string(structure, "format");
    format = std::string(format_local);
    gst_sample_unref(sample);
    return true;
}

void Decoder::play()
{
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sink) {
        printf("sink is NULL\n");
        return;
    }

    GstAppSink* appsink = GST_APP_SINK(sink);
    if (!appsink) {
        printf("appsink is NULL\n");
        return;
    }

    clip_t cclip = get_clip(sequences, start_address);
    int start = cclip.start;
    int end = cclip.end;
    int frame = start;

    double frame_time = ((double) start - 1) / 30;
    gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET  ,
                      frame_time * GST_SECOND,
                      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

    running = true;

    double totalTime = 0;
    struct timeval begin_time, t1, t2;
    double elapsedTime;
    gettimeofday(&begin_time, NULL);

    t2 = begin_time;
    t1 = begin_time;

    size_t frame_size = 4 * width * height * sizeof(uint8_t);

    while (running) {
        GstSample *sample_frame = gst_app_sink_pull_sample(appsink);

        if (!sample_frame) {
            continue;
        }

        GstBuffer *buffer =  gst_sample_get_buffer(sample_frame);
        if (buffer) {
            GstMapInfo map;
            gst_buffer_map (buffer, &map, GST_MAP_READ);

            frame_t frame;
            frame.data = reinterpret_cast<uint8_t*>(malloc(frame_size));
            memcpy(frame.data, map.data, frame_size);

            while (frames.size_approx() > qmax) {
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            
            frames.enqueue(frame);
        }

        gst_sample_unref(sample_frame);

        frame++;
        if (frame  == end) {
            cclip = find_next(sequences, cclip);
            start = cclip.start;
            frame = start;
            frame_time = ((double) frame - 1)  / 30;
            if (!gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                                   frame_time * GST_SECOND,
                                   GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
                printf ("Seek failed!\n");
            }
            printf("Seeking frame %d => %f \n", start, frame_time);
            end = cclip.end;
        }

        
        gettimeofday(&t2, NULL);
        elapsedTime = (t2.tv_sec - t1.tv_sec);
        elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000000.0;
        t1 = t2;
        totalTime += elapsedTime;

        double qsize = frames.size_approx();
        
        submit_data({totalTime, elapsedTime, qsize});
    }
}

void Decoder::stop()
{
    running = false;
    gst_element_set_state (pipeline, GST_STATE_NULL);
}

bool Decoder::pop(frame_t &frame)
{
    return frames.try_dequeue(frame);
}
