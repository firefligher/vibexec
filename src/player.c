#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <al.h>
#include <alc.h>

#include "player.h"
#include "scheduler.h"

static ALuint _buffers[4], _source;
static int started = 0;

void vibexec_player_initialize(void) {
    const char *deviceName;
    ALCdevice *device;
    ALCcontext *context;
    
    deviceName = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    device = alcOpenDevice(deviceName);
    context = alcCreateContext(device, NULL);
    alcMakeContextCurrent(context);

    alGenSources(1, &_source);
    alGenBuffers(4, _buffers);
}

void vibexec_player_update(void) {
    ALint sourceState, processedBuffers;

    alGetSourcei(_source, AL_SOURCE_STATE, &sourceState);

    if (!started) {
        struct vibexec_scheduled_buffer buffer;
        int i;

        for (i = 0; i < 4; i++) {
            ALenum format;

            vibexec_scheduler_next_buffer(&buffer);

            switch (buffer.parameters->sample_format) {
                case SIGNED_8BIT:
                    if (buffer.parameters->channels == 1) {
                        format = AL_FORMAT_MONO8;
                        break;
                    }

                    if (buffer.parameters->channels == 2) {
                        format = AL_FORMAT_STEREO8;
                        break;
                    }

                case SIGNED_16BIT:
                    if (buffer.parameters->channels == 1) {
                        format = AL_FORMAT_MONO16;
                        break;
                    }

                    if (buffer.parameters->channels == 2) {
                        format = AL_FORMAT_STEREO16;
                        break;
                    }

                default:
                    fputs("Unsupported format.\n", stderr);
                    return;
            }

            alBufferData(
                _buffers[i],
                format,
                buffer.buffer, buffer.buffer_size,
                buffer.parameters->sample_frequency
            );
        }

        alSourceQueueBuffers(_source, 4, _buffers);
        alSourcePlay(_source);
        started = 1;

        return;
    }

    alGetSourcei(_source, AL_BUFFERS_PROCESSED, &processedBuffers);

    while (processedBuffers)
    {
        ALint target;
        ALenum format;
        struct vibexec_scheduled_buffer buffer;
        alSourceUnqueueBuffers(_source, 1, &target);

        vibexec_scheduler_next_buffer(&buffer);
        
        switch (buffer.parameters->sample_format) {
            case SIGNED_8BIT:
                if (buffer.parameters->channels == 1) {
                    format = AL_FORMAT_MONO8;
                    break;
                }

                if (buffer.parameters->channels == 2) {
                    format = AL_FORMAT_STEREO8;
                    break;
                }

            case SIGNED_16BIT:
                if (buffer.parameters->channels == 1) {
                    format = AL_FORMAT_MONO16;
                    break;
                }

                if (buffer.parameters->channels == 2) {
                    format = AL_FORMAT_STEREO16;
                    break;
                }

            default:
                fputs("Unsupported format.\n", stderr);
                return;
        }

        alBufferData(
            target,
            format,
            buffer.buffer, buffer.buffer_size,
            buffer.parameters->sample_frequency
        );

        alSourceQueueBuffers(_source, 1, &target);
        processedBuffers--;
    }

    if (sourceState != AL_PLAYING) {
        alSourcePlay(_source);
    }
}
