/*B-em v2.2 by Tom Walker
  OpenAL interface*/

#include <stdio.h>
#include <AL/al.h>
#include <AL/alut.h>
#include "b-em.h"
#include "sound.h"
#include "soundopenal.h"

static ALuint buffers[4]; // front and back buffers
static ALuint source[3];     // audio source
static ALuint buffersdd[4]; // front and back buffers
static ALuint buffersm5[4]; // front and back buffers
static ALenum format;     // internal format

#define FREQ 31250
#define FREQDD 44100
#define FREQM5 46875

#define BUFLEN (2000<<2)
#define BUFLENDD (4410<<2)
#define BUFLENM5 (3000<<2)

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
static int16_t tempbufdd[BUFLENDD>>1];
static int16_t tempbufm5[BUFLENM5>>1];

void al_init()
{
        int c;
        format = AL_FORMAT_STEREO16;
        check();

        alGenBuffers(4, buffers);
        check();

        alGenSources(3, source);
        check();

        alSource3f(source[0], AL_POSITION,        0.0, 0.0, 0.0);
        alSource3f(source[0], AL_VELOCITY,        0.0, 0.0, 0.0);
        alSource3f(source[0], AL_DIRECTION,       0.0, 0.0, 0.0);
        alSourcef (source[0], AL_ROLLOFF_FACTOR,  0.0          );
        alSourcei (source[0], AL_SOURCE_RELATIVE, AL_TRUE      );
        check();

        memset(tempbuf, 0, BUFLEN);

        for (c = 0; c < 4; c++)
            alBufferData(buffers[c], AL_FORMAT_STEREO16, tempbuf, BUFLEN, FREQ);
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

        memset(tempbufdd, 0, BUFLENDD);

        for (c = 0; c < 4; c++)
            alBufferData(buffersdd[c], AL_FORMAT_STEREO16, tempbufdd, BUFLENDD, FREQDD);
        alSourceQueueBuffers(source[1], 4, buffersdd);
        check();
        alSourcePlay(source[1]);
        check();

        alGenBuffers(4, buffersm5);
        check();

        alSource3f(source[2], AL_POSITION,        0.0, 0.0, 0.0);
        alSource3f(source[2], AL_VELOCITY,        0.0, 0.0, 0.0);
        alSource3f(source[2], AL_DIRECTION,       0.0, 0.0, 0.0);
        alSourcef (source[2], AL_ROLLOFF_FACTOR,  0.0          );
        alSourcei (source[2], AL_SOURCE_RELATIVE, AL_TRUE      );
        check();

        memset(tempbufm5, 0, BUFLENM5);

        for (c = 0; c < 4; c++)
            alBufferData(buffersm5[c], AL_FORMAT_STEREO16, tempbufm5, BUFLENM5, FREQM5);
        alSourceQueueBuffers(source[2], 4, buffersm5);
        check();
        alSourcePlay(source[2]);
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

                alBufferData(buffer, AL_FORMAT_STEREO16, zbuf, BUFLEN, FREQ);
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

                for (c = 0; c < (BUFLENDD >> 1); c++) zbuf[c] = buf[c >> 1];//^0x8000;

//bem_debug("BufferData\n");
                alBufferData(buffer, AL_FORMAT_STEREO16, zbuf, BUFLENDD, FREQDD);
                check();

//bem_debug("Queue\n");
                alSourceQueueBuffers(source[1], 1, &buffer);
                check();
        }

//        bem_debug("DDnoise3\n");
}

void al_givebufferm5(int16_t *buf)
{
        int processed;
        int state;
        int c;

        if (!sound_music5000) return;

        alGetSourcei(source[2], AL_SOURCE_STATE, &state);

        if (state == 0x1014)
        {
                alSourcePlay(source[2]);
//                printf("Resetting soundm5\n");
        }
        alGetSourcei(source[2], AL_BUFFERS_PROCESSED, &processed);
//bem_debug("Get source\n");
        check();
//bem_debug("Got source\n");
        if (processed>=1)
        {
                ALuint buffer;

//bem_debug("Unqueue\n");
                alSourceUnqueueBuffers(source[2], 1, &buffer);
                check();

                for (c = 0; c < (BUFLENM5 >> 1); c++) zbuf[c] = buf[c >> 1];//^0x8000;

//bem_debug("BufferData\n");
                alBufferData(buffer, AL_FORMAT_STEREO16, zbuf, BUFLENM5, FREQM5);
                check();

//bem_debug("Queue\n");
                alSourceQueueBuffers(source[2], 1, &buffer);
                check();
        }

//        bem_debug("DDnoise3\n");
}
