#include "decode.h"
#include "gst/app/gstappsink.h"
#include "gst/gstbuffer.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstsegment.h"

#include <chrono>
#include <cstdint>
#include <stdio.h>
#include <sys/time.h>

#include <gst/gstobject.h>
#include <thread>

clip_t find_next(clip_t** sequences, clip_t clip)
{
    printf("%d %d %d\n", clip.address[0], clip.address[1], clip.njumps);
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

    create_pipeline(&pipeline);
    check_pipeline_for_message(&pipeline);
}

Decoder::~Decoder()
{
    g_source_remove (bus_watch_id);
    cleanup_pipeline(&pipeline);
}

bool Decoder::init()
{
    printf("Pull a sample!\n");
    GstSample *sample = NULL;

    while (!sample) {
        printf("sample is NULL\n");
        sample = gst_app_sink_try_pull_sample(GST_APP_SINK(pipeline.appsink), 10 * GST_MSECOND);
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);

    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    gint fnum;
    gint den;

    gst_structure_get_fraction(structure, "framerate", &fnum, &den);

    framerate = (double)fnum / den;
    
    const gchar * format_local = gst_structure_get_string(structure, "format");
    format = std::string(format_local);
    gst_sample_unref(sample);

    printf("Video Properties: %dx%d %f FPS %s\n", width, height, framerate, format.c_str());
    return true;
}

void Decoder::play()
{
    pipeline.pipeState = GST_STATE_PLAYING;
    update_pipeline_state(&pipeline);

    GstAppSink* appsink = GST_APP_SINK(pipeline.appsink);

    clip_t cclip = get_clip(sequences, start_address);
    int start = cclip.start;
    int end = cclip.end;
    int frame = start;

    double frame_time = ((double) start - 1) / framerate;
    gst_element_seek (pipeline.videosrc, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET,
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
        bool eos = gst_app_sink_is_eos(appsink);
        printf("Is EOS: %d\n", eos);
        GstSample *sample_frame = gst_app_sink_pull_sample(appsink);

        if (!sample_frame) {
            printf("NULL Sample!\n");
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
            frame_time = ((double) frame - 1)  / framerate;

            printf("Seeking frame %d => %f \n", start, frame_time);
            if (!gst_element_seek (pipeline.videosrc, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                                   frame_time * GST_SECOND,
                                   GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
                printf ("Seek failed!\n");
            }
            printf("Seek successful!\n");
            end = cclip.end;
        }

        
        gettimeofday(&t2, NULL);
        elapsedTime = (t2.tv_sec - t1.tv_sec);
        elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000000.0;
        t1 = t2;
        totalTime += elapsedTime;

        double qsize = frames.size_approx();
        
        submit_data({totalTime, elapsedTime, qsize});

        pipeline.pipeState = GST_STATE_PLAYING;
        update_pipeline_state(&pipeline);
        check_pipeline_for_message(&pipeline);
    }
}

void Decoder::stop()
{
    running = false;
    pipeline.pipeState = GST_STATE_NULL;
    update_pipeline_state(&pipeline);
}

bool Decoder::pop(frame_t &frame)
{
    return frames.try_dequeue(frame);
}

void Decoder::query()
{
    check_pipeline_for_message(&pipeline);
}
