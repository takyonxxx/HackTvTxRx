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

// Forward declaration
class HackRfDevice;

class PortAudioInput : public QObject
{
    Q_OBJECT

public:
    // New constructor: write to HackRfDevice ring buffer
    explicit PortAudioInput(HackRfDevice& device, QObject *parent = nullptr)
        : QObject(parent), stream(nullptr), m_device(&device), isRunning(false)
    {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cout << "PortAudio initialization error:" << Pa_GetErrorText(err) << std::endl;
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
            return false;
        }

        PaError err = Pa_OpenDefaultStream(&stream,
                                           1,                  // Mono input
                                           0,                  // No output
                                           paFloat32,          // Float32 samples
                                           44100,              // Sample rate
                                           1024,               // Smaller buffer for lower latency
                                           audioCallback,
                                           this);
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
        std::cout << "PortAudio initialized and stream started (ring buffer mode)" << std::endl;
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
        std::cout << "PortAudio stopped" << std::endl;
        isRunning = false;
    }

private:
    static int audioCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData);

    PaStream *stream;
    HackRfDevice* m_device;
    bool isRunning;
    QMutex m_mutex;
    QWaitCondition m_bufferNotEmpty;
};

#endif // AUDIOINPUT_H
