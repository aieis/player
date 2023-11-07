#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <argp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>

#include "parse_spec.h"
#include "graph.h"
#include "gstdecoder.h"
#include "avdecoder.h"


static void glfw_error_callback (int error, const char *description)
{
    g_print ("GLFW Error %d: %s\n", error, description);
}


int main_player(char* movie, int flip_method, clip_t** sequences, int (*start_address)[2])
{
    srand(time(NULL));

    Graph ft_graph {2000, 0.01, 0.05};
    Graph fps_graph {2000, 0, 60};
    Graph qlen_graph {2000, 0, 15};

    decdata_f ftdata = [&](DecoderData p) {
        ft_graph.add(p.tt, p.decode_time);
        qlen_graph.add(p.tt, p.queue_size);
    };

    std::shared_ptr<Decoder> decoder;
    decoder.reset(new Decoder(std::string(movie), flip_method, sequences, start_address, 10, ftdata));
    decoder->init();

    int width = decoder->get_width();
    int height = decoder->get_height();
    double framerate = decoder->get_framerate();
    double frametime = 1000.0 / framerate;

    std::thread decoder_thread ([&] {decoder->play();});

    glfwSetErrorCallback (glfw_error_callback);
    if (!glfwInit ())
        return 1;

    const char *glsl_version = "#version 130";
    glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 0);

    printf ("video resolution is %dx%d\n", width, height);

    GLFWwindow *window = glfwCreateWindow (width, height, "RRVP - Rapid Response Video Player", NULL, NULL);
    if (window == NULL)
        return 1;

    glfwMakeContextCurrent (window);
    glfwSwapInterval (1);

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

    int show_debug = 1;

    double total_time = 0;
    auto t1 = std::chrono::steady_clock::now();
    auto t2 = t1;
    double elapsed_time;
    auto end = t1 + std::chrono::milliseconds(33);

    bool swapready = false;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame ();
        ImGui_ImplGlfw_NewFrame ();
        ImGui::NewFrame ();


        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            show_debug = !show_debug;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            break;
        }

        t2 = std::chrono::steady_clock::now();

        frame_t frame;
        if (!swapready && decoder->pop(frame)) {
            glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.data);
            frame_free(frame);
            swapready = true;
        }
            
        ImGui::GetBackgroundDrawList()->AddImage((void *) (guintptr) videotex, ImVec2 (0, 0),
                                                 ImVec2 (width, height), ImVec2 (0, 0), ImVec2 (1, 1));

        if (show_debug) {
            ImGui::SetNextWindowPos({ 0, 0 });
            float nwidth = (float)width / 2;
            float nheight = (float) height / 3 * 2;
            ImGui::SetNextWindowSize({ nwidth + 5, nheight + 5});
            ImGui::Begin("Graph", (bool*)0, ImGuiWindowFlags_NoBringToFrontOnFocus
                         | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

            ft_graph.draw("frame_decode_time (s)", nwidth, nheight / 3);
            fps_graph.draw("FPS", nwidth, nheight / 3);
            qlen_graph.draw("frame_queue_size", nwidth, nheight / 3);

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

        if (t2 >= end && swapready) {            
            glfwMakeContextCurrent (window);
            glfwSwapBuffers (window);
            swapready = false;
            elapsed_time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() / 1000;
            t1 = t2;
            end = t1 + std::chrono::milliseconds((int)frametime);

            total_time += elapsed_time;
            fps_graph.add(total_time, 1.0 / elapsed_time);
        }

    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    decoder->stop();

    frame_t frame;
    while (decoder->pop(frame))
        frame_free(frame);

    decoder_thread.join();

    while (decoder->pop(frame))
        frame_free(frame);


    return 0;
}

char* movie = (char*) "./vid/vid.mp4";
char* spec = (char*) "./vid/spec.txt";
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
