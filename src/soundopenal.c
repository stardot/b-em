/*B-em v2.2 by Tom Walker
  OpenAL interface*/

#include <stdio.h>
#include <AL/al.h>
#include <AL/alut.h>
#include "b-em.h"
#include "sound.h"
#include "soundopenal.h"

// Number of buffers per sources; openAL uses these cyclically
#define NUM_AL_BUFFERS 4

#define NUM_AL_SOURCES 3

#define SOURCE_SO 0 // normal sound
#define SOURCE_DD 1 // disc drive noise
#define SOURCE_M5 2 // music 5000

#define DATA_MONO   0
#define DATA_STEREO 1

static ALuint source[NUM_AL_SOURCES];     // audio source

static int source_freq[NUM_AL_SOURCES] = {
       31250, // normal sound
       44100, // disc drive noise
       46875  // music 5000
};

// lengths in bytes (buffer holds stereo int16_t samples)

#define MAXBUFLEN (4410<<2)

static int source_buflen[NUM_AL_SOURCES] = {
       2000<<2, // normal sound
       4410<<2, // disc drive noise
       3000<<2  // music 5000
};

static ALuint buffers[NUM_AL_SOURCES][NUM_AL_BUFFERS];

static int16_t tempbuf[MAXBUFLEN>>1];

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

void al_init()
{
        int c;
        int i;

        check();

        alGenSources(NUM_AL_SOURCES, source);
        check();

        for (i = 0; i < NUM_AL_SOURCES; i++) {

                alGenBuffers(NUM_AL_BUFFERS, buffers[i]);
                check();

                alSource3f(source[i], AL_POSITION,        0.0, 0.0, 0.0);
                alSource3f(source[i], AL_VELOCITY,        0.0, 0.0, 0.0);
                alSource3f(source[i], AL_DIRECTION,       0.0, 0.0, 0.0);
                alSourcef (source[i], AL_ROLLOFF_FACTOR,  0.0          );
                alSourcei (source[i], AL_SOURCE_RELATIVE, AL_TRUE      );
                check();

                memset(tempbuf, 0, sizeof(tempbuf));
                for (c = 0; c < NUM_AL_BUFFERS; c++) {
                        alBufferData(buffers[i][c], AL_FORMAT_STEREO16, tempbuf, source_buflen[i], source_freq[i]);
                }

                alSourceQueueBuffers(source[i], NUM_AL_BUFFERS, buffers[i]);
                check();

                alSourcePlay(source[i]);
                check();
        }

//        printf("InitAL\n");
}

static void al_givebuffer_generic(int16_t *buf, int i, int type)
{
        int processed;
        int state;
        int c;

        alGetSourcei(source[i], AL_SOURCE_STATE, &state);

        if (state == 0x1014)
        {
                alSourcePlay(source[i]);
//                printf("Resetting sound\n");
        }
//        printf("State - %i %08X\n",state,state);
        alGetSourcei(source[i], AL_BUFFERS_PROCESSED, &processed);

//        printf("P ");
        check();
//        printf("Processed - %i\n",processed);

        if (processed>=1)
        {
                ALuint buffer;

                alSourceUnqueueBuffers(source[i], 1, &buffer);
//                printf("U ");
                check();

                if (type == DATA_MONO) {
                   // Expand mono data to fill both channels
                       for (c = 0; c < (source_buflen[i] >> 1); c++) tempbuf[c] = buf[c >> 1];
                       buf = tempbuf;
                }

                alBufferData(buffer, AL_FORMAT_STEREO16, buf, source_buflen[i], source_freq[i]);
//                printf("B ");
                check();

                alSourceQueueBuffers(source[i], 1, &buffer);
//                printf("Q ");
                check();

        }
}

void al_givebuffer(int16_t *buf)
{
        al_givebuffer_generic(buf, SOURCE_SO, DATA_MONO);
}

void al_givebufferdd(int16_t *buf)
{
        al_givebuffer_generic(buf, SOURCE_DD, DATA_MONO);
}

void al_givebufferm5(int16_t *buf)
{
        al_givebuffer_generic(buf, SOURCE_M5, DATA_STEREO);
}
