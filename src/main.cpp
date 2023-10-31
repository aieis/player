#include <algorithm>
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
#include <vector>

#include "parse_spec.h"
#include "implot.h"

#define _TS_BUFFER_SIZE 2000

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


int main_player(char* movie, int flip_method, clip_t** sequences, int (*start_address)[2])
{
    srand(time(NULL));

    glfwSetErrorCallback (glfw_error_callback);
    if (!glfwInit ())
        return 1;

    const char *glsl_version = "#version 130";
    glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 0);

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

    clip_t cclip = get_clip(sequences, start_address);
    int start = cclip.start;
    int end = cclip.end;


    double frame_time = ((double) start - 1) / 30;
    gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET  ,
                      frame_time * GST_SECOND,
                      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);


    GLFWwindow *window = glfwCreateWindow (width, height, "RRVP - Rapid Response Video Player", NULL, NULL);
    if (window == NULL)
        return 1;

    glfwMakeContextCurrent (window);
    glfwSwapInterval (1);         // Enable vsync

    IMGUI_CHECKVERSION ();
    ImGui::CreateContext ();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark ();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL (window, true);
    ImGui_ImplOpenGL3_Init (glsl_version);

    glfwSetKeyCallback(window, ImGui_ImplGlfw_KeyCallback);


    GLuint videotex;
    glGenTextures (1, &videotex);
    glBindTexture (GL_TEXTURE_2D, videotex);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    int frame = start;

    int show_debug = 1;

    double ts_begin[_TS_BUFFER_SIZE * 2] = {};
    double fts_begin[_TS_BUFFER_SIZE * 2] = {};

    memset(ts_begin, 0, sizeof(ts_begin));
    memset(fts_begin, 0, sizeof(fts_begin));

    double* ts = ts_begin;
    double* fts = fts_begin;

    int i = 0;

    struct timeval begin_time, t1, t2;
    double elapsedTime;
    double totalTime = 0;
    gettimeofday(&begin_time, NULL);

    t2 = begin_time;
    t1 = begin_time;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        GstSample *sample_frame = gst_app_sink_pull_sample(appsink);
        GstBuffer *buffer = nullptr;
        if (sample_frame) {
            buffer = gst_sample_get_buffer(sample_frame);
        }

        if (buffer) {
            GstMapInfo map;
            gst_buffer_map (buffer, &map, GST_MAP_READ);
            glBindTexture (GL_TEXTURE_2D, videotex);
            glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, map.data);
            gst_buffer_unmap (buffer, &map);
        }

        if (sample) {
            gst_sample_unref(sample_frame);
        }

        ImGui_ImplOpenGL3_NewFrame ();
        ImGui_ImplGlfw_NewFrame ();
        ImGui::NewFrame ();

        ImGui::GetBackgroundDrawList()->AddImage((void *) (guintptr) videotex, ImVec2 (0, 0),
                                                 ImVec2 (width, height), ImVec2 (0, 0), ImVec2 (1, 1));

        if (show_debug && i > 2) {
            ImGui::SetNextWindowPos({ 0, 0 });
            float nwidth = (float)width / 2;
            float nheight = (float) height / 4;
            ImGui::SetNextWindowSize({ nwidth + 5, nheight + 5});
            ImGui::Begin("Graph", (bool*)0, ImGuiWindowFlags_NoBringToFrontOnFocus
                         | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

            int fv = std::min(i - 1, _TS_BUFFER_SIZE);

            double xs[_TS_BUFFER_SIZE];

            for (int j = 0; j < fv; j++) {
                xs[j] = ts[j] - totalTime;
            }

            ImPlot::SetNextAxesLimits(-60, 0, 0.01, 0.05);
            ImPlot::BeginPlot("frame_time", {nwidth, nheight});
            ImPlot::PlotLine("FPS", xs, fts, fv);
            ImPlot::EndPlot();

            ImGui::End();
        }

        ImGui::Render();

        int display_w, display_h;
        glfwMakeContextCurrent (window);
        glfwGetFramebufferSize (window, &display_w, &display_h);
        glViewport (0, 0, display_w, display_h);

        ImVec4 clear_color = ImVec4 (0, 0, 0, 1);
        glClearColor (clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear (GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData ());

        glfwMakeContextCurrent (window);
        glfwSwapBuffers (window);

        gettimeofday(&t2, NULL);
        elapsedTime = (t2.tv_sec - t1.tv_sec);
        elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000000.0;
        t1 = t2;

        totalTime += elapsedTime;

        ts_begin[i] = totalTime;
        fts_begin[i] = elapsedTime;
        i += 1;

        if (i > _TS_BUFFER_SIZE * 1.5) {
            memcpy(ts_begin, ts, _TS_BUFFER_SIZE * sizeof(double));
            memcpy(fts_begin, fts, _TS_BUFFER_SIZE * sizeof(double));
            ts = ts_begin;
            fts = fts_begin;
            i = _TS_BUFFER_SIZE;
        }

        if (i > _TS_BUFFER_SIZE) {
            ts = ts_begin + (i - _TS_BUFFER_SIZE);
            fts = fts_begin + (i - _TS_BUFFER_SIZE);
        }

        frame++;
        if (frame  == end) {
            cclip = find_next(sequences, cclip);
            start = cclip.start;
            frame = start;
            frame_time = ((double) frame - 1)  / 30;
            if (!gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET,
                                   frame_time * GST_SECOND,
                                   GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
                printf ("Seek failed!\n");
            }
            printf("Seeking frame %d => %f \n", start, frame_time);
            end = cclip.end;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            show_debug = !show_debug;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            break;
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

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

    gst_init (&argc, &argv);
    main_player(movie, flip, sequences, &start);
    return 0;
}
