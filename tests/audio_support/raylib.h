#pragma once
// Minimal raylib 5.5 audio API declarations for the offline test double.
// No platform library or real audio device is used by this test target.
// The production game includes raylib's own header instead.
typedef struct rAudioBuffer rAudioBuffer;
typedef struct rAudioProcessor rAudioProcessor;
typedef struct AudioStream {
    rAudioBuffer* buffer;
    rAudioProcessor* processor;
    unsigned int sampleRate, sampleSize, channels;
} AudioStream;
typedef void (*AudioCallback)(void* bufferData, unsigned int frames);

extern "C" {
void InitAudioDevice(void);
void CloseAudioDevice(void);
bool IsAudioDeviceReady(void);
AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels);
bool IsAudioStreamValid(AudioStream stream);
void UnloadAudioStream(AudioStream stream);
void PlayAudioStream(AudioStream stream);
void StopAudioStream(AudioStream stream);
void SetAudioStreamBufferSizeDefault(int size);
void SetAudioStreamCallback(AudioStream stream, AudioCallback callback);
}
