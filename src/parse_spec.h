#ifndef PARSE_SPEC_H_
#define PARSE_SPEC_H_
typedef int (* addr_t)[2];
typedef struct clip clip_t;
struct clip {
    int start;
    int end;
    int address[2];
    int njumps;
    int addrsize;
    int* addresses;
};


clip_t get_clip(clip_t** sequences, int (*address)[2]);
clip_t** parse_spec(char* file_name, int (*start) [2]);
#endif
