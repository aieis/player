#include "gstdecoder.h"

#include "decoder.h"
#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"
#include "gst/base/gstbasesink.h"
#include "gst/gstbuffer.h"
#include "gst/gstbus.h"
#include "gst/gstelement.h"
#include "gst/gstelementfactory.h"
#include "gst/gstformat.h"
#include "gst/gstpad.h"
#include "gst/gststructure.h"
#include "gst/gstutils.h"

#include <bits/chrono.h>
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

static std::string
bus_call (GstBus *bus, GstMessage *msg, gpointer    data)
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

static void push_to_src(GstElement* appsrc, GstBuffer* buffer_og)
{
    static GstClockTime timestamp = 0;
    GstMapInfo map_og;
    gst_buffer_map (buffer_og, &map_og, GST_MAP_READ);    
      
    size_t size = gst_buffer_get_size(buffer_og);
    GstBuffer *buffer = gst_buffer_new_allocate (NULL, size, NULL);
      
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_WRITE);
    memcpy( (uint8_t *)map.data, (uint8_t *) map_og.data,  gst_buffer_get_size( buffer ) );
    gst_buffer_unmap(buffer_og, &map_og);
      
    GST_BUFFER_PTS (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 30);

    timestamp += GST_BUFFER_DURATION (buffer);
    gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
    gst_buffer_unmap(buffer, &map);
}


static void pad_added_handler(GstElement *src, GstPad *new_pad, pipe_t *data)
{
  GstPad *sink_pad = gst_element_get_static_pad(data->conv, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked(sink_pad))
  {
    g_print("  We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps(new_pad);
  new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
  new_pad_type = gst_structure_get_name(new_pad_struct);
  if (!g_str_has_prefix(new_pad_type, "video/x-raw"))
  {
    g_print("  It has type '%s' which is not raw video. Ignoring.\n", new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  ret = gst_pad_link(new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED(ret))
  {
    g_print("  Type is '%s' but link failed.\n", new_pad_type);
  }
  else
  {
    g_print("  Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref(new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref(sink_pad);
}


Decoder::Decoder(std::string movie, int flip_method, clip_t** isequences, addr_t i_start_address, size_t q_size, decdata_f i_submit_data, addstr_f i_msg_hist, addstr_f i_clip_hist)
{
    width = 0;
    height = 0;
    running = false;
    format = "";

    sequences = isequences;
    submit_data = i_submit_data;
    send_msg = i_msg_hist;
    clip_changed = i_clip_hist;
    start_address = i_start_address;

    qmax = q_size;
    frames = moodycamel::BlockingReaderWriterQueue<frame_t>(qmax);


    pipe.src = gst_element_factory_make("filesrc", "source0");
    pipe.dec = gst_element_factory_make("decodebin", "decoder0");
    pipe.conv = gst_element_factory_make("videoconvert", "conv0");
    pipe.sink = gst_element_factory_make("appsink", "sink0");

    pipe.pipeline = gst_pipeline_new ("decoder-pipe");

    if (!pipe.pipeline || !pipe.src || !pipe.dec || !pipe.conv || !pipe.sink) {
        printf("Not all elements could be created.\n");
        return;
    }

    g_object_set(pipe.src, "location", movie.c_str(), NULL);
    g_object_set(pipe.sink, "max-buffers", 2, NULL);
    g_object_set(pipe.sink, "drop", false, NULL);
    g_object_set(pipe.sink, "sync", false, NULL);
     g_object_set(pipe.sink, "async", true, NULL);
    
    gst_bin_add_many (GST_BIN (pipe.pipeline), pipe.src, pipe.dec, pipe.conv, pipe.sink, NULL);
    g_signal_connect(pipe.dec, "pad-added", G_CALLBACK(pad_added_handler), &pipe);

    
    gboolean link = gst_element_link(pipe.src, pipe.dec);
    g_assert(link);
    
    GstCaps* caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "RGBA", NULL);
    link = gst_element_link_filtered(pipe.conv, pipe.sink, caps);
    g_assert(link);
    
    
    if (!link) {
        printf("Error when linking pipeline \n");
    }


    gst_element_set_state(pipe.pipeline, GST_STATE_READY);
    gst_element_set_state(pipe.pipeline, GST_STATE_PLAYING);

    pipe.bus = gst_element_get_bus(pipe.pipeline);
    //pipe.bus_watch_id = gst_bus_add_watch (pipe.bus, bus_call, NULL);
    

}

Decoder::~Decoder()
{
    if (pipe.pipeline != NULL) {
        //g_source_remove (pipe.bus_watch_id);
        gst_object_unref(pipe.bus);
        gst_object_unref(pipe.pipeline);
        pipe.pipeline = NULL;
    }
}

bool Decoder::init()
{
    GstSample *sample = NULL;
    while (!sample) {
        printf("sample is NULL\n");
        sample = gst_app_sink_pull_sample(GST_APP_SINK(pipe.sink));
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);

    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);
    
    gst_structure_get_fraction(structure, "framerate", &fr_n, &fr_d);
    framerate = (double)fr_n/fr_d;

    const gchar * format_local = gst_structure_get_string(structure, "format");
    format = std::string(format_local);
    gst_sample_unref(sample);

    printf("Video properties: %dx%d %f FPS %s\n", width, height, framerate, format.c_str());
    return true;
}

void Decoder::play(GstElement* appsrc)
{
    clip_t cclip = get_clip(sequences, start_address);
    int start = cclip.start;
    int end = cclip.end;
    int frame = start;

    double frame_time = ((double) start - 1) / framerate;
    gst_element_seek (pipe.pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET  ,
                      frame_time * GST_SECOND,
                      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

    running = true;

    double total_time = 0;
    auto t1 = std::chrono::steady_clock::now();
    auto t2 = t1;
    double elapsed_time;
    
    size_t frame_size = 4 * width * height * sizeof(uint8_t);
    while (running) {
        GstMessage* msg = gst_bus_timed_pop(pipe.bus, 2);
        if (msg != NULL) {
            std::string msgval = bus_call(pipe.bus, msg, NULL);
            send_msg(msgval);
        }

        GstSample *sample_frame = gst_app_sink_try_pull_sample(GST_APP_SINK(pipe.sink), 33);

        if (!sample_frame) {
            continue;
        }
        
        if (!sample_frame) {
            continue;
        }

        GstBuffer *buffer =  gst_sample_get_buffer(sample_frame);
        if (buffer) {
            // GstMapInfo map;
            // gst_buffer_map (buffer, &map, GST_MAP_READ);
            push_to_src(appsrc, buffer);

            while (gst_app_src_get_current_level_buffers(GST_APP_SRC(appsrc)) > qmax) {
                std::this_thread::sleep_for(std::chrono::milliseconds(3));                
            }
            
            // frame_t frame;
            // frame.data = reinterpret_cast<uint8_t*>(malloc(frame_size));
            // memcpy(frame.data, map.data, frame_size);
            // gst_buffer_unmap(buffer, &map);

            // while (frames.size_approx() > qmax) {
            //     std::this_thread::sleep_for(std::chrono::milliseconds(3));
            // }

            // frames.enqueue(frame);
        }

        gst_sample_unref(sample_frame);

        frame++;
        if (frame  == end) {
            cclip = find_next(sequences, cclip);
            start = cclip.start;
            frame = start;
            frame_time = ((double) frame - 1)  / framerate;
            
            printf("Seeking frame %d => %f \n", start, frame_time);

            if (!gst_element_seek (pipe.pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                                   frame_time * GST_SECOND,
                                   GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
                printf ("Seek failed!\n");
            }
            
            printf("Seeking successful %d => %f \n", start, frame_time);
            clip_changed(std::to_string(cclip.address[0]) + "." + std::to_string(cclip.address[1]));
            
            end = cclip.end;
        }


        t2 = std::chrono::steady_clock::now();
        elapsed_time = (double) std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() / 1000;
        t1 = t2;
        total_time += elapsed_time;

        double qsize = frames.size_approx();

        submit_data({total_time, elapsed_time, qsize});
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
