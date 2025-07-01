#include "parse_spec.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
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
#include <string_view>

#include <filesystem>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include "imgui_impl_glfw.h"
#include "vulkan_interop.h"


#include "implot.h"
#include "argparse.hpp"

#include "sm.h"
#include "graph.h"
#include "list.h"

#include "gstdecoder.h"
//#include "avdecoder.h"

void init_logger(const std::string log_file) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file.c_str(), 4 * 1024 * 1024, 10));
    auto logger = std::make_shared<spdlog::logger>("logger", begin(sinks), end(sinks));
    logger->set_pattern("%^[%Y-%m-%d %H:%M:%S %z] [%n] [---%L---] [thread %t] %v%$");
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::trace);
}

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;

    spdlog::error("[vulkan] Error: VkResult = {}", (int) err);

    if (err < 0)
        abort();
}

static void glfw_error_callback (int error, const char *description)
{
    spdlog::error("GLFW Error {}: {}", error, description);
}


int main_player(const char* movie, int flip_method, Base_SM* state_machine, bool vsync, bool debug)
{
    srand(time(NULL));

    const int q_size = 48;

    ListView msg_hist (100);
    ListView clip_hist (100);

    addstr_f msgdata = [&](std::string s) { msg_hist.add(s);};
    addstr_f clipdata = [&](std::string s) { clip_hist.add(s);};

    std::shared_ptr<Decoder> decoder;
    decoder.reset(new Decoder(std::string(movie), flip_method, state_machine, q_size));
    decoder->init();

    int width = decoder->get_width();
    int height = decoder->get_height();
    double framerate = decoder->get_framerate();
    double frametime_ns = 1000000000.0 / framerate;

    int max_graph_elems = (int) (framerate * 2 * 60) * 10;

    Graph ft_graph (max_graph_elems, 0, 0.5);
    Graph fps_graph (max_graph_elems, 0, 140);
    Graph qlen_graph (max_graph_elems, 0, q_size * 1.5);

    decdata_f ftdata = [&](DecoderData p) {
        ft_graph.add(p.tt, p.decode_time);
    };

    std::thread decoder_thread ([&] {decoder->play(ftdata, msgdata, clipdata);});

    glfwSetErrorCallback (glfw_error_callback);
    if (!glfwInit ())
        return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    spdlog::info("video resolution is {}x{}", width, height);

    GLFWwindow *window = glfwCreateWindow (width, height, "RRVP", NULL, NULL);
    if (window == NULL)
        return 1;

    if (!glfwVulkanSupported())
    {
        spdlog::info("GLFW: Vulkan Not Supported");
        return 1;
    }

    VulkanInterface interface{};
    uint32_t extensions_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensions_count);
    interface.SetupVulkan(extensions, extensions_count, debug);

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err = glfwCreateWindowSurface(interface.g_Instance, window, interface.g_Allocator, &surface);
    check_vk_result(err);

    // Create Framebuffers
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &interface.g_MainWindowData;
    interface.SetupVulkanWindow(wd, surface, w, h, vsync);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls


    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = interface.makeInfo();
    ImGui_ImplVulkan_Init(&init_info, wd->RenderPass);

    /* Setup textures */
    int image_size = width * height * 4;
    char* init_data = reinterpret_cast<char*>(malloc(image_size));
    memset(init_data, 0, image_size);

    constexpr int NUM_TEXTURES = 10;
    int current_texture = NUM_TEXTURES;
    TextureData frame_textures [NUM_TEXTURES];

    for (int i = 0; i < NUM_TEXTURES; i++) {
        interface.LoadTextureFromData(&frame_textures[i], init_data, width, height);
    }

    free(init_data);

    {
        VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;
        VkCommandBuffer command_buffer = wd->Frames[wd->FrameIndex].CommandBuffer;

        err = vkResetCommandPool(interface.g_Device, command_pool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(command_buffer, &begin_info);
        check_vk_result(err);

        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;
        err = vkEndCommandBuffer(command_buffer);
        check_vk_result(err);
        err = vkQueueSubmit(interface.g_Queue, 1, &end_info, VK_NULL_HANDLE);
        check_vk_result(err);

        err = vkDeviceWaitIdle(interface.g_Device);
        check_vk_result(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    interface.g_SwapChainRebuild = true;

    ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.00f);
    wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
    wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
    wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
    wd->ClearValue.color.float32[3] = clear_color.w;

    int show_debug = 0;
    double total_time = 0;
    double elapsed_time;
    unsigned long slow_frames = 0;

    auto frame_interval = std::chrono::nanoseconds((int)frametime_ns);
    auto next_frame_update = std::chrono::steady_clock::now();
    auto prev_render = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        auto current_time = std::chrono::steady_clock::now();

        bool update_frame = current_time >= next_frame_update;
        if (update_frame)
        {
            frame_t frame;
            if (decoder->pop(frame)) {
                current_texture = (current_texture + 1) % NUM_TEXTURES;
                interface.UpdateTexture(&frame_textures[current_texture], frame.data, image_size);
                decoder->return_frame(frame);
            } else {
                slow_frames ++;
            }

            qlen_graph.add(total_time, decoder->get_queue_size());
            next_frame_update = current_time + frame_interval;
        }


        if (vsync || update_frame) {
            if (interface.g_SwapChainRebuild) {
                int w, h;
                glfwGetFramebufferSize(window, &w, &h);
                if (width > 0 && height > 0){
                    ImGui_ImplVulkan_SetMinImageCount(interface.g_MinImageCount);
                    ImGui_ImplVulkanH_CreateOrResizeWindow(interface.g_Instance, interface.g_PhysicalDevice, interface.g_Device, &interface.g_MainWindowData, interface.g_QueueFamily, interface.g_Allocator, w, h, interface.g_MinImageCount);
                    interface.g_MainWindowData.FrameIndex = 0;
                    interface.g_SwapChainRebuild = false;
                }
            }


            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (ImGui::IsKeyPressed(ImGuiKey_A)) {
                show_debug = !show_debug;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
                break;
            }


            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::Begin("Window", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
            ImGui::Image((ImTextureID)frame_textures[(current_texture + NUM_TEXTURES / 2) % NUM_TEXTURES].DS, ImVec2(frame_textures[0].Width, frame_textures[0].Height));
            ImGui::End();
            ImGui::PopStyleVar(1);

            if (show_debug) {
                ImGui::SetNextWindowPos(ImVec2(10, 10));
                ImVec2 dims = ImVec2((float) width / 3, (float) height / 3);
                float nwidth = dims.x - 10;
                float nheight = dims.y - 50;

                ImGui::SetNextWindowSize(dims);
                ImGui::Begin("Config", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

                ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
                ImGui::Text("Slow Frames: %ld", slow_frames);
                ft_graph.draw("frame_decode_time (s)", nwidth, nheight / 3, total_time);
                fps_graph.draw("frame_update_rate (fps)", nwidth, nheight / 3, total_time);
                qlen_graph.draw("frame_queue_size (frames)", nwidth, nheight / 3, total_time);

                ImGui::End();

                nwidth = (float)width / 4;
                nheight = (float) height / 3 * 2;
                ImGui::SetNextWindowPos({ width - nwidth - 25, 0 });
                ImGui::SetNextWindowSize({ nwidth + 25, nheight + 25});

                ImGui::Begin("History", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

                msg_hist.draw("Messages", nwidth, nheight / 2);
                clip_hist.draw("Clip History", nwidth, nheight / 2);
                ImGui::End();
            } else {
                ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            }

            ImGui::Render();

            ImDrawData* draw_data = ImGui::GetDrawData();
            interface.FrameRender(wd, draw_data);
            interface.FramePresent(wd);

            elapsed_time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(current_time - prev_render).count() / 1000;
            total_time += elapsed_time;
            fps_graph.add(total_time, 1.0 / elapsed_time);

            prev_render = current_time;
        }
    }

    err = vkDeviceWaitIdle(interface.g_Device);
    check_vk_result(err);
    for (int i = 0; i < NUM_TEXTURES; i++) {
        interface.RemoveTexture(&frame_textures[i]);
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    interface.CleanupVulkanWindow();
    interface.CleanupVulkan();

    glfwDestroyWindow(window);
    glfwTerminate();


    decoder->stop();

    decoder_thread.join();

    frame_t frame;
    while (decoder->pop(frame)) {
        frame_free(frame);
    }


    return 0;
}


int main(int argc, char **argv)
{
    argparse::ArgumentParser program("rrvp");

    program.add_argument("-m", "--movie")
        .help("Path to the movie file.")
        .default_value("./vid/vid.mp4");

    program.add_argument("-s", "--spec")
        .help("Path to the state machine file.")
        .default_value("./vid/spec.txt");

    program.add_argument("--vsync")
        .help("Enable/disable VSync.")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("--debug")
        .help("Run with more debug information.")
        .default_value(false)
        .implicit_value(true);


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
    bool vsync = program["--vsync"] == true;
    bool debug = program["--debug"] == true;

    std::filesystem::path current_exe = std::string(argv[0]);
    auto log = current_exe.parent_path();
    log /= "log/rrvp.log";


    init_logger(log.string());
    spdlog::info("RRVP started running: {} {} {}", movie.c_str(), spec.c_str(), 0);

    Base_SM* state_machine = nullptr;
    if (spec.ends_with(".json")) {
        state_machine = new BigBloom(spec);
    } else {
        state_machine = new Bird(spec);
    }
    gst_init (&argc, &argv);
    main_player(movie.c_str(), 0, state_machine, vsync, debug);
    return 0;
}
