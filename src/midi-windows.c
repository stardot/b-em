#include "b-em.h"
#include "midi.h"
#include "sound.h"
#include "music4000.h"

#include <allegro/config.h>
#include <windows.h>

static const char *szMusic4000InDevName;
static HMIDIIN hMusic4000MidiIn;

struct _midi_dev {
    const char *szName;
    HMIDIOUT hMidiOut;
};

midi_dev_t Music4000In;
midi_dev_t Music2000Out1;
midi_dev_t Music2000Out2;
midi_dev_t Music2000Out3;

void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2) {

    if (wMsg == MIM_DATA) {
        switch(dwParam1 & 0xf0) {
            case 0x80:
                music4000_note_off((dwParam1 >> 8) & 0xff, (dwParam1 >> 16) & 0xff);
                break;
            case 0x90:
                music4000_note_on((dwParam1 >> 8) & 0xff, (dwParam1 >> 16) & 0xff);
                break;
        }
    }
}

static void MidiOpenInInternal(UINT nMidiDevice) {
    MMRESULT rv;

    if ((rv = midiInOpen(&hMusic4000MidiIn, nMidiDevice, (DWORD)(void*)MidiInProc, 0, CALLBACK_FUNCTION)) == MMSYSERR_NOERROR) {
        log_info("midi-windows: starting MIDI in device #%d, %s", nMidiDevice, szMusic4000InDevName);
        midiInStart(hMusic4000MidiIn);
    }
    else
        log_error("midi-windows: unable to open MIDI device #%d, %s, rv=%d", nMidiDevice, szMusic4000InDevName, rv);
}

static void MidiOpenM400Dev(void) {
    UINT nMidiDevices, i;
    MIDIINCAPS caps;

    if ((nMidiDevices = midiInGetNumDevs()) > 0) {
        if (szMusic4000InDevName) {
            for (i = 0; i < nMidiDevices; i++) {
                midiInGetDevCaps(i, &caps, sizeof(MIDIINCAPS));
                if (strcasecmp(caps.szPname, szMusic4000InDevName) == 0) {
                    MidiOpenInInternal(i);
                    return;
                }
            }
            log_error("midi-windows: no MIDI device with name '%s'", szMusic4000InDevName);
        }
        midiInGetDevCaps(0, &caps, sizeof(MIDIINCAPS));
        szMusic4000InDevName = caps.szPname;
        MidiOpenInInternal(0);
    } else
        log_warn("midi-windows: no MIDI input devices available for M4000");
}

static HMIDIOUT MidiOpenOutInternal(UINT nMidiDevice, const char *szName) {
    MMRESULT rv;
    HMIDIOUT hMidiDevice;

    if ((rv = midiOutOpen(&hMidiDevice, nMidiDevice, 0, 0, CALLBACK_NULL)) == MMSYSERR_NOERROR) {
        log_info("midi-windows: opened MIDI out device #%d, %s", nMidiDevice, szName);
        return hMidiDevice;
    }
    else {
        log_error("midi-windows: unable to open MIDI device #%d, %s, rv=%d", nMidiDevice, szName, rv);
        return NULL;
    }
}

static void MidiOpenM2000Devs(void) {
    UINT nMidiDevices, i;
    MIDIOUTCAPS caps;

    if ((nMidiDevices = midiOutGetNumDevs()) > 0) {
        if (Music2000Out1.szName || Music2000Out2.szName || Music2000Out3.szName) {
            for (i = 0; i < nMidiDevices; i++) {
                midiOutGetDevCaps(i, &caps, sizeof(MIDIOUTCAPS));
                if (Music2000Out1.szName && !Music2000Out1.hMidiOut && strcmp(caps.szPname, Music2000Out1.szName) == 0)
                    Music2000Out1.hMidiOut = MidiOpenOutInternal(i, Music2000Out1.szName);
                else if (Music2000Out2.szName && !Music2000Out2.hMidiOut && strcmp(caps.szPname, Music2000Out2.szName) == 0)
                    Music2000Out2.hMidiOut = MidiOpenOutInternal(i, Music2000Out2.szName);
                else if (Music2000Out3.szName && !Music2000Out3.hMidiOut && strcmp(caps.szPname, Music2000Out3.szName) == 0)
                    Music2000Out3.hMidiOut = MidiOpenOutInternal(i, Music2000Out3.szName);
            }
        }
        if (!Music2000Out1.hMidiOut || !Music2000Out2.hMidiOut || !Music2000Out3.hMidiOut) {
            for (i = 0; i < nMidiDevices; i++) {
                midiOutGetDevCaps(i, &caps, sizeof(MIDIOUTCAPS));
                if (!Music2000Out1.hMidiOut) {
                    Music2000Out1.szName = strdup(caps.szPname);
                    Music2000Out1.hMidiOut = MidiOpenOutInternal(i, Music2000Out1.szName);
                } else if (!Music2000Out2.hMidiOut) {
                    Music2000Out2.szName = strdup(caps.szPname);
                    Music2000Out2.hMidiOut = MidiOpenOutInternal(i, Music2000Out2.szName);
                } else if (!Music2000Out3.hMidiOut) {
                    Music2000Out3.szName = strdup(caps.szPname);
                    Music2000Out3.hMidiOut = MidiOpenOutInternal(i, Music2000Out3.szName);
                } else
                    break;
            }
        }
    } else
        log_warn("midi-windows: no MIDI output devices available for M2000");
}    

void midi_init(void) {
    MidiOpenM400Dev();
    MidiOpenM2000Devs();
}

void midi_close(void) {
    if (hMusic4000MidiIn != NULL)
        midiInClose(hMusic4000MidiIn);
    if (Music2000Out1.hMidiOut != NULL)
        midiOutClose(Music2000Out1.hMidiOut);
    if (Music2000Out2.hMidiOut != NULL)
        midiOutClose(Music2000Out2.hMidiOut);
    if (Music2000Out3.hMidiOut != NULL)
        midiOutClose(Music2000Out3.hMidiOut);
}

void midi_load_config(void) {
    szMusic4000InDevName = get_config_string("midi", "music4000_in_device",   NULL);
    Music2000Out1.szName = get_config_string("midi", "music2000_out1_device", NULL);
    Music2000Out2.szName = get_config_string("midi", "music2000_out2_device", NULL);
    Music2000Out3.szName = get_config_string("midi", "music2000_out3_device", NULL);
};

void midi_save_config(void) {
    if (szMusic4000InDevName)
        set_config_string("midi", "music4000_in_device", szMusic4000InDevName);
    if (Music2000Out1.szName)
        set_config_string("midi", "music2000_out1_device", Music2000Out1.szName);
    if (Music2000Out2.szName)
        set_config_string("midi", "music2000_out2_device", Music2000Out2.szName);
    if (Music2000Out3.szName)
        set_config_string("midi", "music2000_out3_device", Music2000Out3.szName);
}

void midi_send_msg(midi_dev_t *dev, uint8_t *msg, size_t size) {
    DWORD value = msg[0] | (msg[1] << 8) | (msg[2] << 16);
    MMRESULT res;

    if ((res = midiOutShortMsg(dev->hMidiOut, value)) != MMSYSERR_NOERROR)
        log_error("midi-windows: unable to send MIDI event on %s: %d", dev->szName, res);
}
