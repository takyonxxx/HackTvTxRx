#ifndef AUDIOFILEINPUT_H
#define AUDIOFILEINPUT_H
#pragma once

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <QObject>
#include "stream_tx.h"
#include "types.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

class AudioFileInput : public QObject
{
    Q_OBJECT

public:
    explicit AudioFileInput(dsp::stream_tx<dsp::complex_tx>& stream_tx, QObject *parent = nullptr)
        : QObject(parent), stream_tx(stream_tx), m_running(false),
          m_loop(true), m_formatCtx(nullptr), m_codecCtx(nullptr), m_swrCtx(nullptr),
          m_audioStreamIndex(-1)
    {
    }

    ~AudioFileInput()
    {
        stop();
    }

    bool start(const std::string& filePath, bool loop = true)
    {
        if (m_running.load()) {
            stop();
        }

        m_filePath = filePath;
        m_loop = loop;

        if (!openFile()) {
            fprintf(stderr, "AudioFileInput: failed to open file: %s\n", filePath.c_str());
            fflush(stderr);
            return false;
        }

        stream_tx.setBufferSize(STREAM_BUFFER_SIZE);
        m_running.store(true);

        m_thread = std::thread([this]() {
            decodeLoop();
        });

        fprintf(stderr, "AudioFileInput: started playing: %s\n", m_filePath.c_str());
        fflush(stderr);
        return true;
    }

    void stop()
    {
        m_running.store(false);

        if (m_thread.joinable()) {
            m_thread.join();
        }

        closeFile();
        fprintf(stderr, "AudioFileInput: stopped\n");
        fflush(stderr);
    }

    bool isRunning() const { return m_running.load(); }

    void setLoop(bool loop) { m_loop = loop; }

private:
    bool openFile()
    {
        closeFile();

        int ret = avformat_open_input(&m_formatCtx, m_filePath.c_str(), nullptr, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "AudioFileInput: avformat_open_input failed: %s\n", errbuf);
            fflush(stderr);
            return false;
        }

        ret = avformat_find_stream_info(m_formatCtx, nullptr);
        if (ret < 0) {
            fprintf(stderr, "AudioFileInput: avformat_find_stream_info failed\n");
            fflush(stderr);
            closeFile();
            return false;
        }

        // Find audio stream
        m_audioStreamIndex = -1;
        for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
            if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                m_audioStreamIndex = i;
                break;
            }
        }

        if (m_audioStreamIndex < 0) {
            fprintf(stderr, "AudioFileInput: no audio stream found in file\n");
            fflush(stderr);
            closeFile();
            return false;
        }

        // Open codec
        AVCodecParameters* codecPar = m_formatCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
        if (!codec) {
            fprintf(stderr, "AudioFileInput: unsupported audio codec\n");
            fflush(stderr);
            closeFile();
            return false;
        }

        m_codecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_codecCtx, codecPar);

        ret = avcodec_open2(m_codecCtx, codec, nullptr);
        if (ret < 0) {
            fprintf(stderr, "AudioFileInput: avcodec_open2 failed\n");
            fflush(stderr);
            closeFile();
            return false;
        }

        // Setup resampler: convert to mono float32 @ 44100 Hz (same as PortAudioInput)
        m_swrCtx = swr_alloc();
        if (!m_swrCtx) {
            fprintf(stderr, "AudioFileInput: swr_alloc failed\n");
            fflush(stderr);
            closeFile();
            return false;
        }

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
        // FFmpeg 5.1+ uses AVChannelLayout
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_MONO;
        AVChannelLayout inLayout;
        if (m_codecCtx->ch_layout.nb_channels > 0) {
            av_channel_layout_copy(&inLayout, &m_codecCtx->ch_layout);
        } else {
            inLayout = AV_CHANNEL_LAYOUT_STEREO;
        }
        av_opt_set_chlayout(m_swrCtx, "in_chlayout", &inLayout, 0);
        av_opt_set_chlayout(m_swrCtx, "out_chlayout", &outLayout, 0);
#else
        // Older FFmpeg uses channel_layout int64
        int64_t inChLayout = m_codecCtx->channel_layout;
        if (inChLayout == 0) {
            inChLayout = av_get_default_channel_layout(m_codecCtx->channels);
        }
        av_opt_set_int(m_swrCtx, "in_channel_layout", inChLayout, 0);
        av_opt_set_int(m_swrCtx, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
#endif
        av_opt_set_int(m_swrCtx, "in_sample_rate", m_codecCtx->sample_rate, 0);
        av_opt_set_int(m_swrCtx, "out_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_codecCtx->sample_fmt, 0);
        av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

        ret = swr_init(m_swrCtx);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "AudioFileInput: swr_init failed: %s\n", errbuf);
            fflush(stderr);
            closeFile();
            return false;
        }

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
        fprintf(stderr, "AudioFileInput: opened file - codec: %s, sr: %d, ch: %d\n",
                codec->name, m_codecCtx->sample_rate, m_codecCtx->ch_layout.nb_channels);
#else
        fprintf(stderr, "AudioFileInput: opened file - codec: %s, sr: %d, ch: %d\n",
                codec->name, m_codecCtx->sample_rate, m_codecCtx->channels);
#endif
        fflush(stderr);

        return true;
    }

    void closeFile()
    {
        if (m_swrCtx) {
            swr_free(&m_swrCtx);
            m_swrCtx = nullptr;
        }
        if (m_codecCtx) {
            avcodec_free_context(&m_codecCtx);
            m_codecCtx = nullptr;
        }
        if (m_formatCtx) {
            avformat_close_input(&m_formatCtx);
            m_formatCtx = nullptr;
        }
        m_audioStreamIndex = -1;
    }

    void decodeLoop()
    {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();

        if (!packet || !frame) {
            fprintf(stderr, "AudioFileInput: failed to allocate packet/frame\n");
            fflush(stderr);
            m_running.store(false);
            return;
        }

        while (m_running.load()) {
            int ret = av_read_frame(m_formatCtx, packet);

            if (ret < 0) {
                // EOF or error
                if (m_loop && m_running.load()) {
                    // Seek back to beginning
                    av_seek_frame(m_formatCtx, m_audioStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(m_codecCtx);
                    av_packet_unref(packet);
                    continue;
                } else {
                    break;
                }
            }

            if (packet->stream_index != m_audioStreamIndex) {
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_send_packet(m_codecCtx, packet);
            av_packet_unref(packet);

            if (ret < 0) {
                continue;
            }

            while (ret >= 0 && m_running.load()) {
                ret = avcodec_receive_frame(m_codecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    break;
                }

                // Resample to mono float32 @ 44100
                int outSamples = swr_get_out_samples(m_swrCtx, frame->nb_samples);
                if (outSamples <= 0) {
                    av_frame_unref(frame);
                    continue;
                }

                // Allocate temporary output buffer
                float* outBuf = new float[outSamples];
                uint8_t* outPtr = reinterpret_cast<uint8_t*>(outBuf);

                int converted = swr_convert(m_swrCtx,
                                             &outPtr, outSamples,
                                             (const uint8_t**)frame->extended_data, frame->nb_samples);

                if (converted > 0) {
                    // PortAudioInput callback does:
                    //   memcpy(writeBuf, inputBuffer, framesPerBuffer * sizeof(complex_tx))
                    // This packs 2 consecutive float audio samples into each complex_tx:
                    //   complex_tx[i].re = audioSample[2*i], complex_tx[i].im = audioSample[2*i+1]
                    // readBufferToVector then returns them all as a flat float array.
                    // We must do the same: memcpy raw floats into writeBuf.

                    int remaining = converted;
                    int offset = 0;

                    while (remaining > 0 && m_running.load()) {
                        // Each complex_tx holds 2 floats, so chunkFloats = how many audio floats per chunk
                        // PortAudioInput uses framesPerBuffer=4096 as complex_tx count,
                        // which means 4096*2 = 8192 audio floats per swap.
                        // We use smaller chunks since our decoded frames vary in size.
                        int maxComplexPerChunk = 2048;
                        int floatsAvailable = remaining - offset;
                        // Round down to even number of floats (need pairs for complex_tx)
                        int floatsToWrite = std::min(floatsAvailable, maxComplexPerChunk * 2);
                        if (floatsToWrite <= 0) break;
                        // Make even
                        floatsToWrite = floatsToWrite & ~1;
                        if (floatsToWrite <= 0) {
                            // Odd single sample left, pad with zero
                            stream_tx.writeBuf[0].re = outBuf[offset];
                            stream_tx.writeBuf[0].im = 0.0f;
                            stream_tx.swap(1);
                            break;
                        }

                        int complexCount = floatsToWrite / 2;
                        std::memcpy(stream_tx.writeBuf, &outBuf[offset], floatsToWrite * sizeof(float));
                        stream_tx.swap(complexCount);

                        offset += floatsToWrite;
                        remaining -= floatsToWrite;

                        // Pace: complexCount complex_tx = floatsToWrite audio samples @ 44100 Hz
                        if (m_running.load() && floatsToWrite > 0) {
                            std::this_thread::sleep_for(std::chrono::microseconds(
                                static_cast<int64_t>(floatsToWrite * 1000000.0 / 44100.0 * 0.80)
                            ));
                        }
                    }
                }

                delete[] outBuf;
                av_frame_unref(frame);
            }
        }

        av_packet_free(&packet);
        av_frame_free(&frame);

        fprintf(stderr, "AudioFileInput: decode loop ended\n");
        fflush(stderr);
        m_running.store(false);
    }

    dsp::stream_tx<dsp::complex_tx>& stream_tx;
    std::atomic<bool> m_running;
    bool m_loop;
    std::string m_filePath;
    std::thread m_thread;

    AVFormatContext* m_formatCtx;
    AVCodecContext* m_codecCtx;
    SwrContext* m_swrCtx;
    int m_audioStreamIndex;
};

#endif // AUDIOFILEINPUT_H
