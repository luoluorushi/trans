#include "ITranscoder.h"
#include <string>
typedef struct FilteringContext {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
} FilteringContext;

typedef struct StreamContext {
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;
} StreamContext;

class TranscodeHelper
{
public:
    static int open_input_file(const char* filename, AVFormatContext *&ifmt_ctx, StreamContext *&stream_ctx);
    static int open_output_file(const char *filename,VideoTransParams video_params, AudioTransParams audio_params
                ,AVFormatContext *ifmt_ctx, AVFormatContext *&ofmt_ctx, StreamContext *& stream_ctx, bool just_video);
    static int init_filters(AVFormatContext *ifmt_ctx, StreamContext *stream_ctx, VideoTransParams video_params
                ,AudioTransParams audio_params, FilteringContext *&filter_ctx, bool just_video);
    static int filter_encode_write_frame(AVFrame *frame, StreamContext *stream_ctx, unsigned int stream_index
                                         ,AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, FilteringContext *filter_cxt);
    static int filter_encode_write_frame2(AVFrame *frame, StreamContext *stream_ctx, unsigned int stream_index
                                         ,AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, FilteringContext *filter_cxt);
    static int flush_encoder(StreamContext *stream_ctx, unsigned int stream_index, AVFormatContext *ifmt_ctx
                             , AVFormatContext *ofmt_ctx);
    static int encode_write_frame(AVFrame *filt_frame, StreamContext *stream_ctx, unsigned int stream_index
                                  , AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, int *got_frame);

private:
    static int init_filter(FilteringContext *filter_ctx, AVCodecContext *dec_ctx,
                            AVCodecContext *enc_ctx, const char *filter_spec);

};

