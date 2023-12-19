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

#include "argparse.hpp"

#include <GLFW/glfw3.h>

#include "parse_spec.h"
#include "graph.h"
#include "list.h"

#include "gstdecoder.h"
//#include "avdecoder.h"



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
    std::string format = decoder->get_format();

    int fr_n;
    int fr_d;
    decoder->get_framerate(&fr_n, &fr_d);
    
    GMainLoop* loop = g_main_loop_new (NULL, FALSE);

    const char* pipe_args =
        "appsrc name=appsrc max-buffers=10"
        " ! videoconvert ! fpsdisplaysink video-sink=glimagesink";

    GstElement* pipeline = gst_parse_launch(pipe_args, NULL);
    GstElement* appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "appsrc");
    
    char caps_args[1024];
    sprintf(caps_args, "video/x-raw,format=%s,height=%d,width=%d,framerate=%d/%d", format.c_str(), height, width, fr_n, fr_d);
    printf("Caps: %s\n", caps_args);
    
    g_object_set(gst_bin_get_by_name(GST_BIN(pipeline), "appsrc"), "caps", gst_caps_from_string(caps_args), NULL);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    std::thread decoder_thread ([&] {decoder->play(appsrc);});

    g_main_loop_run (loop);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (pipeline));
    g_main_loop_unref (loop);
    
    
    // int show_debug = 0;
    // double total_time = 0;
    // auto t1 = std::chrono::steady_clock::now();
    // auto t2 = t1;
    // double elapsed_time;
    // auto end = t1 + std::chrono::milliseconds(33);
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
