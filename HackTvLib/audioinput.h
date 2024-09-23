#ifndef AUDIOINPUT_H
#define AUDIOINPUT_H
#pragma once

#include <iostream>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <portaudio.h>
#include "hacktv/rf.h"
#include "types.h"
#include "stream_tx.h"

class PortAudioInput : public QObject
{
    Q_OBJECT

public:
    explicit PortAudioInput(dsp::stream_tx<dsp::complex_tx>& stream_tx, QObject *parent = nullptr)
        : QObject(parent), stream(nullptr), stream_tx(stream_tx), isRunning(false)
    {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cout<< "PortAudio initialization error:" << Pa_GetErrorText(err) << std::endl;
        }
    }

    ~PortAudioInput()
    {
        stop();
        Pa_Terminate();
    }

    bool start()
    {
        if (isRunning) {
            return false; // Already running
        }

        stream_tx.setBufferSize(STREAM_BUFFER_SIZE);

        PaError err = Pa_OpenDefaultStream(&stream,
                                           1,                  // Number of input channels
                                           0,                  // Number of output channels
                                           paFloat32,          // Sample format
                                           44100,              // Sample rate
                                           4096,               // Frames per buffer
                                           audioCallback,      // Callback function
                                           this);              // User data
        if (err != paNoError) {
            std::cout << "PortAudio stream error:" << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cout << "PortAudio stream start error:" << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        isRunning = true;
        std::cout << "PortAudio initialized and stream started" << std::endl;
        return true;
    }

    void stop()
    {
        if (!isRunning) {
            return;
        }

        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream = nullptr;
        std::cout<< "PortAudio stopped" << std::endl;
        isRunning = false;
    }

private:
    void checkStreamStatus()
    {
        while (isRunning) {
            Pa_Sleep(100);  // Check every 100ms
            if (Pa_IsStreamActive(stream) != 1) {
                std::cout << "Stream is no longer active!" << std::endl;
                PaError err = Pa_IsStreamStopped(stream);
                if (err == 1) {
                    std::cout << "Stream has stopped." << std::endl;
                } else if (err < 0) {
                    std::cout << "Error checking stream: " << Pa_GetErrorText(err) << std::endl;
                }
                isRunning = false;
                break;
            }
        }
    }

    static int audioCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
    {
        PortAudioInput *paInput = static_cast<PortAudioInput*>(userData);
        memcpy(paInput->stream_tx.writeBuf, inputBuffer, framesPerBuffer * sizeof(dsp::complex_tx));
        paInput->stream_tx.swap(framesPerBuffer);
        return paContinue;
    }

    PaStream *stream;
    dsp::stream_tx<dsp::complex_tx>& stream_tx;
    bool isRunning;
    QMutex m_mutex;
    QWaitCondition m_bufferNotEmpty;
    rf_t* rf_ptr;    
};

#endif // AUDIOINPUT_H
