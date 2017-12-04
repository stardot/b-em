#include "b-em.h"
#include "sound.h"
#include "music4000.h"

#include <allegro/config.h>
#include <windows.h>

static HMIDIIN hMidiDevice = NULL;

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

static void MidiOpenInternal(UINT nMidiDevice, const char *szName) {
    MMRESULT rv;

    if ((rv = midiInOpen(&hMidiDevice, nMidiDevice, (DWORD)(void*)MidiInProc, 0, CALLBACK_FUNCTION)) == MMSYSERR_NOERROR) {
        log_info("midi-windows: starting MIDI device #%d, %s", nMidiDevice, szName);
        midiInStart(hMidiDevice);
    }
    else
        log_error("midi-windows: unable to open MIDI device #%d, %s, rv=%d", nMidiDevice, szName, rv);
}

static void MidiOpenByName(const char *szName, UINT nMidiDevices) {
    UINT i;
    MIDIINCAPS caps;

    for (i = 0; i < nMidiDevices; i++) {
        midiInGetDevCaps(i, &caps, sizeof(MIDIINCAPS));
        if (strcasecmp(caps.szPname, szName) == 0) {
            MidiOpenInternal(i, szName);
            return;
        }
    }
    log_error("midi-windows: no MIDI device with name '%s'", szName);
}

static void MidiOpenByNum(UINT nMidiDevice, UINT nMidiDevices) {
    MIDIINCAPS caps;

    if (nMidiDevice < nMidiDevices) {
        midiInGetDevCaps(nMidiDevice, &caps, sizeof(MIDIINCAPS));
        MidiOpenInternal(nMidiDevice, caps.szPname);
    } else
        log_error("midi-windows: MIDI device number %d exceeds number of available devices", nMidiDevice);
}

void midi_init(void) {
    UINT nMidiDevices;
    const char *szName;

    if (sound_music5000) {
        if ((nMidiDevices = midiInGetNumDevs()) > 0) {
            if ((szName = get_config_string("midi", "midi_device_name", NULL)))
                MidiOpenByName(szName, nMidiDevices);
            else
                MidiOpenByNum(get_config_int("midi", "midi_device_num", 0), nMidiDevices);
        } else
            log_warn("midi-windows: no MIDI devices available");
    }
}

void midi_close(void) {
    if (hMidiDevice != NULL)
        midiInClose(hMidiDevice);
}
