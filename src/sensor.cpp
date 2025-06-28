#include "sensor.h"

#include <iostream>
#include <chrono>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

int SensorMan::thread_loop(){

    // open resources
    int shm_fd = shm_open("/bearPlayerSensor",  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) {
        std::cerr << "Error opening shared memory." << std::endl;
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(PythonInputData)) == -1) {
	std::cerr << "Failed to set the memory size." << std::endl;
	return 1;
    }

    void* addr = mmap(NULL, sizeof(PythonInputData), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        std::cerr << "Error mapping shared memory." << std::endl;
        return 1;
    }


    // sensor check loop
    while (run) {
	try {
	    memcpy(&data, addr, sizeof(PythonInputData));
	    std::this_thread::sleep_for(std::chrono::seconds(1));
	} catch (std::exception& e) {
	    std::cerr << e.what() << std::endl;
	    break;
	}
    }



    // Cleanup
    if (munmap(addr, 2) == -1) {
        std::cerr << "Error unmapping shared memory" << std::endl;
        return 1;
    }

    if (close(shm_fd) == -1) {
        std::cerr << "Error closing shared memory" << std::endl;
        return 1;
    }


    return 0;
}

void SensorMan::thread_proc(){
    while (run) {
	thread_loop();

	if (run) {
	    std::this_thread::sleep_for(std::chrono::seconds(1));
	}
    }
}

void SensorMan::start() {
    run = true;
    worker.reset(new std::thread(&SensorMan::thread_proc, this));
}

SensorMan::SensorMan() {}

SensorMan::~SensorMan() {
    run = false;
    if (worker) {
	worker->join();
	worker.reset();
    }
}
