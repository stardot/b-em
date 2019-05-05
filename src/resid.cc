/*B-em v2.2 by Tom Walker
  resid-fp interfacing code*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "resid-fp/sid.h"
#include "sidtypes.h"
#include "sid_b-em.h"
#include "sound.h"

int sidrunning=0;
extern "C" void sid_init();
extern "C" void sid_reset();
extern "C" void sid_settype(int resamp, int model);
extern "C" uint8_t sid_read(uint16_t addr);
extern "C" void sid_write(uint16_t addr, uint8_t val);
extern "C" void sid_fillbuf(int16_t *buf, int len);

struct sound_s
{
    /* resid sid implementation */
    SIDFP *sid;
};

typedef struct sound_s sound_t;

sound_t *psid;

void sid_init()
{
        int c;
        sampling_method method=SAMPLE_INTERPOLATE;
        float cycles_per_sec=1000000;
        
        psid = new sound_t;
        psid->sid = new SIDFP;
        
        psid->sid->set_chip_model(MOS8580FP);
        
        psid->sid->set_voice_nonlinearity(1.0f);
        psid->sid->get_filter().set_distortion_properties(0.f, 0.f, 0.f);
        psid->sid->get_filter().set_type4_properties(6.55f, 20.0f);

        psid->sid->enable_filter(true);
        psid->sid->enable_external_filter(true);

        psid->sid->reset();
        
        for (c=0;c<32;c++)
                psid->sid->write(c,0);
                
        if (!psid->sid->set_sampling_parameters((float)cycles_per_sec, method,
                                                (float) FREQ_SO, 0.9*((float) FREQ_SO)/2.0))
                                            {
  //                                                      printf("reSID failed!\n");
                                                }
}

void sid_reset()
{
        int c;
        psid->sid->reset();

        for (c=0;c<32;c++)
                psid->sid->write(c,0);
                
        sidrunning=0;
}


void sid_settype(int resamp, int model)
{
        sampling_method method=(resamp)?SAMPLE_RESAMPLE_INTERPOLATE:SAMPLE_INTERPOLATE;
        if (!psid->sid->set_sampling_parameters((float)1000000, method,(float) FREQ_SO, 0.9*((float) FREQ_SO)/2.0))
        {
//                rpclog("Change failed\n");
        }

        psid->sid->get_filter().set_type4_properties(6.55f, 20.0f);
        /* Model numbers 8-15 are reserved for distorted 6581s. */
        if (model < 8 || model > 15) {
                psid->sid->set_chip_model((model)?MOS8580FP:MOS6581FP);
                psid->sid->set_voice_nonlinearity(1.0f);
                psid->sid->get_filter().set_distortion_properties(0.f, 0.f, 0.f);
        } else {
                psid->sid->set_chip_model(MOS6581FP);
                psid->sid->set_voice_nonlinearity(0.96f);
                psid->sid->get_filter().set_distortion_properties(3.7e-3f, 2048.f, 1.2e-4f);
        }
        psid->sid->input(0);
        switch (model)
        {
                case SID_MODEL_8580D:
                psid->sid->input(-32768);
                break;
                case SID_MODEL_8580R5_3691:
                psid->sid->get_filter().set_type4_properties(6.55f, 20.0f);
                break;
                case SID_MODEL_8580R5_3691D:
                psid->sid->get_filter().set_type4_properties(6.55f, 20.0f);
                psid->sid->input(-32768);
                break;

                case SID_MODEL_8580R5_1489:
                psid->sid->get_filter().set_type4_properties(5.7f, 20.0f);
                break;
                case SID_MODEL_8580R5_1489D:
                psid->sid->get_filter().set_type4_properties(5.7f, 20.0f);
                psid->sid->input(-32768);
                break;

                case SID_MODEL_6581R3_4885:
                psid->sid->get_filter().set_type3_properties(8.5e5f, 2.2e6f, 1.0075f, 1.8e4f);
                break;
                case SID_MODEL_6581R3_0486S:
                psid->sid->get_filter().set_type3_properties(1.1e6f, 1.5e7f, 1.006f, 1e4f);
                break;
                case SID_MODEL_6581R3_3984:
                psid->sid->get_filter().set_type3_properties(1.8e6f, 3.5e7f, 1.0051f, 1.45e4f);
                break;
                default:
                case SID_MODEL_6581R4AR_3789:
                psid->sid->get_filter().set_type3_properties(1.40e6f, 1.47e8f, 1.0059f, 1.55e4f);
                break;
                case SID_MODEL_6581R3_4485:
                psid->sid->get_filter().set_type3_properties(1.3e6f, 5.2e8f, 1.0053f, 1.1e4f);
                break;
                case SID_MODEL_6581R4_1986S:
                psid->sid->get_filter().set_type3_properties(1.33e6f, 2.2e9f, 1.0056f, 7e3f);
                break;
        }
}

uint8_t sid_read(uint16_t addr)
{
        return psid->sid->read(addr&0x1F);
//        return 0xFF;
}

void sid_write(uint16_t addr, uint8_t val)
{
        sidrunning=1;
        psid->sid->write(addr&0x1F,val);
}

static void fillbuf2(int& count, int16_t *buf, int len)
{
        if (sidrunning) psid->sid->clock(count, buf, len, 1);
        else            memset(buf,0,len*2);
//        printf("Result %i len %i\n",c,len);
}
void sid_fillbuf(int16_t *buf, int len)
{
        int x=64;
        
        fillbuf2(x,buf,len);
}
