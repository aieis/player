#ifndef _SM_H_
#define _SM_H_
#include <string>

#include "parse_spec.h"
#include "bb_sm.h"
#include "sensor.h"

struct Clip {
    int start;
    int end;
    std::string name;
};

class Base_SM {
public:
    virtual Clip next() = 0;
    virtual Clip current() = 0;
    virtual Clip seek(const std::string& clip_name) = 0;
};



class Bird : public Base_SM {

    clip_t curr;
    int max_clips = 0;
    clip_t** clips;

public:
    Bird(std::string file_path);
    ~Bird();
    Clip next();
    Clip current();
    Clip seek(const std::string& clip_name);
};


class BigBloom : public Base_SM {

    StateMachine sm;
    SensorMan sensor_manager;

public:
    BigBloom(std::string file_path);
    ~BigBloom();
    Clip next();
    Clip current();
    Clip seek(const std::string& clip_name);
};

#endif // _SM_H
