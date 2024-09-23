#ifndef AUDIOINPUT_H
#define AUDIOINPUT_H
#pragma once

#include <iostream>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <portaudio.h>
#include "hacktv/rf.h"
#include <thread>  // For std::this_thread::sleep_for
#include <chrono>  // For std::chrono::milliseconds
#include "types.h"
#include "stream_tx.h"

class PortAudioInput : public QObject
{
    Q_OBJECT

public:
    explicit PortAudioInput(QObject *parent = nullptr)
        : QObject(parent), stream(nullptr), isRunning(false)
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

        // Open an input stream
        PaError err = Pa_OpenDefaultStream(&stream,
                                           1,                  // Number of input channels
                                           0,                  // Number of output channels
                                           paFloat32,          // Sample format
                                           44100,              // Sample rate
                                           4096,                // Frames per buffer
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
        std::cout<< "PortAudio initialized" << std::endl;
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

    std::vector<float> readStreamToSize(size_t size) {
        std::vector<float> float_buffer;
        float_buffer.reserve(size);

        while (float_buffer.size() < size) {
            std::vector<float> temp_buffer = stream_tx.readBufferToVector();
            if (temp_buffer.empty()) {
                // Add a sleep or yield to avoid busy-waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                size_t elements_needed = size - float_buffer.size();
                size_t elements_to_add = (elements_needed < temp_buffer.size()) ? elements_needed : temp_buffer.size();
                float_buffer.insert(float_buffer.end(), temp_buffer.begin(), temp_buffer.begin() + elements_to_add);
            }
        }
        return float_buffer;
    }



private:
    static int audioCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
    {
        PortAudioInput *paInput = static_cast<PortAudioInput*>(userData);
        if (inputBuffer == nullptr) {
            std::cerr << "audioCallback: inputBuffer is null!" << std::endl;
            return paContinue;
        }

        std::memcpy(paInput->stream_tx.writeBuf, inputBuffer, framesPerBuffer * sizeof(dsp::complex_tx));
        paInput->stream_tx.swap(framesPerBuffer);
        return paContinue;
    }

    PaStream *stream;
    bool isRunning;
    QMutex m_mutex;
    QWaitCondition m_bufferNotEmpty;
    rf_t* rf_ptr;
    dsp::stream_tx<dsp::complex_tx> stream_tx;
};

#endif // AUDIOINPUT_H
