#include <functional>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "readerwriterqueue.h"

#include "decoder.h"
#include "parse_spec.h"

class AVDecoder {
 private:

    std::string filename;
    
    decdata_f submit_data;
    
    clip_t** sequences;
    addr_t start_address;
    int width;
    int height;
    double framerate;
    std::string format;

    bool running;

    size_t qmax;
    moodycamel::BlockingReaderWriterQueue<frame_t> frames;
    void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt);
    
 public:

    AVDecoder(std::string movie, int flip_mehtod, clip_t** clips, addr_t start_address, size_t q_size, decdata_f submit_data);
    ~AVDecoder();
    
    void init() {};

    void play();
    void play2();
    
    void stop();
        
    int get_width() {return width;};
    int get_height() {return height;};

    bool pop(frame_t &frame);
};
