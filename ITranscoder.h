#pragma once
struct VideoTransParams {
    int codec_id;
    int width;
    int height;
    int frame_per_second;
    int bit_per_second;
    int start_ms;
    int end_ms;
};

struct AudioTransParams {
    int codec_id;
    int bit_per_second;
};

enum TranscodeError
{
    kOk,
    kOpenInputFileFailed,
    kOpenOutputFileFailed,
    kInitFilterFailed,
    kEncodeFailed,
    kMuxFailed,
};

struct ITranscoderCallback
{
    virtual void OnProgress(int percent) = 0;
    virtual void OnTransResult(bool is_success, TranscodeError err_code) = 0;
};

struct ITranscoder
{
    virtual void InitWithTransParams(const char* in_filename, const char* out_filename
                    , ITranscoderCallback *callback,VideoTransParams* video_params, AudioTransParams* audio_params)=0;
    virtual void InitWithMergeParams(const char** in_filename,const int file_count,const char* out_filename
                    ,VideoTransParams* video_params, AudioTransParams* audio_params)=0;
    virtual int StartTranscode()=0;
    virtual int StopTranscode()=0;
    virtual int PauseTranscode()=0;
    virtual int ResumeTranscode()=0;
};

enum EditorOpType
{
    kOpTypeTrans,
    kOpTypeMerge
};
