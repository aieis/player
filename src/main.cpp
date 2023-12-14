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

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include "imgui_impl_glfw.h"
#include "vulkan_interop.h"


#include "implot.h"
#include "argparse.hpp"

#include <GLFW/glfw3.h>

#include "parse_spec.h"
#include "graph.h"
#include "list.h"

#include "gstdecoder.h"
//#include "avdecoder.h"

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

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

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    printf ("video resolution is %dx%d\n", width, height);

    GLFWwindow *window = glfwCreateWindow (width, height, "RRVP - Rapid Response Video Player", NULL, NULL);
    if (window == NULL)
        return 1;

    if (!glfwVulkanSupported())
    {
        printf("GLFW: Vulkan Not Supported\n");
        return 1;
    }

    VulkanInterface interface{};
    uint32_t extensions_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensions_count);
    interface.SetupVulkan(extensions, extensions_count);

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err = glfwCreateWindowSurface(interface.g_Instance, window, interface.g_Allocator, &surface);
    check_vk_result(err);

    // Create Framebuffers
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &interface.g_MainWindowData;
    interface.SetupVulkanWindow(wd, surface, w, h);

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

    int image_size = width * height * 4;
    TextureData my_texture;
    char* init_data = reinterpret_cast<char*>(malloc(image_size));
    memset(init_data, 0, image_size);
    bool ret = interface.LoadTextureFromData(&my_texture, init_data, width, height);
    free(init_data);
    
    IM_ASSERT(ret);
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

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    
    int show_debug = 0;
    double total_time = 0;
    auto t1 = std::chrono::steady_clock::now();
    auto t2 = t1;
    double elapsed_time;
    auto end = t1 + std::chrono::milliseconds(33);

    bool swapready = false;
    while (!glfwWindowShouldClose(window)) {

        frame_t frame;
        t2 = std::chrono::steady_clock::now();
        if (t2 >= end && swapready)
        {
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            if (width > 0 && height > 0)
            {
                ImGui_ImplVulkan_SetMinImageCount(interface.g_MinImageCount);
                ImGui_ImplVulkanH_CreateOrResizeWindow(interface.g_Instance, interface.g_PhysicalDevice, interface.g_Device, &interface.g_MainWindowData, interface.g_QueueFamily, interface.g_Allocator, w, h, interface.g_MinImageCount);
                interface.g_MainWindowData.FrameIndex = 0;
                interface.g_SwapChainRebuild = false;
            }

            swapready = false;
            elapsed_time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() / 1000;
            t1 = t2;
            end = t1 + std::chrono::milliseconds((int)frametime);

            total_time += elapsed_time;
            fps_graph.add(total_time, 1.0 / elapsed_time);

        }


        if (!swapready && decoder->pop(frame)) {
            glfwPollEvents();
            if (ImGui::IsKeyPressed(ImGuiKey_A)) {
                show_debug = !show_debug;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
                break;
            }
            
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            interface.UpdateTexture(&my_texture, frame.data, image_size);
            frame_free(frame);
            
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::Begin("Window", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
            ImGui::Image((ImTextureID)my_texture.DS, ImVec2(my_texture.Width, my_texture.Height));
            ImGui::End();
            ImGui::PopStyleVar(1);
            
            swapready = true;

            if (show_debug) {
                ImGui::SetNextWindowPos(ImVec2(10, 10));
                ImVec2 dims = ImVec2(ImGui::GetIO().DisplaySize / 3);
                float nwidth = dims.x - 10;
                float nheight = dims.y - 10;

                ImGui::SetNextWindowSize(dims);
                ImGui::Begin("Config", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

                    
                
                ft_graph.draw("frame_decode_time (s)", nwidth, nheight / 3);
                fps_graph.draw("FPS", nwidth, nheight / 3);
                qlen_graph.draw("frame_queue_size", nwidth, nheight / 3);

                ImGui::End();

                nwidth = (float)width / 4;
                nheight = (float) height / 3 * 2;
                ImGui::SetNextWindowPos({ width - nwidth - 5, 0 });
                ImGui::SetNextWindowSize({ nwidth + 5, nheight + 5});

                ImGui::Begin("History", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
                
                msg_hist.draw("Messages", nwidth, nheight / 2);
                clip_hist.draw("Clip History", nwidth, nheight / 2);
                ImGui::End();
            }

            ImGui::Render();

            ImDrawData* draw_data = ImGui::GetDrawData();
            wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            wd->ClearValue.color.float32[3] = clear_color.w;

            interface.FrameRender(wd, draw_data);
            interface.FramePresent(wd);                    
        } else if (!swapready) {
            msg_hist.add("Failed to pop frame!");
        }        
    }

    err = vkDeviceWaitIdle(interface.g_Device);
    check_vk_result(err);
    interface.RemoveTexture(&my_texture);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    interface.CleanupVulkanWindow();
    interface.CleanupVulkan();
    
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
