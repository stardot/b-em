/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2004  Rob O'Donnell
Copyright (C) 2005  Mike Wyatt

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public
License along with this program; if not, write to the Free
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA  02110-1301, USA.
****************************************************************/

// Econet for BeebEm
// Written by Rob O'Donnell. robert@irrelevant.com
// Mike Wyatt - further development, Dec 2005

#ifndef ECONET_HEADER
#define ECONET_HEADER

// Emulated ADLC control registers.
// control1_b0 is AC
// this splits register address 0x01 as control2 and control3
// and register address 0x03 as tx-data-last-data and control4
struct MC6854 {
    uint8_t control1;
    uint8_t control2;
    uint8_t control3;
    uint8_t control4;
    uint8_t txfifo[3];
    uint8_t rxfifo[3];
    uint8_t txfptr;       // first empty byte in fifo
    uint8_t rxfptr;       // first empty byte in fifo
    uint8_t txftl;        // tx fifo tx lst flags. (bits relate to subscripts)
    uint8_t rxffc;        // rx fifo fc flags bitss
    uint8_t rxap;         // rx fifo ap flags. (bits relate to subscripts)

    uint8_t status1;
    // b0 receiver data available
    // b1 status 2 read required (OR of all in s2)
    // b2 loop mode.
    // b3 flag detected
    // b4 clear to send
    // b5 transmitter underrun
    // b6 tx data reg avail / frame complete
    // b7 IRQ active

    uint8_t status2;
    // b0 Address present
    // b1 Frame Valid
    // b2 Rx idle
    // b3 Rx Abort
    // b4 Frame Check Sequence/Invalid Frame error
    // b5 Data Carrier Detect
    // b6 Overrun
    // b7 Receiver Data Avilable (see s1 b0 )

    int sr2pse;                 // PSE level for SR2 rx bits
    // 0 = inactive
    // 1 = ERR, FV, DCD, OVRN, ABT
    // 2 = Idle
    // 3 = AP
    // 4 = RDA

    bool cts;       // signal up
    bool idle;
};

/* Symbolic values for control register 1 */

#define ADLC_CTL1_AC        0x01
#define ADLC_CTL1_RIE       0x02
#define ADLC_CTL1_TIE       0x04
#define ADLC_CTL1_RDSR      0x08
#define ADLC_CTL1_TDSR      0x10
#define ADLC_CTL1_RX_DISC   0x20
#define ADLC_CTL1_RX_RESET  0x40
#define ADLC_CTL1_TX_RESET  0x80

/* Symbolic values for control register 2 */

#define ADLC_CTL2_PSE       0x01
#define ADLC_CTL2_2BYTE     0x02
#define ADLC_CTL2_FLAG      0x04
#define ADLC_CTL2_FR_COMP   0x08
#define ADLC_CTL2_TX_LAST   0x10
#define ADLC_CTL2_RX_CLEAR  0x20
#define ADLC_CTL2_TX_CLEAR  0x40
#define ADLC_CTL2_RTS       0x80

/* Symbolic values for control register 3 */

#define ADLC_CTL3_LOG_FIELD 0x01
#define ADLC_CTL3_EXT_FIELD 0x02
#define ADLC_CTL3_AUTO_ADDR 0x04
#define ADLC_CTL3_IDLE_PAT  0x08
#define ADLC_CTL3_FLAG_DET  0x10
#define ADLC_CTL3_LOOP      0x20
#define ADLC_CTL3_ACT_POLL  0x40
#define ADLC_CTL3_LOOP_DTR  0x80

/* Symbolic values for control register 4 */

#define ADLC_CTL4_DOUB_FLAG 0x01
#define ADLC_CTL4_TX_WL1    0x02
#define ADLC_CTL4_TX_WL2    0x04
#define ADLC_CTL4_RX_WL1    0x08
#define ADLC_CTL4_RX_WL2    0x10
#define ADLC_CTL4_TX_ABORT  0x20
#define ADLC_CTL4_ABORT_EXT 0x40
#define ADLC_CTL4_NRZI      0x80

/* Symbolic values for status register 1 */

#define ADLC_STA1_RDA       0x01
#define ADLC_STA1_S2RR      0x02
#define ADLC_STA1_LOOP      0x04
#define ADLC_STA1_FLAG_DET  0x08
#define ADLC_STA1_NOT_CTS   0x10
#define ADLC_STA1_TX_UNDER  0x20
#define ADLC_STA1_TDRAFC    0x40
#define ADLC_STA1_IRQ       0x80

/* Symbolic values for status register 2 */

#define ADLC_STA2_ADDR_PRES 0x01
#define ADLC_STA2_FRAME_VAL 0x02
#define ADLC_STA2_INAC_IDLE 0x04
#define ADLC_STA2_ABORT     0x08
#define ADLC_STA2_FCS_ERR   0x10
#define ADLC_STA2_NOT_DCD   0x20
#define ADLC_STA2_RX_OVER   0x40
#define ADLC_STA2_RDA       0x80

void econet_reset(void);
uint8_t econet_read_station(void);
uint8_t econet_read_register(uint8_t addr);
void econet_write_register(uint8_t addr, uint8_t Value);
void econet_poll(void);

extern bool EconetEnabled;
extern bool EconetNMIenabled;
volatile extern struct MC6854 ADLC;

extern uint8_t EconetStationNumber;
extern unsigned int EconetListenPort;

//extern WSADATA WsaDat;                          // Windows sockets info

#endif
