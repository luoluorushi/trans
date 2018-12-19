// transcoding.cpp
//
// Created by luoluorushi on 2018/09/06.
// Copyright © 2018年 luoluorushi. All right reserved.


extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include "ITranscoder.h"
#include "transcode_helper.h"
#include <string>
#include <thread>
#include <chrono>
#include "cross_platform/ConditionEvent.h"



class VideoTranscoder: public ITranscoder
{
public:
    virtual void InitWithTransParams(const char* in_filename, const char* out_filename
                    ,ITranscoderCallback *callback, VideoTransParams* params, AudioTransParams* audio_params);
    virtual void InitWithMergeParams(const char** in_filename, const int file_count,const char* out_filename
                    ,VideoTransParams* params, AudioTransParams* audio_params);
    virtual int StartTranscode();
    virtual int StopTranscode();
    virtual int PauseTranscode();
    virtual int ResumeTranscode();
private:
    int TranscodeThreadProc();
    int MergeThreadProc();

private:
    VideoTransParams video_params_ = {0};
    AudioTransParams audio_params_ = {0};
    const char* in_filename_;
    const char* out_filename_;
    ITranscoderCallback *callback_ = nullptr;

    std::shared_ptr<CConditionEvent> cv_stop;
    std::shared_ptr<CConditionEvent> cv_pause;
    std::shared_ptr<CConditionEvent> cv_resume;

    EditorOpType op_type_;

    std::vector<const char*> file_list_;
    bool just_video_;
};


/*-------------   interface    -------------*/
void VideoTranscoder::InitWithTransParams(const char* in_filename, const char* out_filename
        ,ITranscoderCallback *callback, VideoTransParams* params, AudioTransParams* audio_params)
{
    op_type_ = kOpTypeTrans;
    in_filename_ = in_filename;
    out_filename_ = out_filename;
    if(params == nullptr)
        return;
    video_params_.codec_id = params->codec_id;
    video_params_.width = params->width;
    video_params_.height = params->height;
    video_params_.frame_per_second = params->frame_per_second;
    video_params_.bit_per_second = params->bit_per_second;
    video_params_.start_ms = params->start_ms;
    video_params_.end_ms = params->end_ms;
    audio_params_.codec_id = audio_params->codec_id;
    audio_params_.bit_per_second = audio_params->bit_per_second;
    cv_stop = std::make_shared<CConditionEvent>();
    cv_pause = std::make_shared<CConditionEvent>();
    cv_resume = std::make_shared<CConditionEvent>();
    std::string file = out_filename;
    int index = file.rfind(".gif");
    if( index != -1 && index == file.size() - 4)
    {
        just_video_ = true;
    }
    just_video_ = true;
    callback_ = callback;
}

void VideoTranscoder::InitWithMergeParams(const char** in_filename_list, const int file_count, const char* out_filename
                        ,VideoTransParams* params, AudioTransParams* audio_params)
{
    op_type_ = kOpTypeMerge;
    for(int i = 0; i< file_count; ++i)
    {
        file_list_.push_back(in_filename_list[i]);
    }
    out_filename_ = out_filename;
    if(params == nullptr)
        return;
    video_params_.codec_id = params->codec_id;
    video_params_.width = params->width;
    video_params_.height = params->height;
    video_params_.frame_per_second = params->frame_per_second;
    video_params_.bit_per_second = params->bit_per_second;
    video_params_.start_ms = params->start_ms;
    video_params_.end_ms = params->end_ms;
    audio_params_.codec_id = audio_params->codec_id;
    audio_params_.bit_per_second = audio_params->bit_per_second;
    cv_stop = std::make_shared<CConditionEvent>();
    cv_pause = std::make_shared<CConditionEvent>();
    cv_resume = std::make_shared<CConditionEvent>();
}

int VideoTranscoder::StartTranscode()
{
    if(op_type_ == kOpTypeTrans)
    {
        std::thread([this]
                {
                    printf("\nTrans thread start\n");
                    TranscodeThreadProc();
                }).detach();
    }
    else if(op_type_ == kOpTypeMerge)
    {
        std::thread([this]
                {
                    printf("\nMerge thread start\n");
                    MergeThreadProc();
                }).detach();
    }
    return 0;
}

int VideoTranscoder::StopTranscode()
{
    cv_stop->SetEvent();
    return 0;
}

int VideoTranscoder::PauseTranscode()
{
    cv_resume->ResetEvent();
    cv_pause->SetEvent();
    return 0;
}

int VideoTranscoder::ResumeTranscode()
{
    cv_pause->ResetEvent();
    cv_resume->SetEvent();
    return 0;
}

typedef struct AVFrameCache
{
    AVFrame* frame;
    int stream_index;
}AVFrameCache;

int VideoTranscoder::MergeThreadProc()
{
    int ret;
    AVPacket packet = { .data = NULL, .size = 0 };
    AVFrame *frame = NULL;
    enum AVMediaType type;
    unsigned int stream_index;
    unsigned int i;
    int got_frame;
    int count=0;
    int64_t last_pts_us=0;
    int64_t file_start_us=0;
    int (*dec_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
    AVFormatContext *ifmt_ctx=nullptr, *ofmt_ctx=nullptr;
    StreamContext *stream_ctx=nullptr;
    FilteringContext *filter_ctx = nullptr;

    if ((ret = TranscodeHelper::open_input_file(file_list_[0], ifmt_ctx, stream_ctx) < 0))
        goto end;
    if ((ret = TranscodeHelper::open_output_file(out_filename_, video_params_, audio_params_
                                    , ifmt_ctx,  ofmt_ctx, stream_ctx, just_video_)) < 0)
        goto end;
    if ((ret = TranscodeHelper::init_filters(ifmt_ctx, stream_ctx, video_params_, audio_params_
                                    , filter_ctx, just_video_)) < 0)
        goto end;
    for(int i=0; i < file_list_.size(); ++i)
    {
        if(i > 0)
        {
            if ((ret = TranscodeHelper::open_input_file(file_list_[i],ifmt_ctx, stream_ctx) < 0))
                goto end;
            if ((ret = TranscodeHelper::init_filters(ifmt_ctx, stream_ctx, video_params_
                                            , audio_params_, filter_ctx, just_video_)) < 0)
                goto end;
        }
        file_start_us += last_pts_us;
        /* read all packets */
        while (1) {
            if(WaitForSingleEvent(cv_stop,0) == WAIT_OBJECT_0)
                break;
            if(WaitForSingleEvent(cv_pause, 0) == WAIT_OBJECT_0)
            {
                std::shared_ptr<CConditionEvent> events[2];
                events[0] = cv_stop;
                events[1] = cv_resume;
                int wait_result = WaitForMultipleEvent(events, 2);
                if(wait_result == 0)
                    break;
            }
            if ((ret = av_read_frame(ifmt_ctx, &packet)) < 0)
                break;
            av_dup_packet(&packet);

            stream_index = packet.stream_index;
            type = ifmt_ctx->streams[packet.stream_index]->codecpar->codec_type;
            av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n",
                   stream_index);

            if (filter_ctx[stream_index].filter_graph) {
                av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");
                frame = av_frame_alloc();
                if (!frame) {
                    ret = AVERROR(ENOMEM);
                    break;
                }
                av_packet_rescale_ts(&packet,
                                     ifmt_ctx->streams[stream_index]->time_base,
                                     stream_ctx[stream_index].dec_ctx->time_base);
                dec_func = (type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 :
                    avcodec_decode_audio4;
                ret = dec_func(stream_ctx[stream_index].dec_ctx, frame,
                        &got_frame, &packet);
                if (ret < 0) {
                    av_frame_free(&frame);
                    av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                    break;
                }
                if (got_frame) {
                    frame->pts = frame->best_effort_timestamp;
                    if(frame->pts >=0)
                    {
                        last_pts_us = frame->pts*1000000*av_q2d(stream_ctx[stream_index].dec_ctx->time_base);
                        int64_t file_start_pts = file_start_us/av_q2d(stream_ctx[stream_index].dec_ctx->time_base)/1000000;
                        frame->pts = frame->pts + file_start_pts;
                        ret = TranscodeHelper::filter_encode_write_frame(frame, stream_ctx,stream_index,ifmt_ctx,ofmt_ctx,filter_ctx);
                    }
                    av_frame_free(&frame);
                    if (ret < 0)
                        goto end;
                } else {
                    av_frame_free(&frame);
                }
            } else {
                /* remux this frame without reencoding */
                av_packet_rescale_ts(&packet,
                                     ifmt_ctx->streams[stream_index]->time_base,
                                     ofmt_ctx->streams[stream_index]->time_base);

                ret = av_interleaved_write_frame(ofmt_ctx, &packet);
                if (ret < 0)
                    goto end;
            }
            av_packet_unref(&packet);
        }
    }

    /* flush filters and encoders */
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        /* flush filter */
        if (!filter_ctx[i].filter_graph)
            continue;
        ret = TranscodeHelper::filter_encode_write_frame(NULL, stream_ctx,i,ifmt_ctx,ofmt_ctx,filter_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }

        /* flush encoder */
        ret = TranscodeHelper::flush_encoder(stream_ctx, i, ifmt_ctx, ofmt_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    av_write_trailer(ofmt_ctx);
end:
    av_packet_unref(&packet);
    av_frame_free(&frame);
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        avcodec_free_context(&stream_ctx[i].dec_ctx);
        if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && stream_ctx[i].enc_ctx)
            avcodec_free_context(&stream_ctx[i].enc_ctx);
        if (filter_ctx && filter_ctx[i].filter_graph)
            avfilter_graph_free(&filter_ctx[i].filter_graph);
    }
    av_free(filter_ctx);
    av_free(stream_ctx);
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

    return ret ? 1 : 0;


    return 0;
}
int VideoTranscoder::TranscodeThreadProc()
{
    int ret;
    AVPacket packet = { .data = NULL, .size = 0 };
    AVFrame *frame = NULL;
    enum AVMediaType type;
    unsigned int stream_index;
    unsigned int i;
    int got_frame;
    int count=0;
    int (*dec_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);

    bool found_first_frame = false;
    int64_t seek_time=0;
    int64_t start_pts = 0;
    std::vector<AVFrameCache> cache_frame_list;
    std::vector<int64_t> start_pts_vec;
    std::vector<bool> pts_exceed_end_time;

    AVFormatContext *ifmt_ctx=nullptr, *ofmt_ctx=nullptr;
    StreamContext *stream_ctx = nullptr;
    FilteringContext *filter_ctx = nullptr;
    if ((ret = TranscodeHelper::open_input_file(in_filename_, ifmt_ctx, stream_ctx)) < 0)
    {
        if(callback_)
        {
            callback_->OnTransResult(false, kOpenInputFileFailed);
        }
        goto end;
    }

    if ((ret = TranscodeHelper::open_output_file(out_filename_, video_params_, audio_params_
                                    ,ifmt_ctx, ofmt_ctx, stream_ctx, true)) < 0)
    {
        if(callback_)
        {
            callback_->OnTransResult(false, kOpenOutputFileFailed);
        }
        goto end;
    }
    if ((ret = TranscodeHelper::init_filters(ifmt_ctx, stream_ctx, video_params_
                                    ,audio_params_ , filter_ctx, just_video_)) < 0)
    {
        if(callback_)
        {
            callback_->OnTransResult(false, kInitFilterFailed);
        }
        goto end;
    }

    seek_time = video_params_.start_ms/av_q2d(ifmt_ctx->streams[0]->time_base)/1000;
    ret = av_seek_frame(ifmt_ctx, 0, seek_time, AVSEEK_FLAG_BACKWARD);
    start_pts = video_params_.start_ms/1000/av_q2d(stream_ctx[0].dec_ctx->time_base);
    pts_exceed_end_time.insert(pts_exceed_end_time.begin(), ifmt_ctx->nb_streams, false);
    start_pts_vec.insert(start_pts_vec.begin(), ifmt_ctx->nb_streams, 0);

    /* read all packets */
    while (1) {
        if(WaitForSingleEvent(cv_stop,0) == WAIT_OBJECT_0)
            break;
        if(WaitForSingleEvent(cv_pause, 0) == WAIT_OBJECT_0)
        {
            std::shared_ptr<CConditionEvent> events[2];
            events[0] = cv_stop;
            events[1] = cv_resume;
            int wait_result = WaitForMultipleEvent(events, 2);
            if(wait_result == 0)
                break;
        }
        if ((ret = av_read_frame(ifmt_ctx, &packet)) < 0)
            break;
        av_dup_packet(&packet);

        stream_index = packet.stream_index;
        type = ifmt_ctx->streams[packet.stream_index]->codecpar->codec_type;
        if(just_video_ && type != AVMEDIA_TYPE_VIDEO)
        {
            continue;
        }
        av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n",
               stream_index);

        if (filter_ctx[stream_index].filter_graph) {
            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");
            frame = av_frame_alloc();
            if (!frame) {
                ret = AVERROR(ENOMEM);
                break;
            }
            av_packet_rescale_ts(&packet,
                                 ifmt_ctx->streams[stream_index]->time_base,
                                 stream_ctx[stream_index].dec_ctx->time_base);
            dec_func = (type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 :
                avcodec_decode_audio4;
            ret = dec_func(stream_ctx[stream_index].dec_ctx, frame,
                    &got_frame, &packet);
            if (ret < 0) { 
                av_frame_free(&frame);
                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                break;
            }
            if (got_frame) {
                frame->pts = frame->best_effort_timestamp;
                if(!found_first_frame)
                {
                    if(stream_index != 0)
                    {
                        AVFrameCache cache;
                        cache.frame = frame;
                        cache.stream_index = stream_index;
                        cache_frame_list.push_back(cache);
                        continue;
                    }
                    else
                    {
                        if(frame->pts < start_pts)
                        {
                            av_frame_free(&frame);
                            continue;
                        }
                        found_first_frame = true;
                        start_pts = frame->pts;
                        start_pts_vec[0] = start_pts;
                        for(int i = 1; i < ifmt_ctx->nb_streams; ++i)
                        {
                            start_pts_vec[i] = av_rescale_q(frame->pts, stream_ctx[0].dec_ctx->time_base
                                                 ,stream_ctx[i].dec_ctx->time_base);
                        }
                        for(std::vector<AVFrameCache>::iterator ite = cache_frame_list.begin(); ite != cache_frame_list.end(); ++ite)
                        {
                            ite->frame->pts -= start_pts_vec[ite->stream_index];
                            if(ite->frame->pts >= 0 )
                            {
                                TranscodeHelper::filter_encode_write_frame(ite->frame,stream_ctx, ite->stream_index,ifmt_ctx,ofmt_ctx,filter_ctx);
                            }
                            av_frame_free(&(ite->frame));
                        }
                    }
                }
                int64_t ts_ms = frame->pts * 1000 * av_q2d(stream_ctx[stream_index].dec_ctx->time_base);
                if(ts_ms > video_params_.end_ms)
                {
                    if(just_video_)
                    {
                        av_frame_free(&frame);
                        break;
                    }
                    pts_exceed_end_time[stream_index] = true;
                    bool all_exceed = true;
                    for(std::vector<bool>::iterator ite = pts_exceed_end_time.begin(); ite != pts_exceed_end_time.end(); ++ite)
                    {
                        if(!*ite)
                        {
                            all_exceed = false;
                            break;
                        }
                    }
                    av_frame_free(&frame);
                    if(all_exceed)
                    {
                        break;
                    }
                    else
                    {
                        continue;
                    }
                }
                frame->pts -= start_pts_vec[stream_index];
                if(type == AVMEDIA_TYPE_VIDEO && callback_)
                {
                    callback_->OnProgress((ts_ms-video_params_.start_ms)*100/(video_params_.end_ms-video_params_.start_ms+1));
                }
                if(frame->pts >= 0)
                {
                    ret = TranscodeHelper::filter_encode_write_frame(frame, stream_ctx,stream_index,ifmt_ctx,ofmt_ctx,filter_ctx);
                }
                av_frame_free(&frame);
                if (ret < 0)
                {
                    if(callback_)
                    {
                        callback_->OnTransResult(false, kEncodeFailed);
                    }
                    goto end;
                }
            } else {
                av_frame_free(&frame);
            }
        } else {
            /* remux this frame without reencoding */
            av_packet_rescale_ts(&packet,
                                 ifmt_ctx->streams[stream_index]->time_base,
                                 ofmt_ctx->streams[stream_index]->time_base);

            ret = av_interleaved_write_frame(ofmt_ctx, &packet);
            if (ret < 0)
            {
                if(callback_)
                {
                    callback_->OnTransResult(false, kMuxFailed);
                }
                goto end;
            }
        }
        av_packet_unref(&packet);
    }

    /* flush filters and encoders */
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        /* flush filter */
        if (!filter_ctx[i].filter_graph)
            continue;
        ret = TranscodeHelper::filter_encode_write_frame(NULL,stream_ctx, i, ifmt_ctx, ofmt_ctx, filter_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }

        ret = TranscodeHelper::filter_encode_write_frame2(NULL,stream_ctx, i, ifmt_ctx, ofmt_ctx, filter_ctx);
        /* flush encoder */
        ret = TranscodeHelper::flush_encoder(stream_ctx, i, ifmt_ctx, ofmt_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    av_write_trailer(ofmt_ctx);
    if(callback_)
    {
        callback_->OnProgress(100);
        callback_->OnTransResult(true, kOk);
    }
end:
    av_packet_unref(&packet);
    av_frame_free(&frame);
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        avcodec_free_context(&stream_ctx[i].dec_ctx);
        if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && stream_ctx[i].enc_ctx)
            avcodec_free_context(&stream_ctx[i].enc_ctx);
        if (filter_ctx && filter_ctx[i].filter_graph)
            avfilter_graph_free(&filter_ctx[i].filter_graph);
    }
    av_free(filter_ctx);
    av_free(stream_ctx);
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0)
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

    return ret ? 1 : 0;

}

class TransCallback : public ITranscoderCallback
{
    virtual void OnProgress(int percent);
    virtual void OnTransResult(bool is_success, TranscodeError err_code);
};

void TransCallback::OnProgress(int percent)
{
    printf("trans percent=%d\%\n",percent);
}

void TransCallback::OnTransResult(bool is_success, TranscodeError err_code)
{
    printf("OnTransResult, success = %d, err_code=%d\n", is_success, err_code);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }
    VideoTransParams params{
        AV_CODEC_ID_MPEG4,320,180,5,320*1000,12*1000,13*1000
    };
    AudioTransParams audio_params {
        AV_CODEC_ID_AAC, 100*1000
    };
    TransCallback callback;
    VideoTranscoder transcoder;
    const char* file_list[2];
    file_list[0] = argv[1];
    file_list[1] = argv[2];
    //transcoder.InitWithMergeParams(file_list,2, argv[3], &params);
    transcoder.InitWithTransParams(argv[1], argv[2], &callback, &params, &audio_params);
    transcoder.StartTranscode();
    std::this_thread::sleep_for(std::chrono::milliseconds(300000));
    return 0;
}
