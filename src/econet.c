#define _DEBUG
/*
 * This file is part of B-Em, a BBC Micro Emulator by Sarah Walker
 * and provides support for Econet, Acorn's low-cost network.
 *
 * A version of this file was originally part of BeebEm and has
 * been ported to B-Em by Steve Fosdick, 2021.  The original is:
 *
 * Copyright (C) 2004  Rob O'Donnell
 * Copyright (C) 2005  Mike Wyatt
 *
 * AUN support was added (to BeebEm) by Rob in Jun/Jul 2009.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

/*
 * The following conditional compilation is to deal with differences
 * between WINSOCK and the original Berkeley networking as used in
 * Unix-like systems.
 */
#ifdef WIN32
#include <windows.h>
#define local_ipaddr(a) (a.S_un.S_addr)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1
#define closesocket close
#define WSAGetLastError() (long)errno
#define WSACleanup()
#define SOCKADDR struct sockaddr
#define local_ipaddr(a) (a.s_addr)
#endif

#include "b-em.h"
#include "model.h"
#include "cmos.h"
#include "6502.h"
#include "econet.h"
#include "via.h"
#include "sysvia.h"
#include <ctype.h>

bool EconetEnabled;             // Enable hardware
bool EconetNMIenabled;          // 68B54 -> NMI enabled. (IC97)

/* Acorn Universal Networking (AUN) packet header. */

struct aunhdr {
    uint8_t type;               /* AUN magic protocol byte */
#define AUN_TYPE_BROADCAST  1   // 'type' from NetBSD aund.h
#define AUN_TYPE_UNICAST    2
#define AUN_TYPE_ACK        3
#define AUN_TYPE_NACK       4
#define AUN_TYPE_IMMEDIATE  5
#define AUN_TYPE_IMM_REPLY  6

    uint8_t port;               // dest port
#define EC_PORT_FS              0x99
#define EC_PORT_PS_STATUS_ENQ   0x9f
#define EC_PORT_PS_STATUS_REPLY 0x9e
#define EC_PORT_PS_JOB          0xd1

    uint8_t cb;                 // flag
    uint8_t pad;                // retrans
    uint32_t handle;            // 4 byte sequence little-endian.
};

static unsigned long ec_sequence = 0; // AUN sequence.

/*
 * A finite state machine to handle the translation between AUN and
 * tradional Econet.  This includes the ability to fake the scout
 * packet and its response in each direction.
 */

enum fourway {
    FWS_IDLE,
    FWS_SCOUTSENT,  // BBC micro sent scout (on Econet, suppressed on AUN).
    FWS_SCACKRCVD,  // Fake scout ack created for BBC micro to receive.
    FWS_DATASENT,   // BBC micro data packet sent to AUN.
    FWS_WAIT4IDLE,
    FWS_SCOUTRCVD,  // Data has arrived on AUN, faking scout to BBC micro.
    FWS_SCACKSENT,  // BBC micro has send scout ack, running a timer.
    FWS_DATARCVD,   // Deliver the (delayed) AUN packet to the BBC micro.
    FWS_IMMSENT,    // immediate scout packet sent on AUN
    FWS_IMMRCVD
};

static enum fourway fourwaystage;

#ifdef _DEBUG

static const char fws_names[][10] = {
    "IDLE",
    "SCOUTSENT",
    "SCACKRCVD",
    "DATASENT",
    "WAIT4IDLE",
    "SCOUTRCVD",
    "SCACKSENT",
    "IMMSENT",
    "IMMRCVD"
};

#endif

/* Headers for traditional Econet */

struct shorteconethdr {
    uint8_t deststn;
    uint8_t destnet;
    uint8_t srcstn;
    uint8_t srcnet;
};

struct longeconetpacket {
    uint8_t deststn;
    uint8_t destnet;
    uint8_t srcstn;
    uint8_t srcnet;
    uint8_t cb;
    uint8_t port;
};

/*
 * What is the biggest packet required?  The MC6854 has 3 byte FIFOs.
 * There is no wait for an end of data before transmission starts. Data
 * is sent immediately it's put into the first slot.
 *
 * Does Econet send multiple packets for big transfers, or just one
 * huge packet?  What's MTU on econet? Depends on clock speed but it's
 * big (e.g. 100K).  As we are using UDP, we will use a larger buffer
 * buffer and accept data into this, then send it periodically.  We
 * will accept incoming data similarly, and dribble it back into the
 * emulated 68B54.
 *
 * We should thus never suffer underrun errors....
 * -- we do actually flag an underrun, if data exceeds the size of the buffer.
 * -- sniffed AUN between live arcs seems to max out at 1288 bytes (1280+header)
 * -- bigger packers ARE possible - UDP fragments & reassembles transparently.. doh..
 *
 * 64K max.. can't see any transfers being neeeded larger than this too often!
 * (and that's certainly larger than acorn bridges can cope with.)
 */

#define ETHERNETBUFFERSIZE 65536

/* Defintion for the AUN packets sent over UDP. */

struct aunpacket {
    union {
        uint8_t raw[8];
        struct aunhdr ah;
    };
    union {
        uint8_t buff[ETHERNETBUFFERSIZE];
        struct shorteconethdr eh;
    };
    volatile unsigned int Pointer;
    volatile unsigned int BytesInBuffer;
    unsigned long inet_addr;
    unsigned int port;
    unsigned int deststn;
    unsigned int destnet;
};

/* buffers used to construct packets for sending out via UDP */

static struct aunpacket EconetRx;
static struct aunpacket EconetTx;

// buffers used to construct packets sent to/received from bbc micro

struct econetpacket {
    union {
        struct longeconetpacket eh;
        uint8_t buff[ETHERNETBUFFERSIZE + 12];
    };
    volatile unsigned int Pointer;
    volatile unsigned int BytesInBuffer;
};

static struct econetpacket BeebTx;
static struct econetpacket BeebRx;

uint8_t BeebTxCopy[6];          // size of longeconetpacket structure

/* Networking Table */

// Holds data from econet.cfg file
struct ECOLAN {                 // what we we need to find a beeb?
    struct ECOLAN *next;
    uint8_t station;
    uint8_t network;
    uint16_t port;
    struct in_addr inet_addr;
};

struct AUNTAB {
    struct AUNTAB *next;
    struct in_addr inet_addr;
    uint8_t network;
};

static struct ECOLAN *networks;        // list of my friends! :-)
static struct AUNTAB *aunnet;          // AUNmap file for guess mode.
static struct AUNTAB *myaunnet;        // aunnet entry that I match.

static int inmask, outmask;

/* Configuration Options.
 * These, among others, are overridden in econet.cfg
 * (see econet_read_netfile())
 */
static bool confAUNmode = false;       // Use AUN style networking
static bool confLEARN = false;         // Add receipts from unknown hosts to network table
static bool confSTRICT = false;        // Assume network ip=stn number when sending to unknown hosts
static bool confSingleSocket = true;   // use same socket for Send and receive
static unsigned int FourWayStageTimeout = 15625;
static bool MassageNetworks = false;   // massage network numbers on send/receive (add/sub 128)

/* Station Configuration settings:
 * You specify station number on command line.
 * This allows multiple different instances of the emulator to be run
 * and to communicate with each other.  Note that you STILL need to
 * have them all listed in econet.cfg so each one knows where the
 * others are.
 */
static uint8_t EconetStationNumber = 0;        // default Station Number
static unsigned int EconetListenPort = 0;      // default Listen port
static unsigned long EconetListenIP = 0x0100007f;
// IP settings:
static SOCKET ListenSocket = INVALID_SOCKET;   // Listen socket
static SOCKET SendSocket = INVALID_SOCKET;
static bool ReceiverSocketsOpen = false;       // Used to flag line up and clock running

/* Flag Fill.
 *
 * A receiving station goes into flag fill mode while it is processing,
 * a message.  This stops other stations sending messages that may
 * interfere with the four-way handshake. That means from the emulated
 * perspective of this BBC micro, the remote end would flag-fill
 * between an outgoing scout and the acknowledgement and between an
 * outgoing data packet and its acknowledgement.  The BBC micro itself
 * would flag fill between receiving a flag acknowledgement and sending
 * the data packet.
 *
 * Attempting to notify evey station using IP messages when flag fill
 * goes active/inactive would be complicated and would suffer from
 * timing issues due to network latency, so a pseudo flag fill
 * algorithm is emulated.  We assume that the receiving station will go
 * into flag fill when we send a message or when we see a message
 * destined for another station.  We cancel flag fill when we receive a
 * message as the other station must have cancelled flag fill.  In
 * order to cancel flag fill after the last message of a four-way
 * handshake we time it out - which is not ideal as we do not want to
 * delay new messages any longer that we have to - but it will have
 * to do for now!
 */

static bool FlagFillActive;                             // Flag fill state
static unsigned long EconetFlagFillTimeoutTrigger;      // Trigger point for flag fill
static unsigned long EconetFlagFillTimeout = 500000;    // Cycles for flag fill timeout // added cfg file to override this

/* In the BeebEm version the time between network bytes was
 * configurable but in B-Em we have hooked the function otherstuff_poll
 * which fixes the interval at 128 CPU cycles.  The original BeebEm
 * comment follows...
 *
 * Frequency between network actions.
 * max 250Khz network clock. 2MHz system clock. one click every 8 cycles.
 * say one byte takes about 8 clocks, receive a byte every 64 cpu cycyes?
 * (The reason for "about" 8 clocks is that as this a continuous syncronous tx,
 * there are no start/stop bits, however to avoid detecting a dead line as ffffff
 * zeros are added and removed transparently if you get more than five "1"s
 * during data transmission - more than 5 are flags or errors)
 * 6854 datasheet has max clock frequency of 1.5MHz for the B version.
 * 64 cycles seems to be a bit fast for 'netmon' prog to keep up - set to 128.
 */

/*
 * BeebEm has a central, incrementing counter which can be used to
 * implement timeouts.  B-Em does not so this module keeps its own
 * counter.  This ticks in units of 128 CPU cycles.
 */
static unsigned long EconetCycles = 0;

/* Other timeouts using the above cycke counter */

static unsigned long EconetSCACKtrigger;         //trigger point for scout ack
static unsigned long EconetSCACKtimeout = 4;   // cycles to delay before sending ack to scout (aun mode only)
static unsigned long EconetWait4IdleTrigger;
static unsigned long EconetWait4IdleTimeout = 12;
static unsigned long Econet4Wtrigger;

/* Device and temp copy */

volatile struct MC6854 ADLC;
static uint8_t old_status1, old_status2;

static const uint8_t powers[4] = { 1, 2, 4, 8 };

uint8_t irqcause;               // flagto indicate cause of irq sr1b7
uint8_t sr1b2cause;             // flagto indicate cause of irq sr1b2

static void econet_free_networks(void)
{
    struct ECOLAN *ptr1 = networks;
    while (ptr1) {
        struct ECOLAN *ptr2 = ptr1->next;
        free(ptr1);
        ptr1 = ptr2;
    }
    networks = NULL;
}

static void econet_free_aunmap(void)
{
    struct AUNTAB *ptr1 = aunnet;
    while (ptr1) {
        struct AUNTAB *ptr2 = ptr1->next;
        free(ptr1);
        ptr1 = ptr2;
    }
    aunnet = NULL;
}

static void econet_read_netfile(void)
{
    // read econet.cfg file into network table
    ALLEGRO_PATH *path = find_cfg_file("econet", ".cfg");
    if (path) {
        const char *cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        FILE *EcoCfg = fopen(cpath, "rt");
        if (EcoCfg) {
            econet_free_networks();
            char EcoNameBuf[256];
            unsigned lineno = 0;
            while (fgets(EcoNameBuf, sizeof(EcoNameBuf)-1, EcoCfg)) {
                lineno++;
                char *EcoName = EcoNameBuf;
                log_debug("Econet: ConfigFile %s", EcoName);
                int ch = *EcoName;
                while (ch == ' ' || ch == '\t')
                    ch = *++EcoName;
                if (ch != '#') {
                    if (isdigit(ch)) {
                        char *end;
                        uint8_t network = strtol(EcoName, &end, 10);
                        if (end > EcoName) {
                            EcoName = end;
                            uint8_t station = strtol(EcoName, &end, 10);
                            if (end > EcoName) {
                                EcoName = end;
                                ch = *EcoName;
                                while (ch && isspace(ch))
                                    ch = *++EcoName;
                                char *EcoPtr = EcoName;
                                while (ch && (ch == '.' || isdigit(ch)))
                                    ch = *++EcoPtr;
                                if (EcoPtr > EcoName) {
                                    *EcoPtr++ = 0;
                                    struct in_addr iaddr;
                                    if (inet_aton(EcoName, &iaddr)) {
                                        unsigned int port = strtol(EcoPtr, &end, 10);
                                        if (end > EcoPtr) {
                                            log_debug("Econet: ConfigFile Net %i Stn %i IP %s Port %i", network, station, inet_ntoa(iaddr), port);
                                            struct ECOLAN *entry = malloc(sizeof(struct ECOLAN));
                                            if (entry) {
                                                entry->next = networks;
                                                entry->station = station;
                                                entry->network = network;
                                                entry->inet_addr = iaddr;
                                                entry->port = port;
                                                networks = entry;
                                            }
                                            else {
                                                log_error("econet: out of memory for network table");
                                                break;
                                            }
                                        }
                                        else
                                            log_warn("Econet: %s, line %u: missing or invalid port number", cpath, lineno);
                                    }
                                    else
                                        log_warn("Econet: %s, line %u: invalid IP address '%s'", cpath, lineno, EcoName);
                                }
                                else
                                    log_warn("Econet: %s, line %u: missing IP address", cpath, lineno);
                            }
                            else
                                log_warn("Econet: %s, line %u: invalid station number '%s'", cpath, lineno, EcoName);
                        }
                        else
                            log_warn("Econet: %s, line %u: invalid network number '%s'", cpath, lineno, EcoName);
                    }
                    else {
                        char *EcoPtr = EcoName;
                        do {
                            if (isupper(ch))
                                ch = tolower(ch);
                            *EcoPtr = ch;
                            ch = *++EcoPtr;
                        } while (ch && !isspace(ch));

                        if (ch) {
                            bool bad = false;
                            *EcoPtr++ = 0;
                            int value = atoi(EcoPtr);
                            if (strcmp("aunmode", EcoName) == 0)
                                confAUNmode = (value != 0);
                            else if (strcmp("learn", EcoName) == 0)
                                confLEARN = (value != 0);
                            else if (strcmp("aunstrict", EcoName) == 0)
                                confSTRICT = (value != 0);
                            else if (strcmp("singlesocket", EcoName) == 0)
                                confSingleSocket = (value != 0);
                            else if (strcmp("flagfilltimeout", EcoName) == 0)
                                EconetFlagFillTimeout = (value);
                            else if (strcmp("scacktimeout", EcoName) == 0)
                                EconetSCACKtimeout = (value);
                            else if (strcmp("fourwaytimeout", EcoName) == 0)
                                FourWayStageTimeout = (value);
                            else if (strcmp("massagenets", EcoName) == 0)
                                MassageNetworks = (value != 0);
                            else {
                                bad = true;
                                log_warn("Econet: unrecognised option name %s in config file %s", EcoName, cpath);
                            }
                            if (!bad)
                                log_debug("Econet: Config option %s flag %i", EcoName, value);
                        }
                    }
                }
            }
            fclose(EcoCfg);

            if (MassageNetworks) {
                inmask = 255;
                outmask = 0;
            }
            else {
                inmask = 127;
                outmask = 128;
            }
        }
        else
            log_error("Econet: Failed to open configuration file %s", cpath);
        al_destroy_path(path);
    }
    else
        log_error("Econet: unable to find Econet configuration file econet.cfg");

    if (confAUNmode) {          // don't bother reading file if not using AUN.
        if ((path = find_cfg_file("AUNmap", ".cfg"))) {
            const char *cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
            FILE *EcoCfg = fopen(cpath, "rt");
            if (EcoCfg) {
                econet_free_aunmap();
                char EcoNameBuf[256];
                unsigned lineno = 0;
                while (fgets(EcoNameBuf, sizeof(EcoNameBuf), EcoCfg)) {
                    lineno++;
                    char *EcoName = EcoNameBuf;
                    int ch = *EcoName;
                    while (ch && isspace(ch))
                        ch = *++EcoName;
                    char *EcoSep = EcoName;
                    while (ch && !isspace(ch))
                        ch = *++EcoSep;
                    if (ch) {
                        *EcoSep++ = 0;
                        if (!strcasecmp(EcoName, "AddMap")) {
                            while (ch && isspace(ch))
                                ch = *EcoSep++;
                            if (ch) {
                                EcoName = EcoSep;
                                while (ch && (isdigit(ch) || ch == '.'))
                                    ch = *EcoSep++;
                                if (ch && EcoSep > EcoName) {
                                    EcoSep[-1] = 0;
                                    struct in_addr iaddr;
                                    if (inet_aton(EcoName, &iaddr)) {
                                        while (ch && isspace(ch))
                                            ch = *EcoSep++;
                                        if (ch) {
                                            unsigned net = strtol(EcoSep, NULL, 10);
                                            log_debug("Econet: AUNmap Net %i IP %s ", net, inet_ntoa(iaddr));
                                            struct AUNTAB *entry = malloc(sizeof(struct AUNTAB));
                                            if (entry) {
                                                uint32_t haddr = ntohl(iaddr.s_addr) & 0xffffff00;
                                                entry->next = aunnet;
                                                entry->inet_addr.s_addr = htonl(haddr);
                                                entry->network = net;
                                                aunnet = entry;
                                                // note which network we are a part of.. this wont work on first run as listenip not set!
                                                if (haddr == (ntohl(EconetListenIP) & 0xffffff00)) {
                                                    myaunnet = entry;
                                                    log_debug("Econet: ..and that's the one we're in");
                                                }
                                            }
                                            else {
                                                log_error("econet: out of memory for AUN map");
                                                break;
                                            }
                                        }
                                        else
                                            log_warn("Econet: %s, line %u: missing network", cpath, lineno);
                                    }
                                    else
                                        log_warn("Econet: %s, line %u: invalid IP address '%s'", cpath, lineno, EcoName);
                                }
                                else
                                    log_warn("Econet: %s, line %u: missing IP address", cpath, lineno);
                            }
                            else
                                log_warn("Econet: %s, line %u: missing IP address", cpath, lineno);
                        }
                    }
                }
                fclose(EcoCfg);
            }
            else
                log_error("Econet: unable to open AUN map %s: %s", cpath, strerror(errno));
        }
        else
            log_error("Econet: unable to find AUN map AUNmap.cfg");
    }
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

#ifdef _DEBUG

void econet_adlc_debug(void)
{
    log_debug("ADLC: Ctl:%02X %02X %02X %02X St:%02X %02X TXptr:%01x rx:%01x FF:%d IRQc:%02x SR2c:%02x PC:%04x 4W:%s",
            (int)ADLC.control1, (int)ADLC.control2, (int)ADLC.control3, (int)ADLC.control4,
            (int)ADLC.status1, (int)ADLC.status2, (int)ADLC.txfptr, (int)ADLC.rxfptr, FlagFillActive ? 1 : 0,
            (int)irqcause, (int)sr1b2cause, (int)pc, fws_names[fourwaystage]);
}
#else
static inline void econet_adlc_debug(void) {}
#endif

void econet_reset(void)
{
    if (EconetEnabled)
        log_debug("Econet: reset, hardware enabled");
    else
        log_debug("Econet: reset, hardware disabled");

    // hardware operations:
    // set RxReset and TxReset
    ADLC.control1 = ADLC_CTL1_RX_RESET|ADLC_CTL1_TX_RESET;
    // reset TxAbort, RTS, LoopMode, DTR
    ADLC.control4 = 0;
    ADLC.control2 = 0;
    ADLC.control3 = 0;

    // clear all status conditions
    ADLC.status1 = 0;           //cts - clear to send line input (no collissions talking udp)
    ADLC.status2 = 0;           //dcd - no clock (until sockets initialised and open)
    ADLC.sr2pse = 0;

    //software stuff:
    EconetRx.Pointer = 0;
    EconetRx.BytesInBuffer = 0;
    EconetTx.Pointer = 0;
    EconetTx.BytesInBuffer = 0;

    BeebRx.Pointer = 0;
    BeebRx.BytesInBuffer = 0;
    BeebTx.Pointer = 0;
    BeebTx.BytesInBuffer = 0;

    fourwaystage = FWS_IDLE;    // used for AUN mode translation stage.

    ADLC.rxfptr = 0;
    ADLC.rxap = 0;
    ADLC.rxffc = 0;
    ADLC.txfptr = 0;
    ADLC.txftl = 0;

    ADLC.idle = 1;
    ADLC.cts = 0;

    irqcause = 0;
    sr1b2cause = 0;

    FlagFillActive = false;
    EconetFlagFillTimeoutTrigger = 0;

    // kill anything that was in use
    if (ReceiverSocketsOpen) {
        if (!confSingleSocket)
            closesocket(SendSocket);
        closesocket(ListenSocket);
        ReceiverSocketsOpen = false;
    }

    // Stop here if not enabled
    if (!EconetEnabled)
        return;

    // Read in econet.cfg.  Done here so can refresh it on Break.
    econet_read_netfile();

    //----------------------
    // Create a SOCKET for listening for incoming connection requests.
    ListenSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ListenSocket == INVALID_SOCKET) {
        log_error("Econet: Failed to open listening socket (error %ld)", WSAGetLastError());
        WSACleanup();
        return;
    }

    //----------------------
    // The sockaddr_in structure specifies the address family,
    // IP address, and port for the socket that is being bound.
    struct sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY;       //inet_addr("127.0.0.1");

    // Already have a station num? Either from command line or a free one
    // we found on previous reset.
    if (EconetStationNumber != 0) {
        // Look up our port number in network config
        for (struct ECOLAN *entry = networks; entry; entry = entry->next) {
            if (entry->station == EconetStationNumber) {
                EconetListenPort = entry->port;
                EconetListenIP = entry->inet_addr.s_addr;
                break;
            }
        }
        if (EconetListenPort != 0) {
            service.sin_port = htons(EconetListenPort);
            service.sin_addr.s_addr = EconetListenIP;
            if (bind(ListenSocket, (SOCKADDR *) & service, sizeof(service)) == SOCKET_ERROR) {
                log_error("Econet: Failed to bind to port %d (error %ld)", EconetListenPort, WSAGetLastError());
                closesocket(ListenSocket);
                WSACleanup();
                return;
            }
        }
        else {
            log_error("Econet: Failed to find station %d in econet.cfg", EconetStationNumber);
            WSACleanup();
            return;
        }

    }
    else {
        // Station number not specified, find first one not already in use.
        char localhost[256];
        struct hostent *hent;
        struct in_addr localaddr;

        // Get localhost IP address
        if (gethostname(localhost, 256) != SOCKET_ERROR && (hent = gethostbyname(localhost)) != NULL) {
            in_addr_t loopback = htonl(INADDR_LOOPBACK);
            // See if configured addresses match local IPs
            for (struct ECOLAN *entry = networks; entry && EconetStationNumber == 0; entry = entry->next) {
                // Check address for each network interface/card
                for (int a = 0; hent->h_addr_list[a] != NULL && EconetStationNumber == 0; ++a) {
                    if (entry->inet_addr.s_addr == loopback || entry->inet_addr.s_addr == *(in_addr_t *)hent->h_addr_list[a]) {
                        service.sin_port = htons(entry->port);
                        service.sin_addr = entry->inet_addr;
                        if (bind(ListenSocket, (SOCKADDR *) & service, sizeof(service)) == 0) {
                            EconetListenPort = entry->port;
                            EconetListenIP = entry->inet_addr.s_addr;
                            EconetStationNumber = entry->station;
                        }
                    }
                }
            }

            if (EconetListenPort == 0) {
                // still can't find one ... strict mode?
                if (confSTRICT && confAUNmode) {
                    log_debug("Econet: No free hosts in table; trying automatic mode..");
                    in_addr_t local = htonl(ntohl(local_ipaddr(localaddr)) & 0xffffff00);
                    for (struct AUNTAB *entry = aunnet; entry && EconetStationNumber == 0; entry = entry->next) {
                        for (int a = 0; hent->h_addr_list[a] != NULL && EconetStationNumber == 0; ++a) {
                            memcpy(&localaddr, hent->h_addr_list[a], sizeof(struct in_addr));
                            if (entry->inet_addr.s_addr == local) {
                                service.sin_port = htons(32768);
                                service.sin_addr.s_addr = local_ipaddr(localaddr);
                                if (bind(ListenSocket, (SOCKADDR *) & service, sizeof(service)) == 0) {
                                    myaunnet = entry;
                                    struct ECOLAN *ecoent = malloc(sizeof(struct ECOLAN));
                                    if (ecoent) {
                                        ecoent->next = networks;
                                        ecoent->inet_addr.s_addr = EconetListenIP = local_ipaddr(localaddr);
                                        ecoent->port = EconetListenPort = 32768;
                                        ecoent->station = EconetStationNumber = local_ipaddr(localaddr) >> 24;
                                        ecoent->network = entry->network;
                                        networks = ecoent;
                                    }
                                    else {
                                        log_error("econet: out of memory for network table");
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                if (EconetStationNumber == 0) {
                    log_error("Econet: Failed to find free station/port to bind to");
                    WSACleanup();
                    return;
                }
            }
        }
        else {
            log_error("Econet: Failed to resolve local IP address");
            WSACleanup();
            return;
        }
    }
    log_debug("Econet: Station number set to %d, port %d", EconetStationNumber, EconetListenPort);

    // On Master the station number is read from CMOS so update it
    if (MASTER)
        cmos_set(0xe, EconetStationNumber);

    //---------------------
    // Socket used to send messages.
    if (confSingleSocket) {
        SendSocket = ListenSocket;
    }
    else {
        SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (SendSocket == INVALID_SOCKET) {
            log_error("Econet: Failed to open sending socket (error %ld)", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return;
        }
    }

    // this call is what allows broadcast packets to be sent:
    int broadcast = 1;
    if (setsockopt(SendSocket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast) == -1) {
        log_error("Econet: Failed to set socket for broadcasts (error %ld)", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return;
    }

    ReceiverSocketsOpen = true;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// read to FE18..

uint8_t econet_read_station(void)
{
    log_debug("Econet: Read Station %02X", (int)EconetStationNumber);
    return (EconetStationNumber);
}


//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// write to FEA0-3

static void econet_tx_copy(int start)
{
    size_t size = BeebTx.Pointer - start;
    memcpy(EconetTx.buff, BeebTx.buff + start, size);
    EconetTx.Pointer = size;
}

static void econet_set_wait4idle(const char *dir, const char *reason)
{
    fourwaystage = FWS_WAIT4IDLE;
    EconetWait4IdleTrigger = 0; //EconetCycles + EconetSCACKtimeout * 3<< 1);
    log_debug("Econet(%s): Set FWS_WAIT4IDLE (%s)", dir, reason);
}

static void econet_tx_data(void)
{
    if (ADLC.txfptr) {
        log_debug("Econet(Tx): Write to FIFO noticed");
        bool TXlast = false;
        if (ADLC.txftl & powers[ADLC.txfptr - 1])
            TXlast = true;      // TxLast set
        if (BeebTx.Pointer + 1 > sizeof(BeebTx.buff) || // overflow IP buffer
            (ADLC.txfptr > 4)) {        // overflowed fifo
            ADLC.status1 |= ADLC_STA1_TX_UNDER; // set tx underrun flag
            BeebTx.Pointer = 0; // wipe buffer
            BeebTx.BytesInBuffer = 0;
            ADLC.txfptr = 0;
            ADLC.txftl = 0;
            log_debug("Econet(Tx): TxUnderun!!");
        }
        else {
            BeebTx.buff[BeebTx.Pointer] = ADLC.txfifo[--ADLC.txfptr];
            BeebTx.Pointer++;
        }
        if (TXlast) {   // TxLast set
            log_debug("Econet(Tx): TXLast set - Send packet to %02x %02x ", (unsigned int)(BeebTx.eh.destnet), (unsigned int)BeebTx.eh.deststn);

            // first two bytes of BeebTx.buff contain the destination address
            // (or one zero byte for broadcast)

            struct sockaddr_in RecvAddr;
            bool SendMe = false;
            int SendLen;
            struct ECOLAN *ecoent = networks;
            if (confAUNmode && (BeebTx.eh.deststn == 255 || BeebTx.eh.deststn == 0)) {  // broadcast!
                // TODO something
                // Somewhere that I cannot now find suggested that
                // aun buffers broadcast packet, and broadcasts a simple flag. stations
                // poll us to get the actual broadcast data ..
                // Hmmm...
                //
                // ok, just send it to the local broadcast address.
                // TODO lookup destnet in aunnet() and use proper ip address!
                RecvAddr.sin_family = AF_INET;
                RecvAddr.sin_port = htons(32768);
                RecvAddr.sin_addr.s_addr = INADDR_BROADCAST;    //((EconetListenIP & 0x00FFFFFF) | 0xFF000000) ;
                SendMe = true;
            }
            else {
                do {
                    // does the packet match this network table entry?
    //                          SendMe = false;
    //                          // check for 0.stn and mynet.stn.
                    // aunnet wont be populted if not in aun mode, but we don't need to not check
                    // it because it won't matter..
                    if ((ecoent->network == (unsigned int)(BeebTx.eh.destnet)
                         || ecoent->network == myaunnet->network) && ecoent->station == (unsigned int)(BeebTx.eh.deststn)) {
                        SendMe = true;
                        break;
                    }
                    ecoent = ecoent->next;
                } while (ecoent);
                // guess address if not found in table
                if (!SendMe && confSTRICT) {    // didn't find it and allowed to guess
                    log_debug("Econet(Tx): Send to unknown host; make assumptions & add entry!");
                    if (BeebTx.eh.destnet == 0 || BeebTx.eh.destnet == myaunnet->network) {
                        ecoent = malloc(sizeof(struct ECOLAN));
                        if (ecoent) {
                            ecoent->next = networks;
                            ecoent->inet_addr.s_addr = htonl(ntohl(myaunnet->inet_addr.s_addr) | (BeebTx.eh.deststn & 0xff));
                            ecoent->port = 32768;        // default AUN port
                            ecoent->network = BeebTx.eh.destnet;
                            ecoent->station = BeebTx.eh.deststn;
                            networks = ecoent;
                            SendMe = true;
                        }
                        else
                            log_error("econet: out of memory for network table");
                    }
                    else {
                        for (struct AUNTAB *aunent = aunnet; aunent; aunent = aunent->next) {
                            if (aunent->network == BeebTx.eh.destnet) {
                                ecoent = malloc(sizeof(struct ECOLAN));
                                if (ecoent) {
                                    ecoent->next = networks;
                                    ecoent->inet_addr.s_addr = htonl(ntohl(myaunnet->inet_addr.s_addr) | (BeebTx.eh.deststn & 0xff));
                                    ecoent->port = 32768;        // default AUN port
                                    ecoent->network = BeebTx.eh.destnet;
                                    ecoent->station = BeebTx.eh.deststn;
                                    SendMe = true;
                                }
                                else
                                    log_error("econet: out of memory for network table");
                                break;
                            }
                        }
                    }
                }

                RecvAddr.sin_family = AF_INET;
                RecvAddr.sin_port = htons(ecoent->port);
                RecvAddr.sin_addr = ecoent->inet_addr;
            }

            // TODO
            log_debug("Econet(Tx): TXLast set - Send %d byte packet to %02x %02x (%s:%u)",
                      BeebTx.Pointer, (unsigned int)(BeebTx.eh.destnet), (unsigned int)BeebTx.eh.deststn, inet_ntoa(RecvAddr.sin_addr), (unsigned int)ntohs(RecvAddr.sin_port));
            log_dump("Econet(Tx): Econet packet: ", BeebTx.buff, BeebTx.Pointer);
    /*                  if (confAUNmode && fourwaystage != FWS_IDLE) {
                if (RecvAddr.sin_port != EconetTx.inet_addr ||
                    RecvAddr.sin_port != htons(EconetTx.port) ) {
                        EconetError("Erm.. trying to send somewhere while in the middle of talking to somewhere else.");
                }
            }
    */
            // Send a datagram to the receiver
            if (confAUNmode) {
                // OK. Lets do AUN ...
                // The beeb has given us a packet .. what is it?
                SendMe = false;
                switch (fourwaystage) {
                    case FWS_SCACKRCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_SCACKRCVD");
                        // it came in response to our ack of a scout
                        // what we have /should/ be the data block ..
                        //CLUDGE WARNING is this a scout sent again immediately?? TODO fix this?!?!
                        if (BeebTx.Pointer != sizeof(BeebTx.eh) || memcmp(BeebTx.buff, BeebTxCopy, sizeof(BeebTx.eh)) != 0) {   // nope
                            econet_tx_copy(4);
                            fourwaystage = FWS_DATASENT;
                            log_debug("Econet(Tx): Set FWS_DATASENT");
                            SendMe = true;
                            SendLen = sizeof(EconetTx.ah) + EconetTx.Pointer;
                            break;
                        }       // else fall through...
                    case FWS_IDLE:
                        log_debug("Econet(Tx): AUN mode, in state FWS_IDLE");
                        // not currently doing anything.
                        memcpy(BeebTxCopy, BeebTx.buff, sizeof(BeebTx.eh));
                        // maybe a long scout or a broadcast
                        EconetTx.ah.cb = (unsigned int)(BeebTx.eh.cb) & 127;    //| 128;
                        EconetTx.ah.port = (unsigned int)(BeebTx.eh.port);
                        EconetTx.ah.pad = 0;
                        EconetTx.ah.handle = (ec_sequence += 4);

                        EconetTx.destnet = BeebTx.eh.destnet | outmask; //30JUN
                        EconetTx.deststn = BeebTx.eh.deststn;
                        econet_tx_copy(6);
                        if (EconetTx.deststn == 255 || EconetTx.deststn == 0) {
                            EconetTx.ah.type = AUN_TYPE_BROADCAST;
                            econet_set_wait4idle("Tx", "broadcast snt");
                            SendMe = true;      // send packet ...
                            SendLen = sizeof(EconetTx.ah) + 8;
                        }
                        else if (EconetTx.ah.port == 0) {
                            EconetTx.ah.type = AUN_TYPE_IMMEDIATE;
                            fourwaystage = FWS_IMMSENT;
                            log_debug("Econet(Tx): Set FWS_IMMSENT");
                            SendMe = true;      // send packet ...
                            SendLen = sizeof(EconetTx.ah) + EconetTx.Pointer;
                        }
                        else {
                            EconetTx.ah.type = AUN_TYPE_UNICAST;
                            fourwaystage = FWS_SCOUTSENT;
                            log_debug("Econet(Tx): Set FWS_SCOUTSENT");
                            // dont send anything but set wait anyway
                            EconetSCACKtrigger = EconetCycles + EconetSCACKtimeout;
                            log_debug("Econet(Tx): SCACKtimer set");
                            FlagFillActive = true;
                            EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                            log_debug("Econet(Tx): FlagFill set (between tx scout and ack)");
                        }
                        break;
                    case FWS_SCOUTRCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_SCOUTRCVD");
                        // it's an ack for a scout which we sent the beeb. just drop it, but move on.
                        fourwaystage = FWS_SCACKSENT;
                        log_debug("Econet(Tx): Set FWS_SCACKSENT");
                        EconetSCACKtrigger = EconetCycles + EconetSCACKtimeout;
                        log_debug("Econet(Tx): SCACKtimer set");
                        FlagFillActive = true;
                        EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                        log_debug("Econet(Tx): FlagFill set (between rx scout ack and data)");
                        break;
                    case FWS_DATARCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_DATARCVD");
                        // this must be ack for data just receved
                        // now we really need to send an ack to the far AUN host...
                        // send header of last block received straight back.
                        // this ought to work, but only because the beeb can only talk to one machine at any time..
                        SendLen = sizeof(EconetRx.ah);
                        EconetTx.ah = EconetRx.ah;
                        EconetTx.ah.type = AUN_TYPE_ACK;
                        SendMe = true;
                        econet_set_wait4idle("Tx", "final ack sent");
                        break;
                    case FWS_IMMRCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_IMMRCVD");
                        // it's a reply to an immediate command we just had
                        econet_set_wait4idle("Tx", "imm rcvd");
                        econet_tx_copy(4);
                        EconetTx.ah = EconetRx.ah;
                        EconetTx.ah.type = AUN_TYPE_IMM_REPLY;
                        SendMe = true;
                        SendLen = sizeof(EconetTx.ah) + EconetTx.Pointer;
                        break;
                    default:
                        econet_set_wait4idle("Tx", "invalid 4-way state");
                }
            }
            if (SendMe) {
                if (confAUNmode) {
                    log_debug("Econet(Tx): Sending an AUN packet, SendLen=%d. type=%u, port=%u, handle=%u", SendLen, EconetTx.ah.type, EconetTx.ah.port, EconetTx.ah.handle);
                    log_dump("Econet(Tx): AUN Packet: ", (uint8_t *)&EconetTx, SendLen);
                    if (sendto(SendSocket, (char *)&EconetTx, SendLen, 0, (SOCKADDR *) &RecvAddr, sizeof(RecvAddr)) == SOCKET_ERROR) {
                        log_error("Econet(Tx): Failed to send packet to %02x %02x (%s:%u)",
                                  (unsigned int)(ecoent->network), (unsigned int)ecoent->station, inet_ntoa(ecoent->inet_addr), (unsigned int)ecoent->port);
                    }
                }
                else {
                    log_debug("Econet(Tx): Sending a non-AUN packet, BeebTx.Pointer=%d", BeebTx.Pointer);
                    log_dump("Econet(Tx): BeebEm Packet: ", BeebTx.buff, BeebTx.Pointer);
                    if (sendto(SendSocket, (char *)BeebTx.buff, BeebTx.Pointer, 0, (SOCKADDR *) &RecvAddr, sizeof(RecvAddr)) == SOCKET_ERROR) {
                        log_error("Econet(Tx): Failed to send packet to %02x %02x (%s:%u)",
                                  (unsigned int)(BeebTx.eh.destnet), (unsigned int)BeebTx.eh.deststn, inet_ntoa(ecoent->inet_addr), (unsigned int)ecoent->port);
                    }
                }

                // Sending packet will mean peer goes into flag fill while
                // it deals with it
                FlagFillActive = true;
                EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                log_debug("Econet(Tx): FlagFill set (packet sent)");

                BeebTx.Pointer = 0;     // wipe buffer
                BeebTx.BytesInBuffer = 0;
                econet_adlc_debug();
            }
        }
    }
}

static void econet_rx_copy(int start, int bytes)
{
    size_t size = bytes - sizeof(EconetRx.ah);
    memcpy(BeebRx.buff + start, EconetRx.buff, size);
    BeebRx.BytesInBuffer = size + start;
}

static void econet_rx_data(void)
{
    if (BeebRx.Pointer < BeebRx.BytesInBuffer) {
        // something waiting to be given to the processor
        if (ADLC.rxfptr < 3) {  // space in fifo
            log_debug("Econet(Rx): Time to give another byte to the beeb");
            ADLC.rxfifo[2] = ADLC.rxfifo[1];
            ADLC.rxfifo[1] = ADLC.rxfifo[0];
            ADLC.rxfifo[0] = BeebRx.buff[BeebRx.Pointer];
            ADLC.rxfptr++;
            ADLC.rxffc = (ADLC.rxffc << 1) & 7;
            ADLC.rxap = (ADLC.rxap << 1) & 7;
            if (BeebRx.Pointer == 0)
                ADLC.rxap |= 1; // 2 bytes? adr extention mode
            BeebRx.Pointer++;
            if (BeebRx.Pointer >= BeebRx.BytesInBuffer) {       // that was last byte!
                ADLC.rxffc |= 1;        // set FV flag (this was last byte of frame)
                BeebRx.Pointer = 0;     // Reset read for next packet
                BeebRx.BytesInBuffer = 0;
            }
        }
    }

    if (ADLC.rxfptr == 0) {
        unsigned int j = 0;

        // still nothing in buffers (and thus nothing in Econetrx buffer)
        ADLC.control1 &= ~ADLC_CTL1_RX_DISC;   // reset discontinue flag

        // wait for cpu to clear FV flag from last frame received
        if (!(ADLC.status2 & ADLC_STA2_FRAME_VAL)) {

            if (!confAUNmode || fourwaystage == FWS_IDLE || fourwaystage == FWS_IMMSENT || fourwaystage == FWS_DATASENT) {
                // Try and get another packet from network
                // Check if packet is waiting without blocking
                int RetVal;
                fd_set RdFds;
                struct timeval TmOut = { 0, 0 };
                FD_ZERO(&RdFds);
                FD_SET(ListenSocket, &RdFds);
                RetVal = select((int)ListenSocket + 1, &RdFds, NULL, NULL, &TmOut);
                if (RetVal > 0) {
                    struct sockaddr_in RecvAddr;
                    // Read the packet
                    int sizRcvAdr = sizeof(RecvAddr);
                    if (confAUNmode) {
                        RetVal = recvfrom(ListenSocket, (char *)EconetRx.raw, sizeof(EconetRx), 0, (SOCKADDR *) & RecvAddr, (socklen_t *)&sizRcvAdr);
                        EconetRx.BytesInBuffer = RetVal;
                    }
                    else {
                        RetVal = recvfrom(ListenSocket, (char *)BeebRx.buff, sizeof(BeebRx.buff), 0, (SOCKADDR *) & RecvAddr, (socklen_t *)&sizRcvAdr);
                    }
                    if (RetVal > 0) {
                        log_debug("Econet(Rx): Packet received, %u bytes from %s:%u", (int)RetVal, inet_ntoa(RecvAddr.sin_addr), htons(RecvAddr.sin_port));
                        if (confAUNmode) {
                            log_dump("Econet(Rx): AUN packet: ", EconetRx.raw, RetVal);

                            // convert from AUN format
                            // find station number of sender
                            struct ECOLAN *host;
                            bool foundhost = false;
                            for (host = networks; host; host = host->next) {
                                if (RecvAddr.sin_port == htons(host->port) && RecvAddr.sin_addr.s_addr == host->inet_addr.s_addr) {
                                    foundhost = true;
                                    break;
                                }
                            }
                            if (!foundhost) {
                                // packet from unknown host
                                if (confLEARN) {
                                    uint8_t station = ntohl(host->inet_addr.s_addr) & 0xff;
                                    uint8_t network = 0;
                                    for (struct AUNTAB *aunent = aunnet; aunent; aunent = aunent->next) {
                                        if (aunent->inet_addr.s_addr == RecvAddr.sin_addr.s_addr) {
                                            network = aunent->network;
                                            break;
                                        }
                                    }
                                    log_debug("Econet(Rx): Previusly unknown IP, Econet %u:%u", network, station);
                                    for (struct ECOLAN *entry = networks; entry; entry = entry->next) {
                                        if (entry->network == network && entry->station == station) {
                                            host = entry;
                                            break;
                                        }
                                    }
                                    if (host) {
                                        log_debug("Econet(Rx): updating existing entry %s:%u", inet_ntoa(host->inet_addr), host->port);
                                        host->port = ntohs(RecvAddr.sin_port);
                                        host->inet_addr = RecvAddr.sin_addr;
                                        foundhost = true;
                                    }
                                    else {
                                        host = malloc(sizeof(struct ECOLAN));
                                        if (host) {
                                            host->next = networks;
                                            host->port = ntohs(RecvAddr.sin_port);
                                            host->inet_addr = RecvAddr.sin_addr;
                                            host->station = station;
                                            host->network = network;
                                            log_debug("Econet(Rx): adding new entry %u:%u -> %s:%u", network, station, inet_ntoa(host->inet_addr), host->port);
                                            foundhost = true;
                                        }
                                        else
                                            log_error("econet: out of memory for network table");
                                    }
                                }
                                else {
                                    // ignore it..
                                    log_debug("Econet(Rx): Packet ignored");
                                }
                            }

                            if (!foundhost) {   // didn't find it in the table ..
                                BeebRx.BytesInBuffer = 0;       //ignore the packet
                            }
                            else {
                                log_debug("Econet(Rx): Packet was from %02x %02x, type=%u, port=%u, handle=%u", (unsigned int)(host->network), (unsigned int)host->station, EconetRx.ah.type, EconetRx.ah.port, EconetRx.ah.handle);
                                // TODO - many of these copies can use memcpy()
                                switch (fourwaystage) {
                                    case FWS_IDLE:
                                        log_debug("Econet(Rx): received in FWS_IDLE");
                                        // we weren't doing anything when this packet came in.
                                        BeebRx.eh.srcstn = host->station;
                                        BeebRx.eh.srcnet = host->network;

                                        BeebRx.eh.deststn = EconetStationNumber;        // must be for us.
                                        BeebRx.eh.destnet = 0;
//                                          BeebRx.eh.deststn = EconetRx.eh.deststn ; // 30jun was EconetStationNumber; // must be for us.
//                                          BeebRx.eh.destnet = EconetRx.eh.destnet & inmask ; // 30jun was 0

                                        BeebRx.eh.cb = EconetRx.ah.cb | 128;
                                        BeebRx.eh.port = EconetRx.ah.port;
                                        switch (EconetRx.ah.type) {

                                            case AUN_TYPE_BROADCAST:
                                                BeebRx.eh.deststn = 255;        // wasn't just for us..
                                                BeebRx.eh.destnet = 0;  // TODO check if not net 0.. does it make a difference?
                                                econet_rx_copy(6, RetVal);
                                                econet_set_wait4idle("Rx", "broadcast received");
                                                break;
                                            case AUN_TYPE_IMMEDIATE:
                                                econet_rx_copy(6, RetVal);
                                                fourwaystage = FWS_IMMRCVD;
                                                log_debug("Econet(Rx): Set FWS_IMMRCVD");
                                                break;
                                            case AUN_TYPE_UNICAST:
                                                // we're assuming things here..
                                                fourwaystage = FWS_SCOUTRCVD;
                                                log_debug("Econet(Rx): Set FWS_SCOUTRCVD");
                                                BeebRx.BytesInBuffer = sizeof(BeebRx.eh);
                                                break;
                                            default:
                                                //ignore anything else
                                                BeebRx.BytesInBuffer = 0;
                                        }
                                        BeebRx.Pointer = 0;
                                        break;
                                    case FWS_IMMSENT:  // it should be reply to an immediate instruction
                                        log_debug("Econet(Rx): received in FWS_IMMSENT");
                                        // TODO  check that it is!!!   Example scenario where it will not
                                        // be - *STATIONs poll sends packet to itself... packet we get
                                        // here is the one we just sent out..!!!
                                        // I'm pretty sure that real econet can't send to itself..
                                        BeebRx.eh.srcstn = host->station;
                                        BeebRx.eh.srcnet = host->network;
                                        BeebRx.eh.deststn = EconetStationNumber;        // must be for us.
                                        BeebRx.eh.destnet = 0;
                                        econet_rx_copy(4, RetVal);
                                        BeebRx.Pointer = 0;
                                        econet_set_wait4idle("Rx", "ack received from remote AUN server");
                                        break;
                                    case FWS_DATASENT:
                                        log_debug("Econet(Rx): received in FWS_DATASENT");
                                        // we sent block of data, awaiting final ack..
                                        if (EconetRx.ah.type == AUN_TYPE_ACK || EconetRx.ah.type == AUN_TYPE_NACK) {
                                            // are we expecting a (N)ACK ?
                                            // TODO check it is a (n)ack for packet we just sent!!, deal with naks!
                                            // construct a final ack for the beeb
                                            BeebRx.eh.srcstn = host->station;
                                            BeebRx.eh.srcnet = host->network;
                                            BeebRx.eh.deststn = EconetStationNumber;    // must be for us.
                                            BeebRx.eh.destnet = 0;
//                                              BeebRx.eh.deststn = EconetRx.eh.deststn ; // 30jun was EconetStationNumber; // must be for us.
//                                              BeebRx.eh.destnet = EconetRx.eh.destnet & inmask ; // 30jun was 0

                                            BeebRx.BytesInBuffer = 4;
                                            BeebRx.Pointer = 0;
                                            econet_set_wait4idle("Rx", "aun ack rxd");
                                            break;
                                        }       // else unexpected packet - ignore it.TODO: queue it?
                                    default:   // erm, what are we doing here?
                                        econet_set_wait4idle("Rx", "unexpected 4-way state, packet ignored");
                                        break;
                                }
                            }
                        }
                        else {
                            log_dump("Econet(Rx): BeebEm packet: ", BeebRx.buff, RetVal);
                            BeebRx.BytesInBuffer = RetVal;
                            BeebRx.Pointer = 0;
                        }

                        if ((BeebRx.eh.deststn == EconetStationNumber || BeebRx.eh.deststn == 255 || BeebRx.eh.deststn == 0) && BeebRx.BytesInBuffer > 0) {
                            // Peer sent us packet - no longer in flag fill
                            FlagFillActive = false;
                            log_debug("Econet(Rx): FlagFill reset");
                        }
                        else {
                            // Two other stations communicating - assume one of them will flag fill
                            FlagFillActive = true;
                            EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                            log_debug("Econet(Rx): FlagFill set - other station comms");
                        }
                    }
                    else if (RetVal == SOCKET_ERROR && !confSingleSocket)
                        log_error("Econet(Rx): Failed to receive packet (error %ld)", WSAGetLastError());
                }
                else if (RetVal == SOCKET_ERROR)
                    log_error("Econet(Rx): Failed to check for new packet");
            }

            // this bit fakes the bits of the 4-way handshake that AUN doesn't do.
            if (confAUNmode && EconetSCACKtrigger <= EconetCycles) {
                switch (fourwaystage) {
                    case FWS_SCOUTSENT:
                        // just got a scout from the beeb, fake an acknowledgement.
                        BeebRx.eh.deststn = EconetStationNumber;
                        BeebRx.eh.destnet = 0;
                        BeebRx.eh.srcstn = EconetTx.deststn;    // use scout's dest as source of ack.
                        BeebRx.eh.srcnet = EconetTx.destnet;    // & inmask; //30jun

                        BeebRx.BytesInBuffer = 4;
                        BeebRx.Pointer = 0;
                        fourwaystage = FWS_SCACKRCVD;
                        log_debug("Econet(Rx): Set FWS_SCACKRCVD, faked packet follows");
                        FlagFillActive = false;
                        break;
                    case FWS_SCACKSENT:
                        // beeb acked the scout we gave it, so give it the data AUN sent us earlier.
                        BeebRx.eh.deststn = EconetStationNumber;        // as it is data it must be for us
                        BeebRx.eh.destnet = 0;
//                          BeebRx.eh.deststn = EconetRx.eh.deststn ; // 30jun
//                          BeebRx.eh.destnet = EconetRx.eh.destnet & inmask ; // 30jun was 0

                        BeebRx.eh.srcstn = EconetTx.deststn;    //30jun dont think this is right..
                        BeebRx.eh.srcnet = EconetTx.destnet & inmask;
                        j = 4;
                        for (unsigned int i = 0; i < EconetRx.BytesInBuffer - sizeof(EconetRx.ah); i++) {
                            BeebRx.buff[j] = EconetRx.buff[i];
                            j++;
                        }
                        BeebRx.BytesInBuffer = j;
                        BeebRx.Pointer = 0;
                        fourwaystage = FWS_DATARCVD;
                        log_debug("Econet(Rx): Set FWS_DATARCVD, real packet follows");
                        FlagFillActive = false;
                        break;
                    default:
                        break;
                }
            }
            if (BeebRx.BytesInBuffer > 0)
                log_dump("Econet(Rx): Econet packet: ", BeebRx.buff, BeebRx.BytesInBuffer);
        }
    }
}

static void econet_rx_tx(void)
{
    // Only do this bit occasionally as data only comes in from
    // line occasionally.
    // Trickle data between fifo registers and ip packets.

    // Transmit data
    if (!(ADLC.control1 & ADLC_CTL1_TX_RESET))    // tx reset off
        econet_tx_data();
    // Receive data
    if (!(ADLC.control1 & ADLC_CTL1_RX_RESET))    // rx reset off
        econet_rx_data();

    // Update idle status
    if (!(ADLC.control1 & ADLC_CTL1_RX_RESET)     // not rxreset
        && !ADLC.rxfptr     // nothing in fifo
        && !(ADLC.status2 & ADLC_STA2_FRAME_VAL)      // no FV
        && (BeebRx.BytesInBuffer == 0)) {   // nothing in ip buffer
        ADLC.idle = true;
    }
    else {
        ADLC.idle = false;
    }

    // Reset pseudo flag fill?
    if (EconetFlagFillTimeoutTrigger <= EconetCycles && FlagFillActive) {
        FlagFillActive = false;
        log_debug("Econet: FlagFill timeout reset");
    }
}

static void econet_update_tail(void)
{
    //waiting for AUN to become idle?
    if (confAUNmode && fourwaystage == FWS_WAIT4IDLE && BeebRx.BytesInBuffer == 0 && ADLC.rxfptr == 0 && ADLC.txfptr == 0) {
        if (EconetWait4IdleTrigger == 0) {
            log_debug("Econet: setting WAIT4IDLE timeout");
            EconetWait4IdleTrigger = EconetCycles + EconetWait4IdleTimeout;
        }
        else if (EconetWait4IdleTrigger <= EconetCycles) {
            log_debug("Econet: wait over, returning to FWS_IDLE");
            fourwaystage = FWS_IDLE;
            Econet4Wtrigger = 0;
            EconetWait4IdleTrigger = 0;
            FlagFillActive = false;
        }
    }

    // timeout four way handshake - for when we get lost..
    if (Econet4Wtrigger == 0) {
        if (fourwaystage != FWS_IDLE)
            Econet4Wtrigger = EconetCycles + FourWayStageTimeout;
    }
    else if (Econet4Wtrigger <= EconetCycles) {
        EconetSCACKtrigger = 0;
        EconetWait4IdleTrigger = 0;
        Econet4Wtrigger = 0;
        fourwaystage = FWS_IDLE;
        log_debug("Econet: 4waystage timeout; Set FWS_IDLE");
        econet_adlc_debug();
    }

    //--------------------------------------------------------------------------------------------
    // Status bits need changing?

    // SR1b0 - RDA - received data available.
    if (!(ADLC.control1 & ADLC_CTL1_RX_RESET)) {                          // rx reset off
        if (ADLC.rxfptr && (ADLC.rxffc & (powers[ADLC.rxfptr - 1])))
            ADLC.status2 |= ADLC_STA2_FRAME_VAL;
        if ((ADLC.rxfptr && !(ADLC.control2 & ADLC_CTL2_2BYTE))           // 1 byte mode
            || ((ADLC.rxfptr > 1) && (ADLC.control2 & ADLC_CTL2_2BYTE)))  // 2 byte mode
        {
            ADLC.status1 |= ADLC_STA1_RDA;  // set RDA copy
            ADLC.status2 |= ADLC_STA2_RDA;
        }
        else {
            ADLC.status1 &= ~ADLC_STA1_RDA;
            ADLC.status2 &= ~ADLC_STA2_RDA;
        }
    }
    // SR1b1 - S2RQ - set after SR2, see below
    // SR1b2 - LOOP - set if in loop mode. not supported in this emulation,
    // SR1b3 - FD - Flag detected. Hmm.
    if (FlagFillActive)
        ADLC.status1 |= ADLC_STA1_FLAG_DET;
    else
        ADLC.status1 &= ~ADLC_STA1_FLAG_DET;

    // SR1b4 - CTS - set by ~CTS line going up, and causes IRQ if enabled.
    //              only cleared by cpu.
    //          ~CTS is a NAND of DCD(clock present)(high if valid)
    //           &  collission detection!
    //          i.e. it's low (thus clear to send) when we have both DCD(clock)
    //          present AND no collision on line and no collision.
    //          cts will ALSO be high if there is no cable!
    //      we will only bother checking against DCD here as can't have collisions.
    //      but nfs then loops waiting for CTS high!
    //  on the B+ there is (by default) no collission detection circuitary. instead S29
    // links RTS in it's place, thus CTS is a NAND of not RTS & not DCD
    // i.e. cts = ! ( !rts && !dcd ) all signals are active low.
    // there is a delay on rts going high after cr2b7=0 - ignore this for now.
    // cr2b7 = 1 means RTS low means not rts high means cts low
    // sockets true means dcd low means not dcd high means cts low
    // doing it this way finally works !!  great :-) :-)

    if (ReceiverSocketsOpen && (ADLC.control2 & ADLC_CTL2_RTS)) { // clock + RTS
        ADLC.cts = false;
        ADLC.status1 &= ~ADLC_STA1_NOT_CTS;
    }
    else {
        ADLC.cts = true;
    }

    // and then set the status bit if the line is high! (status bit stays
    // up until cpu tries to clear it) (& still stays up if cts line still high)

    if (!(ADLC.control1 && ADLC_CTL2_RTS) && ADLC.cts) {
        ADLC.status1 |= ADLC_STA1_NOT_CTS;     // set CTS now
    }

    // SR1b5 - TXU - Tx Underrun.
    if (ADLC.txfptr > 4) {      // probably not needed
        log_debug("Econet: TX Underrun - TXfptr %02x", (unsigned int)ADLC.txfptr);
        ADLC.status1 |= ADLC_STA1_TX_UNDER;
        ADLC.txfptr = 4;
    }

    // SR1b6 TDRA flag - another complicated derivation
    if (!(ADLC.control1 & ADLC_CTL1_TX_RESET)) {       // not txreset
        if (!(ADLC.control2 & ADLC_CTL2_FR_COMP)) {     // tdra mode
            if ((((ADLC.txfptr < 3) && !(ADLC.control2 & ADLC_CTL2_2BYTE))    // space in fifo?
                 || ((ADLC.txfptr < 2) && (ADLC.control2 & ADLC_CTL2_2BYTE))) // space in fifo?
                && (!(ADLC.status1 & ADLC_STA1_NOT_CTS))                      // clear to send is ok
                && (!(ADLC.status2 & ADLC_STA2_NOT_DCD))) {                   // DTR not high

                if (!(ADLC.status1 & ADLC_STA1_TDRAFC)) {
                    log_debug("ADLC: set tdra");
                    ADLC.status1 |= ADLC_STA1_TDRAFC;     // set Tx Reg Data Available flag.
                }
            }
            else {
                if (ADLC.status1 & ADLC_STA1_TDRAFC) {
                    log_debug("ADLC: clear tdra");
                    ADLC.status1 &= ~ADLC_STA1_TDRAFC;    // clear Tx Reg Data Available flag.
                }
            }
        }
        else {                          // FC mode
            if (!(ADLC.txfptr)) {       // nothing in fifo
                if (!(ADLC.status1 & ADLC_STA1_TDRAFC)) {
                    log_debug("ADLC: set fc");
                    ADLC.status1 |= ADLC_STA1_TDRAFC;     // set Tx Reg Data Available flag.
                }
            }
            else {
                if (ADLC.status1 & ADLC_STA1_TDRAFC) {
                    log_debug("ADLC: clear fc");
                    ADLC.status1 &= ~ADLC_STA1_TDRAFC;    // clear Tx Reg Data Available flag.
                }
            }
        }
    }
    // SR1b7 IRQ flag - see below

    // SR2b0 - AP - Address present
    if (!(ADLC.control1 & ADLC_CTL1_RX_RESET)) {        // not rxreset
        if (ADLC.rxfptr && (ADLC.rxap & (powers[ADLC.rxfptr - 1]))) {   // ap bits set on fifo
            ADLC.status2 |= ADLC_STA2_ADDR_PRES;
        }
        else {
            ADLC.status2 &= ~ADLC_STA2_ADDR_PRES;
        }
        // SR2b1 - FV -Frame Valid - set in rx - only reset by ClearRx or RxReset
        /* if (ADLC.rxfptr && (ADLC.rxffc & (powers[ADLC.rxfptr - 1]))) {
            ADLC.status2 |= ADLC_STA2_FRAME_VAL;
        } */
        // SR2b2 - Inactive Idle Received - sets irq!
        if (ADLC.idle && !FlagFillActive)
            ADLC.status2 |= ADLC_STA2_INAC_IDLE;
        else
            ADLC.status2 &= ~ADLC_STA2_INAC_IDLE;
    }
    // SR2b3 - RxAbort - Abort received - set in rx routines above
    // SR2b4 - Error during reception - set if error flaged in rx routine.
    // SR2b5 - DCD
    if (!ReceiverSocketsOpen) { // is line down?
        ADLC.status2 |= ADLC_STA2_NOT_DCD;     // flag error
    }
    else {
        ADLC.status2 &= ~ADLC_STA2_NOT_DCD;
    }
    // SR2b6 - OVRN -receipt overrun. probably not needed
    if (ADLC.rxfptr > 4) {
        ADLC.status2 |= ADLC_STA2_RX_OVER;
        ADLC.rxfptr = 4;
    }
    // SR2b7 - RDA. As per SR1b0 - set above.

    // Handle PSE - only for SR2 Rx bits at the moment
    int sr2psetemp = ADLC.sr2pse;
    if (ADLC.control2 & ADLC_CTL2_PSE) {
        if ((ADLC.sr2pse <= 1) && (ADLC.status2 & (ADLC_STA2_FRAME_VAL|ADLC_STA2_ABORT|ADLC_STA2_FCS_ERR|ADLC_STA2_NOT_DCD|ADLC_STA2_RX_OVER))) {      // ERR, FV, DCD, OVRN, ABT
            ADLC.sr2pse = 1;
            ADLC.status2 &= ~(ADLC_STA2_ADDR_PRES|ADLC_STA2_INAC_IDLE|ADLC_STA2_RDA);
        }
        else if ((ADLC.sr2pse <= 2) && (ADLC.status2 & ADLC_STA2_INAC_IDLE)) { // Idle
            ADLC.sr2pse = 2;
            ADLC.status2 &= ~(ADLC_STA2_ADDR_PRES|ADLC_STA2_RDA);
        }
        else if ((ADLC.sr2pse <= 3) && (ADLC.status2 & ADLC_STA2_ADDR_PRES)) { // AP
            ADLC.sr2pse = 3;
            ADLC.status2 &= ~ADLC_STA2_RDA;
        }
        else if (ADLC.status2 & ADLC_STA2_RDA) { // RDA
            ADLC.sr2pse = 4;
            ADLC.status2 &= ~ADLC_STA2_FRAME_VAL;
        }
        else {
            ADLC.sr2pse = 0;    // No relevant bits set
        }

        // Set SR1 RDA copy
        if (ADLC.status2 & ADLC_STA2_RDA)
            ADLC.status1 |= ADLC_STA1_RDA;
        else
            ADLC.status1 &= ~ADLC_STA1_RDA;

    }
    else {                      // PSE inactive
        ADLC.sr2pse = 0;
    }
    if (sr2psetemp != ADLC.sr2pse)
        log_debug("ADLC: PSE SR2Rx priority changed to %d", ADLC.sr2pse);

    // Do we need to flag an interrupt?
    if (ADLC.status1 != old_status1 || ADLC.status2 != old_status2) { // something changed
        uint8_t tempcause, temp2;

        // SR1b1 - S2RQ - Status2 request. New bit set in S2?
        tempcause = ((ADLC.status2 ^ old_status2) & ADLC.status2) & ~ADLC_STA2_RDA;

        if (!(ADLC.control1 & ADLC_CTL1_RIE)) {     // RIE not set,
            tempcause = 0;
        }

        if (tempcause) {        //something got set
            ADLC.status1 |= ADLC_STA1_S2RR;
            sr1b2cause = sr1b2cause | tempcause;
        }
        else if (!(ADLC.status2 & sr1b2cause)) {        //cause has gone
            ADLC.status1 &= ~ADLC_STA1_S2RR;
            sr1b2cause = 0;
        }

        // New bit set in S1?
        tempcause = ((ADLC.status1 ^ old_status1) & ADLC.status1) & ~ADLC_STA1_IRQ;

        if (!(ADLC.control1 & ADLC_CTL1_RIE)) {     // RIE not set,
            tempcause = tempcause & ~11;
        }
        if (!(ADLC.control1 & ADLC_CTL1_TIE)) {     // TIE not set,
            tempcause = tempcause & ~0x70;
        }

        if (tempcause) {        //something got set
            irqcause = irqcause | tempcause;    // remember which bit went high to flag irq
            // SR1b7 IRQ flag
            ADLC.status1 |= ADLC_STA1_IRQ;
            log_debug("ADLC: Status1 bit got set %02x, interrupt", (int)tempcause);
        }

        // Bit cleared in S1?
        temp2 = ((ADLC.status1 ^ old_status1) & old_status1) & ~ADLC_STA1_IRQ;
        if (temp2) {            // something went off
            irqcause = irqcause & ~temp2;       // clear flags that went off
            if (irqcause == 0) {        // all flag gone off now
                // clear irq status bit when cause has gone.
                ADLC.status1 &= ~ADLC_STA1_IRQ;
                log_debug("ADLC: IRQ cause reset, irqcause %02x", (int)temp2);
            }
        }
        econet_adlc_debug();
    }
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
// Optimisation - only call real poll routine when something has changed

void econet_poll(void)
{
    /* This timeout scheme is copied from BeebEm where it has central
     * support within the emulator.  It is foreign to B-Em, though, so
     * is implemented only within this module.
     */
    EconetCycles += 1;
    if (EconetCycles >= LONG_MAX) {
        /* This handles the counters wrapping around.  As they are
         * unsigned and we're checking against the MAX for the signed
         * type we catch the wrap with plenty of headroom
         */
        log_debug("Econet: wrap timing counters");
        EconetCycles -= LONG_MAX;
        if (EconetSCACKtrigger)
            EconetSCACKtrigger -= LONG_MAX;
        if (EconetFlagFillTimeoutTrigger)
            EconetFlagFillTimeoutTrigger -= LONG_MAX;
        if (Econet4Wtrigger)
            Econet4Wtrigger -= LONG_MAX;
    }
    // Don't poll if failed to init sockets
    if (ReceiverSocketsOpen) {
        old_status1 = ADLC.status1;
        old_status2 = ADLC.status2;
        econet_rx_tx();
        econet_update_tail();
    }
    if (EconetNMIenabled && ADLC.status1 & ADLC_STA1_IRQ && !(nmi & 0x04)) {
        nmi |= 0x04;
        log_debug("Econet: NMI raised");
    }
    else
        nmi &= ~0x04;
}

static uint8_t econet_read_rxreg(void)
{
    if ((ADLC.control1 & ADLC_CTL1_RX_RESET) == 0) {       // rxreset not set
        if (ADLC.rxfptr) {
            uint8_t value = ADLC.rxfifo[--ADLC.rxfptr];
            old_status1 = ADLC.status1;
            old_status2 = ADLC.status2;
            econet_update_tail();
            log_debug("ADLC: read register Receive Data, returned fifo: %02X", value);
            return value;
        }
        else
            log_debug("ADLC: read register Receive Data when FIFO is empty");
    }
    else
        log_debug("ADLC: read register Receive Data when receiver in reset state");
    return 0;
}

uint8_t econet_read_register(uint8_t addr)
{
    econet_adlc_debug();

    switch (addr & 0x03) {
        case 0:
            log_debug("ADLC: read register Status1");
            return ADLC.status1;
        case 1:
            log_debug("ADLC: read register Status2");
            return ADLC.status2;
        default:
            return econet_read_rxreg();
    }
}

static void econet_write_txreg(uint8_t value, bool last)
{
    old_status1 = ADLC.status1;
    old_status2 = ADLC.status2;
    if ((ADLC.control1 & ADLC_CTL1_TX_RESET) == 0) {
        ADLC.txfifo[2] = ADLC.txfifo[1];
        ADLC.txfifo[1] = ADLC.txfifo[0];
        ADLC.txfifo[0] = value;
        ADLC.txfptr++;
        ADLC.txftl = ADLC.txftl << 1;       /// shift txlast bits up.
        if (last)
            ADLC.txftl |= 1;        // set b0 - flag for fifo[0]
    }
}

static void econet_rx_clear(void)
{
    ADLC.control2 &= ~ADLC_CTL2_RX_CLEAR; // clear this bit
    ADLC.status1 &= ~(ADLC_STA1_S2RR|ADLC_STA1_FLAG_DET);  // clear sr2rq, FD
    ADLC.status2 &= ~(ADLC_STA2_FRAME_VAL|ADLC_STA2_INAC_IDLE|ADLC_STA2_ABORT|ADLC_STA2_FCS_ERR|ADLC_STA2_NOT_DCD|ADLC_STA2_RX_OVER);  // clear FV, RxIdle, RxAbt, Err, OVRN, DCD

    if ((ADLC.control2 & ADLC_CTL2_PSE) && ADLC.sr2pse) {       // PSE active?
        ADLC.sr2pse++;      // Advance PSE to next priority
        if (ADLC.sr2pse > 4)
            ADLC.sr2pse = 0;
    }
    else {
        ADLC.sr2pse = 0;
    }

    sr1b2cause = 0;         // clear cause of sr2b1 going up
    if (ADLC.control1 & ADLC_CTL1_RX_RESET) {     // rx reset,clear buffers.
        BeebRx.Pointer = 0;
        BeebRx.BytesInBuffer = 0;
        ADLC.rxfptr = 0;
        ADLC.rxap = 0;
        ADLC.rxffc = 0;
        ADLC.sr2pse = 0;
    }
}

static void econet_tx_clear(void)
{
    ADLC.control2 &= ~ADLC_CTL2_TX_CLEAR;   // clear this bit
    ADLC.status1 &= ~(ADLC_STA1_NOT_CTS|ADLC_STA1_TX_UNDER|ADLC_STA1_TDRAFC);  // clear TXU , cts, TDRA/FC
    if (ADLC.cts) {
        ADLC.status1 |= ADLC_STA1_NOT_CTS;      //cts follows signal, reset high again
        old_status1 |= ADLC_STA1_NOT_CTS;  // don't trigger another interrupt instantly
    }
    if (ADLC.control1 & ADLC_CTL1_TX_RESET) {      // tx reset,clear buffers.
        BeebTx.Pointer = 0;
        BeebTx.BytesInBuffer = 0;
        ADLC.txfptr = 0;
        ADLC.txftl = 0;
    }
}

static bool econet_write_ctl1(uint8_t value)
{
    uint8_t changes = value ^ ADLC.control1;
    if (changes) {
        uint8_t raised  = changes & value;
        log_debug("ADLC: write register Control1=%02X, changes=%02X, raised=%02X", value, changes, raised);
        ADLC.control1 = value;
        old_status1 = ADLC.status1;
        old_status2 = ADLC.status2;
        if (raised & ADLC_CTL1_RX_DISC) {
            log_debug("EconetPoll: RxABORT is set");
            BeebRx.Pointer = 0;
            BeebRx.BytesInBuffer = 0;
            ADLC.rxfptr = 0;
            ADLC.rxap = 0;
            ADLC.rxffc = 0;
            ADLC.control1 &= ~ADLC_CTL1_RX_DISC;   // reset flag
            fourwaystage = FWS_IDLE;
        }
        if (raised & ADLC_CTL1_RX_RESET)
            econet_rx_clear();
        if (raised & ADLC_CTL1_TX_RESET)
            econet_tx_clear();
        return (changes & ~ADLC_CTL1_AC);
    }
    else {
        log_debug("ADLC: write register Control1=%02X (unchanged)", value);
        return false;
    }
}

static bool econet_write_ctl2(uint8_t value)
{
    uint8_t changes = value ^ ADLC.control1;
    if (changes) {
        uint8_t raised  = changes & value;
        log_debug("ADLC: write register Control2=%02X, changes=%02X, raised=%02X", value, changes, raised);
        ADLC.control2 = value;
        old_status1 = ADLC.status1;
        old_status2 = ADLC.status2;
        if (raised & ADLC_CTL2_TX_LAST) {
            ADLC.txftl |= 1;             // set b0 - flag for fifo[0]
            ADLC.control2 &= ~ADLC_CTL2_TX_LAST; // clear flag.
        }
        if (raised & ADLC_CTL2_RX_CLEAR)
            econet_rx_clear();
        if (raised & ADLC_CTL2_TX_CLEAR)
            econet_tx_clear();
        return true;
    }
    else {
        log_debug("ADLC: write register Control2=%02X (unchanged)", value);
        return false;
    }
}

static bool econet_write_ctl3(uint8_t value)
{
    uint8_t changes = value ^ ADLC.control1;
    if (changes) {
        uint8_t raised  = changes & value;
        log_debug("ADLC: write register Control3=%02X, changes=%02X, raised=%02X", value, changes, raised);
    }
    else
        log_debug("ADLC: write register Control3=%02X (unchanged)", value);
    return false;
}

static bool econet_write_ctl4(uint8_t value)
{
    uint8_t changes = value ^ ADLC.control1;
    if (changes) {
        uint8_t raised  = changes & value;
        log_debug("ADLC: write register Control4=%02X, changes=%02X, raised=%02X", value, changes, raised);
        old_status1 = ADLC.status1;
        old_status2 = ADLC.status2;
        if (raised & ADLC_CTL4_TX_ABORT) {
            log_debug("EconetPoll: TxABORT is set");
            ADLC.txfptr = 0;        //  reset fifo
            ADLC.txftl = 0;         //  reset fifo flags
            BeebTx.Pointer = 0;
            BeebTx.BytesInBuffer = 0;
            value &= ~ADLC_CTL4_TX_ABORT;   // reset flag.
            fourwaystage = FWS_IDLE;
            log_debug("Econet: Set FWS_IDLE (abort)");
        }
        ADLC.control4 = value;
        return true;
    }
    else {
        log_debug("ADLC: write register Control4=%02X (unchanged)", value);
        return false;
    }
}

void econet_write_register(uint8_t addr, uint8_t Value)
{
    bool changed = true;
    switch (addr & 0x03) {
        case 0:
            changed = econet_write_ctl1(Value);
            break;
        case 1:
            if (ADLC.control1 & ADLC_CTL1_AC)
                changed = econet_write_ctl3(Value);
            else
                changed = econet_write_ctl2(Value);
            break;
        case 2:
            log_debug("ADLC: write register Transmit Data Continue=%02X", Value);
            econet_write_txreg(Value, false);
            break;
        case 3:
            if (ADLC.control1 & ADLC_CTL1_AC)
                changed = econet_write_ctl4(Value);
            else {
                log_debug("ADLC: write register Transmit Data Finish=%02X", Value);
                econet_write_txreg(Value, true);
            }
            break;
    }
    if (changed)
        econet_update_tail();
}
