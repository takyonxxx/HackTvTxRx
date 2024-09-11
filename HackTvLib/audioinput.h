#ifndef AUDIOINPUT_H
#define AUDIOINPUT_H
#pragma once

#include <iostream>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <complex>
#include "modulation.h"
#include <portaudio.h>
#include "hacktv/rf.h"

class PortAudioInput : public QObject
{
    Q_OBJECT

public:
    explicit PortAudioInput(QObject *parent = nullptr, rf_t* rf = nullptr)
        : QObject(parent), stream(nullptr), isRunning(false), rf_ptr(rf)
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
                                           1024,                // Frames per buffer
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

    // const float* getBuffer(int& size)
    // {
    //     QMutexLocker locker(&m_mutex);
    //     if (!m_bufferNotEmpty.wait(&m_mutex, 100)) // Wait with timeout
    //     {
    //         std::cout << "Wait timeout in getBuffer" << std::endl;
    //     }
    //     size = m_buffer.size();
    //     return m_buffer.empty() ? nullptr : m_buffer.data();
    // }

    // void clearBuffer()
    // {
    //     QMutexLocker locker(&m_mutex);
    //     m_buffer.clear();
    //     m_bufferNotEmpty.wakeAll(); // Wake up any waiting threads
    // }

private:
    static int audioCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
    {
        PortAudioInput *paInput = static_cast<PortAudioInput*>(userData);

        if (paInput && paInput->isRunning) {
            QMutexLocker locker(&paInput->m_mutex);

            if (inputBuffer) {
                int numChannels = 1;
                size_t numSamples = framesPerBuffer * numChannels;

                const float* in = static_cast<const float*>(inputBuffer);
                std::vector<float> buffer(in, in + numSamples);

                std::vector<std::complex<float>> resampled_signal = apply_modulation(buffer);

                std::vector<int16_t> rf_data(resampled_signal.size() * 2);
                for (size_t i = 0; i < resampled_signal.size(); ++i) {
                    rf_data[2 * i]     = static_cast<int16_t>(resampled_signal[i].real() * 32767);
                    rf_data[2 * i + 1] = static_cast<int16_t>(resampled_signal[i].imag() * 32767);
                }

                if (rf_write(paInput->rf_ptr, rf_data.data(), rf_data.size() * sizeof(int16_t)) != RF_OK) {
                    std::cerr << "Buffer rf write error." << std::endl;
                }

                // paInput->m_buffer.clear(); // Clear old data
                // paInput->m_buffer.insert(paInput->m_buffer.end(), in, in + framesPerBuffer); // Append new data
                // paInput->m_bufferNotEmpty.wakeOne(); // Notify that buffer has new data
            }
        }
        return paContinue;
    }

    PaStream *stream;
    bool isRunning;
    QMutex m_mutex;
    QWaitCondition m_bufferNotEmpty;
    //std::vector<float> m_buffer;
    rf_t* rf_ptr;
};

#endif // AUDIOINPUT_H
