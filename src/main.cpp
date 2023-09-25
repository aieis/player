#include <argp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>


#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include "gst/gstbuffer.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "parse_spec.h"

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
      //printf("%sn\n", GST_MESSAGE_TYPE_NAME(msg));
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
    g_object_set(gst_bin_get_by_name(GST_BIN(pipeline), "appsrc"), "caps", gst_caps_from_string(caps_args), NULL);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    
    g_main_loop_run (loop);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (pipeline));
    g_main_loop_unref (loop);
    return NULL;

}

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


static void glfw_error_callback (int error, const char *description)
{
  g_print ("GLFW Error %d: %s\n", error, description);
}


int
main_player(char* movie, int flip_method, clip_t** sequences, int (*start_address)[2])
{
    srand(time(NULL));

     glfwSetErrorCallback (glfw_error_callback);
     if (!glfwInit ())
       return 1;

     const char *glsl_version = "#version 130";
     glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
     glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 0);

     GLFWwindow *window =
       glfwCreateWindow (1280, 720, "Dear ImGui GLFW++OpenGL3+Gstreamer example",
                         NULL,
                         NULL);
     if (window == NULL)
       return 1;
     glfwMakeContextCurrent (window);
     glfwSwapInterval (1);         // Enable vsync
     
     IMGUI_CHECKVERSION ();
     ImGui::CreateContext ();
     ImGui::StyleColorsDark ();

     ImGui_ImplGlfw_InitForOpenGL (window, true);
     ImGui_ImplOpenGL3_Init (glsl_version);

        
    GstElement *pipeline;
    const char* pipe_args_fmt =
        "filesrc location=%s name=filesrc"
        " ! decodebin"
        " ! videoflip video-direction=%d"
        " ! videoconvert ! video/x-raw,format=RGBA ! videoconvert ! queue ! appsink name=sink";

    char pipe_args[2048];
    sprintf(pipe_args, pipe_args_fmt, movie, flip_method);


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
    char* format = (char*) malloc((len + 1) * sizeof(gchar));
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

    clip_t cclip = get_clip(sequences, start_address);
    int start = cclip.start;
    int end = cclip.end;

    
    double frame_time = ((double) start - 1) / 30;
    gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET  ,
                      frame_time * GST_SECOND,
                      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    
    int frame = start;
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
        
        frame++;
        if (frame  == end) {
            cclip = find_next(sequences, cclip);
            start = cclip.start;
            frame = start;
            frame_time = ((double) frame - 1)  / 30;
            if (!gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET  ,
                                   frame_time * GST_SECOND,
                                   GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
                printf ("Seek failed!\n");
            }
            //gst_element_set_state(pipeline, GST_STATE_PLAYING);
            printf("Seeking frame %d => %f \n", start, frame_time);
            end = cclip.end;
            /* char str[100]; */
            /* scanf("%s", str); */
        }

        
        
    }

    g_source_remove (bus_watch_id);
    free(format);
    return 0;
}



char* movie = "./vid/vid.mp4";
char* spec = "./vid/spec.txt";
int flip = 0;

static int parse_opt (int key, char * arg, struct argp_state *state)
{
    switch (key) {
    case 'm':
        movie = arg;
        break;
    case 's':
        spec = arg;
        break;
    case 'r':
        flip = atoi(arg);
        break;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct argp_option options[] = {
        {"movie", 'm', "path_to_movie", 0, "Specify path to the movie file"},
        {"spec", 's', "path_to_spec", 0, "Specify path to the spect file"},
        {"rot", 'r', "rotation_num", 0, "Specify rotation methon"},
        {0}
    };

    struct argp argp = {options, parse_opt};
    argp_parse(&argp, argc, argv, 0, 0, 0);

    printf("%s %s %d\n", movie, spec, flip);
    
    int start[2];
    clip_t** sequences = parse_spec(spec, &start);
    /* init GStreamer */
    gst_init (&argc, &argv);
    main_player(movie, flip, sequences, &start);
    return 0;
}
