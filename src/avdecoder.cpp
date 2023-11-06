#include "avdecoder.h"

#include <chrono>
#include <libavcodec/codec_id.h>
#include <libavutil/dict.h>
#include <thread>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#define INBUF_SIZE 4096

AVDecoder::AVDecoder(std::string movie, int flip_method, clip_t **isequences,
                     addr_t i_start_address, size_t q_size,
                     decdata_f i_submit_data) {
  width = 1080;
  height = 1920;
  running = false;
  format = "";
  filename = movie;

  sequences = isequences;
  submit_data = i_submit_data;
  start_address = i_start_address;

  qmax = q_size;
  frames = moodycamel::BlockingReaderWriterQueue<frame_t>(qmax);
}

AVDecoder::~AVDecoder() {}

void AVDecoder::play() {
  const AVCodec *codec;
  AVCodecParserContext *parser;
  AVCodecContext *c = NULL;
  FILE *f;
  AVFrame *frame;
  uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
  uint8_t *data;
  size_t data_size;
  int ret;
  int eof;
  AVPacket *pkt;

  pkt = av_packet_alloc();
  if (!pkt) {
    fprintf(stderr, "Could not allocate packet");
  }

  /* set end of buffer to 0 (this ensures that no overreading happens for
   * damaged MPEG streams) */
  memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

  codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
  if (!codec) {
    fprintf(stderr, "Codec not found\n");
    return;
  }

  parser = av_parser_init(codec->id);
  if (!parser) {
    fprintf(stderr, "parser not found\n");
    return;
  }

  c = avcodec_alloc_context3(codec);
  c->width = 1080;
  c->height = 1920;

  if (!c) {
    fprintf(stderr, "Could not allocate video codec context\n");
    return;
  }

  if (avcodec_open2(c, codec, NULL) < 0) {
    fprintf(stderr, "Could not open codec\n");
    return;
  }

  f = fopen(filename.c_str(), "rb");
  if (!f) {
    fprintf(stderr, "Could not open %s\n", filename.c_str());
    return;
  }

  frame = av_frame_alloc();
  if (!frame) {
    fprintf(stderr, "Could not allocate video frame\n");
    return;
  }

  printf("Playing videos\n");
  do {
    /* read raw data from the input file */
    data_size = fread(inbuf, 1, INBUF_SIZE, f);
    if (ferror(f))
      break;
    eof = !data_size;

    /* use the parser to split the data into frames */
    data = inbuf;
    while (data_size > 0 || eof) {
      ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size, data, data_size,
                             AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (ret < 0) {
        fprintf(stderr, "Error while parsing\n");
        return;
      }
      data += ret;
      data_size -= ret;

      if (pkt->size)
        decode(c, frame, pkt);
      else if (eof)
        break;
    }
  } while (!eof);

  printf("Playing videos no more\n");
  fclose(f);

  av_parser_close(parser);
  avcodec_free_context(&c);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  printf("Cleanup of playback complete\n");
}

template<typename... Args> int logging(const char * f, Args... args) {
  printf(f, args...);
  return printf("\n");
}
void AVDecoder::play2() {

    AVFormatContext *pFormatContext = avformat_alloc_context();

    if (!pFormatContext) {
        logging("ERROR could not allocate memory for Format Context");
        return;
    }

    logging("opening the input file (%s) and loading format (container) header",
            filename.c_str());

    if (avformat_open_input(&pFormatContext, filename.c_str(), NULL, NULL) != 0) {
        logging("ERROR could not open the file");
        return;
    }

    logging("format %s, duration %lld us, bit_rate %lld",
            pFormatContext->iformat->name, pFormatContext->duration,
            pFormatContext->bit_rate);

    logging("finding stream info from format");

    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
        logging("ERROR could not get the stream info");
        return;
    }

    const AVCodec *pCodec;
    AVCodecParameters *pCodecParameters = NULL;
    int video_stream_index = -1;

    // loop though all the streams and print its main information
    for (int i = 0; i < pFormatContext->nb_streams; i++) {
        AVCodecParameters *pLocalCodecParameters = NULL;
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        logging("AVStream->time_base before open coded %d/%d",
                pFormatContext->streams[i]->time_base.num,
                pFormatContext->streams[i]->time_base.den);
        logging("AVStream->r_frame_rate before open coded %d/%d",
                pFormatContext->streams[i]->r_frame_rate.num,
                pFormatContext->streams[i]->r_frame_rate.den);
        logging("AVStream->start_time %" PRId64,
                pFormatContext->streams[i]->start_time);
        logging("AVStream->duration %" PRId64,
                pFormatContext->streams[i]->duration);

        logging("finding the proper decoder (CODEC)");

        const AVCodec *pLocalCodec;

        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == NULL) {
            logging("ERROR unsupported codec!");
            // In this example if the codec is not found we just skip it
            continue;
        }

        // when the stream is a video we store its index, codec parameters and codec
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream_index == -1) {
                video_stream_index = i;
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
            }

            logging("Video Codec: resolution %d x %d", pLocalCodecParameters->width,
                    pLocalCodecParameters->height);
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            logging("Audio Codec: %d channels, sample rate %d",
                    pLocalCodecParameters->channels,
                    pLocalCodecParameters->sample_rate);
        }

        // print its name, id and bitrate
        logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name,
                pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    if (video_stream_index == -1) {
        logging("File %s does not contain a video stream!", filename.c_str());
        return;
    }

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext) {
        logging("failed to allocated memory for AVCodecContext");
        return;
    }

    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) {
        logging("failed to copy codec params to codec context");
        return;
    }

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
        logging("failed to open codec through avcodec_open2");
        return;
    }

    AVFrame *pFrame = av_frame_alloc();
    if (!pFrame) {
        logging("failed to allocate memory for AVFrame");
        return;
    }

    AVPacket *pPacket = av_packet_alloc();
    if (!pPacket) {
        logging("failed to allocate memory for AVPacket");
        return;
    }


    running = true;
    while (running) {
        if(av_read_frame(pFormatContext, pPacket) < 0) {
              logging("av_read_frame failure");
              break;
        }
        
        if (avcodec_send_packet(pCodecContext, pPacket)) {
            logging("avcodec_send_packet failure!");
        }
        
        decode(pCodecContext, pFrame, pPacket);
    }

    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);
}

static AVFrame *frame_as_format(const AVFrame *src, AVPixelFormat format) {
  int width = src->width;
  int height = src->height;

  AVFrame *dst = av_frame_alloc();

  dst->format = format;
  dst->width = src->width;
  dst->height = src->height;

  av_frame_get_buffer(dst, 0);

  SwsContext *conversion =
      sws_getContext(width, height, AV_PIX_FMT_YUV420P, width, height, format,
                     SWS_FAST_BILINEAR, NULL, NULL, NULL);

  sws_scale(conversion, src->data, src->linesize, 0, height, dst->data,
            dst->linesize);
  sws_freeContext(conversion);
  return dst;
}

void AVDecoder::decode(AVCodecContext *dec_ctx, AVFrame *oframe,
                       AVPacket *pkt) {
  int ret;
  ret = avcodec_send_packet(dec_ctx, pkt);
  if (ret < 0) {
    fprintf(stderr, "Error sending a packet for decoding\n");
    return;
  }

  while (ret >= 0) {
    ret = avcodec_receive_frame(dec_ctx, oframe);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      return;
    else if (ret < 0) {
      fprintf(stderr, "Error during decoding\n");
      return;
    }

    AVFrame *frame = frame_as_format(oframe, AV_PIX_FMT_RGBA);
    frame_t f;

    // while (frames.size_approx() > 10) {
    //   std::this_thread::sleep_for(std::chrono::milliseconds(3));
    // }

    f.data = reinterpret_cast<uint8_t *>(
        malloc(frame->width * frame->height * 4 * sizeof(uint8_t)));
    memcpy(f.data, frame->data[0],
           frame->width * frame->height * 4 * sizeof(uint8_t));
    frames.enqueue(f);
    av_frame_free(&frame);
  }
}

void AVDecoder::stop() { running = false; }

bool AVDecoder::pop(frame_t &frame) { return frames.try_dequeue(frame); }
