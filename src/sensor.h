#include <memory>
#include <thread>

struct PythonInputData {
    uint8_t alive = 0;
    uint8_t active = 0;
};


class SensorMan {
public:
    bool run = false;
    PythonInputData data;
    std::unique_ptr<std::thread> worker;

    SensorMan();
    ~SensorMan();

    void start();
    void thread_proc();
    int thread_loop();
};
