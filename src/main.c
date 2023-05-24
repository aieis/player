#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

static void push_to_src(GstElement* appsrc, GstBuffer* buffer_og)
{
    GstFlowReturn ret;

    static GstClockTime timestamp = 0;
      
    GstMapInfo map_og;
    gst_buffer_map (buffer_og, &map_og, GST_MAP_READ);
      
    size_t size = gst_buffer_get_size(buffer_og);
    GstBuffer *buffer = gst_buffer_new_allocate (NULL, size, NULL);
      
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_WRITE);
    memcpy( (uint8_t *)map.data, (uint8_t *) map_og.data,  gst_buffer_get_size( buffer ) );
      
    GST_BUFFER_PTS (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 30);

    timestamp += GST_BUFFER_DURATION (buffer);
    g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
    
}


static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
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
        break;
    }

    return TRUE;
}

typedef struct {
    pthread_t tid;
    int width;
    int height;
    char* format;
    GstElement* appsrc;
} playback_info;


void*
playback_run(void* thread_data)
{

    playback_info* info = (playback_info*) thread_data;
    GMainLoop* loop = g_main_loop_new (NULL, FALSE);

    const char* pipe_args =
        "appsrc name=appsrc"
        " ! videoconvert ! autovideosink";

    GstElement* pipeline = gst_parse_launch(pipe_args, NULL);
    GstElement* appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "appsrc");
    info->appsrc = appsrc;
    
    char caps_args[1024];
    sprintf(caps_args, "video/x-raw,format=%s,height=%d,width=%d,framerate=30/1", info->format, info->height, info->width);
    printf("Caps: %s\n", caps_args);
    g_object_set(gst_bin_get_by_name(GST_BIN(pipeline), "appsrc"), "caps", gst_caps_from_string(caps_args));

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    
    g_main_loop_run (loop);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (pipeline));
    g_main_loop_unref (loop);
    return NULL;

}

int find_next()
{
    return rand() % 40000;
}

int
main_player()
{
    GstElement *pipeline;
    const char* pipe_args =
        "filesrc location=./vid/vid.mov name=filesrc"
        " ! decodebin"
        " ! videoflip video-direction=3"
        " ! videoconvert ! video/x-raw,format=RGBA ! videoconvert ! appsink name=sink";
    
    pipeline = gst_parse_launch(pipe_args, NULL);
    GstBus* bus = gst_element_get_bus(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    guint bus_watch_id = gst_bus_add_watch (bus, bus_call, NULL);
    gst_object_unref(bus);
    
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sink) {
        printf("sink is NULL\n");
        return -1;
    }

    
    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "filesrc");
    if (!src) {
        printf("Src is NULL\n");
    }

    GstAppSink* appsink = GST_APP_SINK(sink);
    if (!appsink) {
        printf("appsink is NULL\n");
        return -1;
    }

    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) {
        printf("sample is NULL\n");
        return -1;
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);

    int width, height;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    const gchar * format_local = gst_structure_get_string(structure, "format");
    
    size_t len = strlen(format_local);
    char* format = malloc((len + 1) * sizeof(gchar));
    memcpy(format, format_local, (len+1) * sizeof(gchar));
    
    gst_sample_unref(sample);
    printf ("video resolution is %dx%d\n", width, height);

    playback_info info;
    info.width = width;
    info.height = height;
    info.format = format;
    info.appsrc = NULL;
    pthread_create(&info.tid, NULL, playback_run, &info);

    while (info.appsrc == NULL) usleep(50);
    
    struct timeval t1, t2;
    double elapsedTime;
    gettimeofday(&t2, NULL);

    int frame = 0;
    while (1) {
        t1 = t2;
        GstSample *sample_frame = gst_app_sink_pull_sample(appsink);
        if (sample_frame == NULL) {
            continue;
        }
        GstBuffer *buffer = gst_sample_get_buffer(sample_frame);
        if (buffer == NULL) {
            continue;
        }

        push_to_src(info.appsrc, buffer);
        gst_sample_unref(sample_frame);

        gettimeofday(&t2, NULL);
        elapsedTime = (t2.tv_sec - t1.tv_sec);
        elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000000.0;   // us to ms
        printf("Frame decode time: %f\n", elapsedTime);

        if (frame % 100 == 99) {
            int f = find_next();
            gint64 frame_time = ((gint64 )f)  / 30;
            printf("Seeking frame %d => %ld \n", f, frame_time);

            if (!gst_element_seek (pipeline, 1, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET,
                                   frame_time * GST_SECOND,
                                   GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
                printf ("Seek failed!\n");
            }
        }
        
        frame++;
        
    }

    g_source_remove (bus_watch_id);
    free(format);
    return 0;
}

int main(int argc, char** argv) {

    /* init GStreamer */
    gst_init (&argc, &argv);
    main_player();
    return 0;
}
