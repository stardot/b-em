/*B-em v2.2 by Tom Walker
  OpenAL interface*/

#include <stdio.h>
#include <AL/al.h>
#include <AL/alut.h>
#include "b-em.h"
#include "sound.h"
#include "soundopenal.h"

static ALuint buffers[4]; // front and back buffers
static ALuint source[2];     // audio source
static ALuint buffersdd[4]; // front and back buffers
static ALenum format;     // internal format

#define FREQ 31250
//#define BUFLEN (3125<<2)
#define BUFLEN (2000<<2)

static void check()
{
        ALenum error;
        if ((error = alGetError()) != AL_NO_ERROR)
        {
                //printf("AL Error : %08X\n", error);
                //printf("Description : %s\n",alGetErrorString(error));
        }
/*        if ((error = alutGetError()) != ALUT_ERROR_NO_ERROR)
        {
                printf("ALut Error : %08X\n", error);
                printf("Description : %s\n",alutGetErrorString(error));
        }*/
}

void al_init_main(int argc, char *argv[])
{
        alutInit(0, NULL);//&argc, argv);
        check();
//        atexit(closeal);
//        printf("AlutInit\n");
}

void al_close()
{
        alutExit();
}

static int16_t tempbuf[BUFLEN>>1];
static int16_t tempbufdd[4410*2];

void al_init()
{
        int c;
        format = AL_FORMAT_STEREO16;
        check();

        alGenBuffers(4, buffers);
        check();

        alGenSources(2, source);
        check();

        alSource3f(source[0], AL_POSITION,        0.0, 0.0, 0.0);
        alSource3f(source[0], AL_VELOCITY,        0.0, 0.0, 0.0);
        alSource3f(source[0], AL_DIRECTION,       0.0, 0.0, 0.0);
        alSourcef (source[0], AL_ROLLOFF_FACTOR,  0.0          );
        alSourcei (source[0], AL_SOURCE_RELATIVE, AL_TRUE      );
        check();

        memset(tempbuf, 0, BUFLEN);

        for (c = 0; c < 4; c++)
            alBufferData(buffers[c], AL_FORMAT_STEREO16, tempbuf, BUFLEN, 31250);
        alSourceQueueBuffers(source[0], 4, buffers);
        check();
        alSourcePlay(source[0]);
        check();
//        printf("InitAL\n");

        alGenBuffers(4, buffersdd);
        check();

        alSource3f(source[1], AL_POSITION,        0.0, 0.0, 0.0);
        alSource3f(source[1], AL_VELOCITY,        0.0, 0.0, 0.0);
        alSource3f(source[1], AL_DIRECTION,       0.0, 0.0, 0.0);
        alSourcef (source[1], AL_ROLLOFF_FACTOR,  0.0          );
        alSourcei (source[1], AL_SOURCE_RELATIVE, AL_TRUE      );
        check();

        memset(tempbufdd, 0, 4410 * 4);

        for (c = 0; c < 4; c++)
            alBufferData(buffersdd[c], AL_FORMAT_STEREO16, tempbufdd, 4410*4, 44100);
        alSourceQueueBuffers(source[1], 4, buffersdd);
        check();
        alSourcePlay(source[1]);
        check();
//        printf("InitAL\n");
}

static int16_t zbuf[16384];

void al_givebuffer(int16_t *buf)
{
        int processed;
        int state;
        int c;

//return;
        alGetSourcei(source[0], AL_SOURCE_STATE, &state);

        if (state == 0x1014)
        {
                alSourcePlay(source[0]);
//                printf("Resetting sound\n");
        }
//        printf("State - %i %08X\n",state,state);
        alGetSourcei(source[0], AL_BUFFERS_PROCESSED, &processed);

//        printf("P ");
        check();
//        printf("Processed - %i\n",processed);

        if (processed>=1)
        {
                ALuint buffer;

                alSourceUnqueueBuffers(source[0], 1, &buffer);
//                printf("U ");
                check();

                for (c = 0; c < (BUFLEN >> 1); c++) zbuf[c] = buf[c >> 1];

                alBufferData(buffer, AL_FORMAT_STEREO16, zbuf, BUFLEN, 31250);
//                printf("B ");
                check();

                alSourceQueueBuffers(source[0], 1, &buffer);
//                printf("Q ");
                check();

        }
}

void al_givebufferdd(int16_t *buf)
{
        int processed;
        int state;
        int c;

        if (!sound_ddnoise && !sound_tape) return;

        alGetSourcei(source[1], AL_SOURCE_STATE, &state);

        if (state == 0x1014)
        {
                alSourcePlay(source[1]);
//                printf("Resetting sounddd\n");
        }
        alGetSourcei(source[1], AL_BUFFERS_PROCESSED, &processed);
//bem_debug("Get source\n");
        check();
//bem_debug("Got source\n");
        if (processed>=1)
        {
                ALuint buffer;

//bem_debug("Unqueue\n");
                alSourceUnqueueBuffers(source[1], 1, &buffer);
                check();

                for (c = 0; c < (4410 * 2); c++) zbuf[c] = buf[c >> 1];//^0x8000;

//bem_debug("BufferData\n");
                alBufferData(buffer, AL_FORMAT_STEREO16, zbuf, 4410*4, 44100);
                check();

//bem_debug("Queue\n");
                alSourceQueueBuffers(source[1], 1, &buffer);
                check();
        }

//        bem_debug("DDnoise3\n");
}
