#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include <iostream>

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
#include "argparse.hpp"

#include <GLFW/glfw3.h>

#include "parse_spec.h"
#include "graph.h"
#include "list.h"

#include "gstdecoder.h"
//#include "avdecoder.h"


static void glfw_error_callback (int error, const char *description)
{
    g_print ("GLFW Error %d: %s\n", error, description);
}


int main_player(const char* movie, int flip_method, clip_t** sequences, int (*start_address)[2])
{
    srand(time(NULL));

    const int q_size = 30;

    Graph ft_graph {2000, 0, 0.5};
    Graph fps_graph {2000, 0, 70};
    Graph qlen_graph {2000, 0, q_size * 1.5};

    ListView msg_hist {100};
    ListView clip_hist {100};

    decdata_f ftdata = [&](DecoderData p) {
        ft_graph.add(p.tt, p.decode_time);
        qlen_graph.add(p.tt, p.queue_size);
    };

    addstr_f msgdata = [&](std::string s) { msg_hist.add(s);};
    addstr_f clipdata = [&](std::string s) { clip_hist.add(s);};

    std::shared_ptr<Decoder> decoder;
    decoder.reset(new Decoder(std::string(movie), flip_method, sequences, start_address, q_size, ftdata, msgdata, clipdata));
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

    int show_debug = 0;

    double total_time = 0;
    auto t1 = std::chrono::steady_clock::now();
    auto t2 = t1;
    double elapsed_time;
    auto end = t1 + std::chrono::milliseconds(33);

    bool swapready = false;
    bool swapon = false;
    while (!glfwWindowShouldClose(window)) {

        frame_t frame;
        t2 = std::chrono::steady_clock::now();
        if (t2 >= end && swapon) {
            glfwSwapBuffers (window);
            swapready = false;
            elapsed_time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() / 1000;
            t1 = t2;
            end = t1 + std::chrono::milliseconds((int)frametime);

            total_time += elapsed_time;
            fps_graph.add(total_time, 1.0 / elapsed_time);
        }

        if (!swapready && decoder->pop(frame)) {
            glfwPollEvents();
            glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.data);
            frame_free(frame);
            swapready = true;

            if (ImGui::IsKeyPressed(ImGuiKey_A)) {
                show_debug = !show_debug;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
                break;
            }


            ImGui_ImplOpenGL3_NewFrame ();
            ImGui_ImplGlfw_NewFrame ();
            ImGui::NewFrame ();

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

                nwidth = (float)width / 4;
                nheight = (float) height / 3 * 2;
                ImGui::SetNextWindowPos({ width - nwidth - 5, 0 });
                ImGui::SetNextWindowSize({ nwidth + 5, nheight + 5});

                ImGui::Begin("History", (bool*)0, ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

                msg_hist.draw("Messages", nwidth, nheight / 2);
                clip_hist.draw("Clip History", nwidth, nheight / 2);
                ImGui::End();
            }

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData ());

        } else if (!swapready) {
            msg_hist.add("Failed to pop frame!");
        }

    
        swapon = swapready;

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


int main(int argc, char **argv)
{
    argparse::ArgumentParser program("rrvp");

    program.add_argument("-m", "--movie")
        .default_value(std::string{"./vid/vid.mp4"});

    program.add_argument("-s", "--spec")
        .default_value("./vid/spec.txt");

    program.add_argument("-r", "--rotation")
        .default_value(0);


    try {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    auto movie = program.get<std::string>("--movie");
    auto spec = program.get<std::string>("--spec");

    printf("%s %s %d\n", movie.c_str(), spec.c_str(), 0);

    int start[2];
    clip_t** sequences = parse_spec(spec.c_str(), &start);

    gst_init (&argc, &argv);
    main_player(movie.c_str(), 0, sequences, &start);
    return 0;
}
