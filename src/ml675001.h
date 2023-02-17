/**********************************************************************************/
/*                                                                                */
/*    Copyright (C) 2003 Oki Electric Industry Co., LTD.                          */
/*                                                                                */
/*    System Name    :  ML675001 series                                           */
/*    Module Name    :  Common definition include file for ML675001 series        */
/*    File   Name    :  ML675001.h                                                */
/*    Revision       :  01.00                                                     */
/*    Date           :  2003/03/09                                                */
/*                                                                                */
/**********************************************************************************/
#ifndef ML675001_H
#define ML675001_H

#ifdef __cplusplus
extern "C" {
#endif


/*------------------------------ uPLAT-7B core -----------------------------------*/
/*****************************************************/
/*    interrupt control register                     */
/*****************************************************/
#define ICR_BASE    (0x78000000)    /* base address of interrupt control register */
#define IRQ         (ICR_BASE+0x00) /* IRQ register (R,32,0x00000000) */
#define IRQS        (ICR_BASE+0x04) /* IRQ soft register (RW,32,0x00000000) */
#define FIQ         (ICR_BASE+0x08) /* FIQ register (R,32,0x00000000) */
#define FIQRAW      (ICR_BASE+0x0C) /* FIQRAW status register (R,32,--)*/
#define FIQEN       (ICR_BASE+0x10) /* FIQ enable register (RW,32,0x00000000)*/
#define IRN         (ICR_BASE+0x14) /* IRQ number register (R,32,0x00000000)*/
#define CIL         (ICR_BASE+0x18) /* current IRQ level register (RW,32,0x00000000)*/
#define ILC0        (ICR_BASE+0x20) /* IRQ level control register 0 (RW,32,0x00000000) */
#define ILC1        (ICR_BASE+0x24) /* IRQ level control register 1 (RW,32,0x00000000) */
#define CILCL       (ICR_BASE+0x28) /* current IRQ level clear register (W,32,--) */
#define CILE        (ICR_BASE+0x2C) /* current IRQ level encode register (R,32,0x00000000) */

/* bit field of IRQ register */
#define IRQ_nIR0    (0x00000001)    /* nIR[0] */
#define IRQ_nIR1    (0x00000002)    /* nIR[1] */
#define IRQ_nIR2    (0x00000004)    /* nIR[2] */
#define IRQ_nIR3    (0x00000008)    /* nIR[3] */
#define IRQ_nIR4    (0x00000010)    /* nIR[4] */
#define IRQ_nIR5    (0x00000020)    /* nIR[5] */
#define IRQ_nIR6    (0x00000040)    /* nIR[6] */
#define IRQ_nIR7    (0x00000080)    /* nIR[7] */
#define IRQ_nIR8    (0x00000100)    /* nIR[8] */
#define IRQ_nIR9    (0x00000200)    /* nIR[9] */
#define IRQ_nIR10   (0x00000400)    /* nIR[10] */
#define IRQ_nIR11   (0x00000800)    /* nIR[11] */
#define IRQ_nIR12   (0x00001000)    /* nIR[12] */
#define IRQ_nIR13   (0x00002000)    /* nIR[13] */
#define IRQ_nIR14   (0x00004000)    /* nIR[14] */
#define IRQ_nIR15   (0x00008000)    /* nIR[15] */

/* bit field of IRQS register */
#define IRQS_IRQS   (0x00000002)    /* IRQS bit */

/* bit field of FIQ register */
#define FIQ_FIQ (0x00000001)    /* FIQ bit */

/* bit field of FIQRAW register */
#define FIQRAW_FIQRAW   (0x00000001)    /* FIQRAW bit */

/* bit field of FIQEN register */
#define FIQEN_FIQEN (0x00000001)    /* FIQEN bit */

/* bit field of IRN register */
#define IRN_IRN (0x0000007F)    /* IRN[6:0] */

/* bit field of CIL register */
#define CIL_INT_LV1 (0x00000002)    /* interrupt level 1 */
#define CIL_INT_LV2 (0x00000004)    /* interrupt level 2 */
#define CIL_INT_LV3 (0x00000008)    /* interrupt level 3 */
#define CIL_INT_LV4 (0x00000010)    /* interrupt level 4 */
#define CIL_INT_LV5 (0x00000020)    /* interrupt level 5 */
#define CIL_INT_LV6 (0x00000040)    /* interrupt level 6 */
#define CIL_INT_LV7 (0x00000080)    /* interrupt level 7 */

/* bit field of ILC0 register */
#define ILC0_INT_LV1    (0x11111111)    /* interrupt level 1 */
#define ILC0_INT_LV2    (0x22222222)    /* interrupt level 2 */
#define ILC0_INT_LV3    (0x33333333)    /* interrupt level 3 */
#define ILC0_INT_LV4    (0x44444444)    /* interrupt level 4 */
#define ILC0_INT_LV5    (0x55555555)    /* interrupt level 5 */
#define ILC0_INT_LV6    (0x66666666)    /* interrupt level 6 */
#define ILC0_INT_LV7    (0x77777777)    /* interrupt level 7 */
#define ILC0_ILR0       (0x00000007)    /* nIR[0] */
#define ILC0_ILR1       (0x00000070)    /* nIR[1],nIR[2],nIR[3] */
#define ILC0_ILR4       (0x00070000)    /* nIR[4],nIR[5] */
#define ILC0_ILR6       (0x07000000)    /* nIR[6],nIR[7] */

/* bit field of ILC1 register */
#define ILC1_INT_LV1    (0x11111111)    /* interrupt level 1 */
#define ILC1_INT_LV2    (0x22222222)    /* interrupt level 2 */
#define ILC1_INT_LV3    (0x33333333)    /* interrupt level 3 */
#define ILC1_INT_LV4    (0x44444444)    /* interrupt level 4 */
#define ILC1_INT_LV5    (0x55555555)    /* interrupt level 5 */
#define ILC1_INT_LV6    (0x66666666)    /* interrupt level 6 */
#define ILC1_INT_LV7    (0x77777777)    /* interrupt level 7 */
#define ILC1_ILR8       (0x00000007)    /* nIR[8] */
#define ILC1_ILR9       (0x00000070)    /* nIR[9] */
#define ILC1_ILR10      (0x00000700)    /* nIR[10] */
#define ILC1_ILR11      (0x00007000)    /* nIR[11] */
#define ILC1_ILR12      (0x00070000)    /* nIR[12] */
#define ILC1_ILR13      (0x00700000)    /* nIR[13] */
#define ILC1_ILR14      (0x07000000)    /* nIR[14] */
#define ILC1_ILR15      (0x70000000)    /* nIR[15] */

/* bit field of CILCL register */
#define CILCL_CLEAR (0x00000001)    /* most significant '1' bit of CIL is cleared */

/* bit field of CILE register */
#define CILE_CILE   (0x00000007)    /* CILE[2:0] */

/*****************************************************/
/*    external memory control register               */
/*****************************************************/
#define EMCR_BASE   (0x78100000)        /* base address */
#define BWC         (EMCR_BASE+0x00)    /* bus width control register (RW,32,0x00000008) */
#define ROMAC       (EMCR_BASE+0x04)    /* external ROM access control register (RW,32,0x00000007) */
#define RAMAC       (EMCR_BASE+0x08)    /* external SRAM access control register (RW,32,0x00000007) */
#define IO0AC       (EMCR_BASE+0x0C)    /* external IO0 access control register (RW,32,0x00000007) */
#define IO1AC       (EMCR_BASE+0x10)    /* external IO1 access control register (RW,32,0x00000007) */

/* bit field of BWC register */
#define BWC_ROMBW0  (0x00000000)    /* ROM disable */
#define BWC_ROMBW16 (0x00000008)    /* ROM 16bit */
#define BWC_RAMBW0  (0x00000000)    /* RAM disable */
#define BWC_RAMBW16 (0x00000020)    /* RAM 16bit */
#define BWC_IO0BW0  (0x00000000)    /* IO0 disable */
#define BWC_IO0BW8  (0x00000040)    /* IO0 8bit */
#define BWC_IO0BW16 (0x00000080)    /* IO0 16 bit */
#define BWC_IO1BW0  (0x00000000)    /* IO1 disable */
#define BWC_IO1BW8  (0x00000100)    /* IO1 8bit */
#define BWC_IO1BW16 (0x00000200)    /* IO1 16bit */

/* bit field of ROMAC register */
#define ROMAC_ROMTYPE   (0x00000007)    /* ROMTYPE[2:0] */

/* bit field of RAMAC register */
#define RAMAC_RAMTYPE   (0x00000007)    /* RAMTYPE[2:0] */

/* bit field of IO0AC register */
#define IO0AC_IO0TYPE   (0x00000007)    /* IO0TYPE[2:0] */

/* bit field of IO1AC register */
#define IO1AC_IO1TYPE   (0x00000007)    /* IO1TYPE[2:0] */

/*****************************************************/
/*    system control register                        */
/*****************************************************/
#define SCR_BASE    (0xB8000000)    /* base address */
#define CLKSTP      (SCR_BASE+0x04) /* clock stop register (W,32,0x00000000) */
#define CGBCNT0     (SCR_BASE+0x08) /* clock(CGB) control register 0 (RW,32,0x00000000) */
#define CKWT        (SCR_BASE+0x0C) /* clock wait register (RW,32,0x0000000B) */
#define RMPCON      (SCR_BASE+0x10) /* remap control register (RW,32,0x00000000) */

/* bit field of CLKSTP register */
#define CLKSTP_SIO  (0x00000001)    /* SIO HALT */
#define CLKSTP_TIC  (0x00000002)    /* TIC HALT */
#define CLKSTP_HALT (0x00000004)    /* CPU group HALT */
#define CLKSTP_STBY (0x000000F0)    /* STANDBY */

/* bit field of CGBCNT0 register */
#define CGBCNT0_HCLK1   (0x00000000)    /* HCLK 1 dividing  */
#define CGBCNT0_HCLK2   (0x00000001)    /* HCLK 2 dividing  */
#define CGBCNT0_HCLK4   (0x00000002)    /* HCLK 4 dividing  */
#define CGBCNT0_HCLK8   (0x00000003)    /* HCLK 8 dividing  */
#define CGBCNT0_HCLK16  (0x00000004)    /* HCLK 16 dividing */
#define CGBCNT0_HCLK32  (0x00000005)    /* HCLK 32 dividing */
#define CGBCNT0_CCLK1   (0x00000000)    /* CCLK 1 dividing  */
#define CGBCNT0_CCLK2   (0x00000010)    /* CCLK 2 dividing  */
#define CGBCNT0_CCLK4   (0x00000020)    /* CCLK 4 dividing  */
#define CGBCNT0_CCLK8   (0x00000030)    /* CCLK 8 dividing  */
#define CGBCNT0_CCLK16  (0x00000040)    /* CCLK 16 dividing */
#define CGBCNT0_CCLK32  (0x00000050)    /* CCLK 32 dividing */

/* bit field of RMPCON register */
#define RMPCON_ENABLE   (0x00000008)    /* remap enabled */
#define RMPCON_DISABLE  (0x00000000)    /* remap disabled */
#define RMPCON_AHB      (0x00000002)    /* device space is AHB bus*/
#define RMPCON_EXT      (0x00000000)    /* device space is external bus */
#define RMPCON_DRAM     (0x00000001)    /* memory type is DRAM */
#define RMPCON_SRAM     (0x00000000)    /* memory type is SRAM */
#define RMPCON_IRAM     (0x00000004)    /* memory type is internal RAM */


/*****************************************************/
/*    system timer control register                  */
/*****************************************************/
#define STCR_BASE   (0xB8001000)        /* base address */
#define TMEN        (STCR_BASE+0x04)    /* timer enable register (RW,16,0x0000) */
#define TMRLR       (STCR_BASE+0x08)    /* timer reload register (RW,16,0x0000) */
#define TMOVF       (STCR_BASE+0x10)    /* overflow register (RW,16,0x0000) */

/* bit field of TMEN register */
#define TMEN_TCEN   (0x0001)    /* timer enabled */

/* bit field of TMOVF register */
#define TMOVF_OVF   (0x0001)    /* overflow generated */


/*****************************************************/
/*    ASIO control register                          */
/*****************************************************/
#define SC_BASE (0xB8002000)    /* base address */
#define SIOBUF  (SC_BASE+0x00)  /* transmiting/receiving buffer register (RW,16,0x0000) */
#define SIOSTA  (SC_BASE+0x04)  /* SIO status register (RW,16,0x0000) */
#define SIOCON  (SC_BASE+0x08)  /* SIO control register (RW,16,0x0000) */
#define SIOBCN  (SC_BASE+0x0C)  /* baud rate control register (RW,16,0x0000) */
#define SIOBT   (SC_BASE+0x14)  /* baud rate timer register (RW,16,0x0000) */
#define SIOTCN  (SC_BASE+0x18)  /* SIO test control register (RW,16,0x0000) */

/* bit field of SIOBUF register */
#define SIOBUF_SIOBUF   (0x00FF)    /* SIOBUF[7:0] */

/* bit field of SIOSTA register */
#define SIOSTA_FERR     (0x0001)    /* framing error */
#define SIOSTA_OERR     (0x0002)    /* overrun error */
#define SIOSTA_PERR     (0x0004)    /* parity error */
#define SIOSTA_RVIRQ    (0x0010)    /* receive ready */
#define SIOSTA_TRIRQ    (0x0020)    /* transmit ready */

/* bit field of SIOCON register */
#define SIOCON_LN7      (0x0001)    /* data length : 7bit */
#define SIOCON_LN8      (0x0000)    /* data length : 8bit */
#define SIOCON_PEN      (0x0002)    /* parity enabled */
#define SIOCON_PDIS     (0x0000)    /* parity disabled */
#define SIOCON_EVN      (0x0004)    /* even parity */
#define SIOCON_ODD      (0x0000)    /* odd parity */
#define SIOCON_TSTB1    (0x0008)    /* stop bit : 1 */
#define SIOCON_TSTB2    (0x0000)    /* stop bit : 2 */

/* bit field of SIOBCN register */
#define SIOBCN_BGRUN    (0x0010)    /* count start */

/* bit field of SIOBT register */
#define SIOBT_SIOBT (0x00FF)    /* SIOBT[7:0] */

/* bit field of SIOTCN register */
#define SIOTCN_MFERR    (0x0001)    /* generate framin error */
#define SIOTCN_MPERR    (0x0002)    /* generate parity error */
#define SIOTCN_LBTST    (0x0080)    /* loop back test */


/*---------------------------------- ML674001 ------------------------------------*/
/*****************************************************/
/*    timer control register                         */
/*****************************************************/
#define TCR_BASE    (0xB7F00000)    /* base address */
#define TIMECNTL0   (TCR_BASE+0x00) /* timer0 control register (RW,16,0x0000) */
#define TIMEBASE0   (TCR_BASE+0x04) /* timer0 base register (RW,16,0x0000) */
#define TIMECNT0    (TCR_BASE+0x08) /* timer0 counter register (R,16,0x0000) */
#define TIMECMP0    (TCR_BASE+0x0C) /* timer0 compare register (RW,16,0xFFFF) */
#define TIMESTAT0   (TCR_BASE+0x10) /* timer0 status register (RW,16,0x0000) */
#define TIMECNTL1   (TCR_BASE+0x20) /* timer1 control register (RW,16,0x0000) */
#define TIMEBASE1   (TCR_BASE+0x24) /* timer1 base register (RW,16,0x0000) */
#define TIMECNT1    (TCR_BASE+0x28) /* timer1 counter register (R,16,0x0000) */
#define TIMECMP1    (TCR_BASE+0x2C) /* timer1 compare register (RW,16,0xFFFF) */
#define TIMESTAT1   (TCR_BASE+0x30) /* timer1 status register (RW,16,0x0000) */
#define TIMECNTL2   (TCR_BASE+0x40) /* timer2 control register (RW,16,0x0000) */
#define TIMEBASE2   (TCR_BASE+0x44) /* timer2 base register (RW,16,0x0000) */
#define TIMECNT2    (TCR_BASE+0x48) /* timer2 counter register (R,16,0x0000) */
#define TIMECMP2    (TCR_BASE+0x4C) /* timer2 compare register (RW,16,0xFFFF) */
#define TIMESTAT2   (TCR_BASE+0x50) /* timer2 status register (RW,16,0x0000) */
#define TIMECNTL3   (TCR_BASE+0x60) /* timer3 control register (RW,16,0x0000) */
#define TIMEBASE3   (TCR_BASE+0x64) /* timer3 base register (RW,16,0x0000) */
#define TIMECNT3    (TCR_BASE+0x68) /* timer3 counter register (R,16,0x0000) */
#define TIMECMP3    (TCR_BASE+0x6C) /* timer3 compare register (RW,16,0xFFFF) */
#define TIMESTAT3   (TCR_BASE+0x70) /* timer3 status register (RW,16,0x0000) */
#define TIMECNTL4   (TCR_BASE+0x80) /* timer4 control register (RW,16,0x0000) */
#define TIMEBASE4   (TCR_BASE+0x84) /* timer4 base register (RW,16,0x0000) */
#define TIMECNT4    (TCR_BASE+0x88) /* timer4 counter register (R,16,0x0000) */
#define TIMECMP4    (TCR_BASE+0x8C) /* timer4 compare register (RW,16,0xFFFF) */
#define TIMESTAT4   (TCR_BASE+0x90) /* timer4 status register (RW,16,0x0000) */
#define TIMECNTL5   (TCR_BASE+0xA0) /* timer5 control register (RW,16,0x0000) */
#define TIMEBASE5   (TCR_BASE+0xA4) /* timer5 base register (RW,16,0x0000) */
#define TIMECNT5    (TCR_BASE+0xA8) /* timer5 counter register (R,16,0x0000) */
#define TIMECMP5    (TCR_BASE+0xAC) /* timer5 compare register (RW,16,0xFFFF) */
#define TIMESTAT5   (TCR_BASE+0xB0) /* timer5 status register (RW,16,0x0000) */

/* bit field of TIMECNTL0-5 register */
#define TIMECNTL_CLK    (0x0000)    /* CPUCLK */
#define TIMECNTL_CLK2   (0x0020)    /* CPUCLK/2 */
#define TIMECNTL_CLK4   (0x0040)    /* CPUCLK/4 */
#define TIMECNTL_CLK8   (0x0060)    /* CPUCLK/8 */
#define TIMECNTL_CLK16  (0x0080)    /* CPUCLK/16 */
#define TIMECNTL_CLK32  (0x00A0)    /* CPUCLK/32 */
#define TIMECNTL_IE     (0x0010)    /* enable interrupt */
#define TIMECNTL_START  (0x0008)    /* timer start */
#define TIMECNTL_OS     (0x0001)    /* one shot timer */
#define TIMECNTL_INT    (0x0000)    /* interval timer */

/* bit field of TIMESTAT0-5 register */
#define TIMESTAT_STATUS (0x0001)    /* status bit */

/*****************************************************/
/*    Watch Dog Timer control register               */
/*****************************************************/
#define WDT_BASE    (0xB7E00000)    /* base address */
#define WDTCON      (WDT_BASE+0x00) /* Watch Dog Timer control register (W,8,--) */
#define WDTBCON     (WDT_BASE+0x04) /* time base counter control register (RW,8,0x00) */
#define WDSTAT      (WDT_BASE+0x14) /* Watch Dog Timer status register (RW,8,0x00) */

/* bit field of WDTCON */
#define WDTCON_0xC3 (0xC3)  /* 0xC3 */
#define WDTCON_0x3C (0x3C)  /* 0x3C */

/* bit field of WDTBCON */
#define WDTBCON_CLK32   (0x00)  /* CPUCLK/32 */
#define WDTBCON_CLK64   (0x01)  /* CPUCLK/64 */
#define WDTBCON_CLK128  (0x02)  /* CPUCLK/128 */
#define WDTBCON_CLK256  (0x03)  /* CPUCLK/256 */
#define WDTBCON_WDTM    (0x00)  /* WDT mode */
#define WDTBCON_ITM     (0x08)  /* interval timer mode */
#define WDTBCON_ITDIS   (0x00)  /* disable interval timer */
#define WDTBCON_ITEN    (0x10)  /* enable interval timer */
#define WDTBCON_INT     (0x00)  /* generate interrupt */
#define WDTBCON_RESET   (0x40)  /* system reset */
#define WDTBCON_WDHLT   (0x80)  /* HALT */
#define WDTBCON_WE      (0x5A)  /* enable writing to this register */

/* bit field of WDTSTAT */
#define WDSTAT_RSTWDT   (0x01)  /* reset by WDT */
#define WDSTAT_RSTPWON  (0x00)  /* reset by power on */
#define WDSTAT_WDTIST   (0x10)  /* WDT interrupt */
#define WDSTAT_IVTIST   (0x20)  /* IVT interrupt */


/*****************************************************/
/*    UART control register                          */
/*****************************************************/
#define UCR_BASE    (0xB7B00000)    /* base address */
#define UARTRBR     (UCR_BASE+0x00) /* receiver buffer register (R,8,--) */
#define UARTTHR     (UCR_BASE+0x00) /* transmitter buffer register (R,8--) */
#define UARTIER     (UCR_BASE+0x04) /* interrupt enable register (RW,8,0x00) */
#define UARTIIR     (UCR_BASE+0x08) /* interrupt identification (R,8,0x01) */
#define UARTFCR     (UCR_BASE+0x08) /* FIFO control register (W,8,0x00) */
#define UARTLCR     (UCR_BASE+0x0C) /* line control register (RW,8,0x00) */
#define UARTMCR     (UCR_BASE+0x10) /* modem control register (RW,8,0x00) */
#define UARTLSR     (UCR_BASE+0x14) /* line status register (RW,8,0x60) */
#define UARTMSR     (UCR_BASE+0x18) /* modem status register (RW,8,--) */
#define UARTSCR     (UCR_BASE+0x1C) /* scratchpad register (RW,8,--) */
#define UARTDLL     (UCR_BASE+0x00) /* divisor latch(LSB) (RW,8,0x00) */
#define UARTDLM     (UCR_BASE+0x04) /* divisor latch(MSB) (RW,8,0x00) */

/* bit field of UARTRBR register */
#define UARTRBR_RBR (0xFF)  /* RBR[7:0] */

/* bit field of UARTTHR register */
#define UARTTHR_THR (0xFF)  /* THR[7:0] */

/* bit field of UARTIER register */
#define UARTIER_ERBF    (0x01)  /* enable received data available interrupt */
#define UARTIER_ETBEF   (0x02)  /* enable transmitter holding register empty interrupt */
#define UARTIER_ELSI    (0x04)  /* enable receiver line status interrupt */
#define UARTIER_EDSI    (0x08)  /* enable modem status interrupt */

/* bit field of UARTIIR register */
#define UARTIIR_IP      (0x01)  /* interrupt generated */
#define UARTIIR_LINE    (0x06)  /* receiver line status interrupt */
#define UARTIIR_RCV     (0x04)  /* receiver interrupt */
#define UARTIIR_TO      (0x0C)  /* time out interrupt */
#define UARTIIR_TRA     (0x02)  /* transmitter interrupt */
#define UARTIIR_FM      (0xC0)  /* FIFO mode */

/* bit field of UARTFCR register */
#define UARTFCR_FE      (0x01)  /* FIFO enable */
#define UARTFCR_FD      (0x00)  /* FIFO disable */
#define UARTFCR_RFCLR   (0x02)  /* receiver FIFO clear */
#define UARTFCR_TFCLR   (0x04)  /* transmitter FIFO clear */
#define UARTFCR_RFLV1   (0x00)  /* RCVR FIFO interrupt trigger level : 1byte */
#define UARTFCR_RFLV4   (0x40)  /* RCVR FIFO interrupt trigger level : 4byte */
#define UARTFCR_RFLV8   (0x80)  /* RCVR FIFO interrupt trigger level : 8byte */
#define UARTFCR_RFLV14  (0xC0)  /* RCVR FIFO interrupt trigger level : 14byte */

/* bit field of UARTLCR register */
#define UARTLCR_LEN5    (0x00)  /* data length : 5bit */
#define UARTLCR_LEN6    (0x01)  /* data length : 6bit */
#define UARTLCR_LEN7    (0x02)  /* data length : 7bit */
#define UARTLCR_LEN8    (0x03)  /* data length : 8bit */
#define UARTLCR_STB1    (0x00)  /* stop bit : 1 */
#define UARTLCR_STB2    (0x04)  /* stop bit : 2(data length 6-8) */
#define UARTLCR_STB1_5  (0x04)  /* stop bit : 1.5(data length 5) */
#define UARTLCR_PEN     (0x08)  /* parity enabled */
#define UARTLCR_PDIS    (0x00)  /* parity disabled */
#define UARTLCR_EVN     (0x10)  /* even parity */
#define UARTLCR_ODD     (0x00)  /* odd parity */
#define UARTLCR_SP      (0x20)  /* stick parity */
#define UARTLCR_BRK     (0x40)  /* break delivery */
#define UARTLCR_DLAB    (0x80)  /* devisor latch access bit */

/* bit field of UARTMCR register */
#define UARTMCR_DTR     (0x01)  /* data terminal ready */
#define UARTMCR_RTS     (0x02)  /* request to send */
#define UARTMCR_LOOP    (0x10)  /* loopback */

/* bit field of UARTLSR register */
#define UARTLSR_DR      (0x01)  /* data ready */
#define UARTLSR_OE      (0x02)  /* overrun error */
#define UARTLSR_PE      (0x04)  /* parity error */
#define UARTLSR_FE      (0x08)  /* framing error */
#define UARTLSR_BI      (0x10)  /* break interrupt */
#define UARTLSR_THRE    (0x20)  /* transmitter holding register empty */
#define UARTLSR_TEMT    (0x40)  /* transmitter empty */
#define UARTLSR_ERF     (0x80)  /* receiver FIFO error */

/* bit field of UARTMSR register */
#define UARTMSR_DCTS    (0x01)  /* delta clear to send */
#define UARTMSR_DDSR    (0x02)  /* delta data set ready */
#define UARTMSR_TERI    (0x04)  /* trailing edge of ring endicator */
#define UARTMSR_DDCD    (0x08)  /* delta data carrer detect */
#define UARTMSR_CTS     (0x10)  /* clear to send */
#define UARTMSR_DSR     (0x20)  /* data set ready */
#define UARTMSR_RI      (0x40)  /* ring indicator */
#define UARTMSR_DCD     (0x80)  /* data carrer detect */

/* bit field of UARTSCR register */
#define UARTSCR_SCR (0xFF)  /* SCR[7:0] */

/* bit field of UARTDLL register */
#define UARTDLL_DLL (0xFF)  /* DLL[7:0](=DL[7:0]) */

/* bit field of UARTDLM register */
#define UARTDLM_DLM (0xFF)  /* DLM[7:0](=DL[15:8]) */


/*****************************************************/
/*    PWM control register                           */
/*****************************************************/
#define PWM_BASE    (0xB7D00000)    /* base address */
#define PWR0        (PWM_BASE+0x00) /* PWM register 0 (RW,16,0x0000) */
#define PWCY0       (PWM_BASE+0x04) /* PWM cycle register 0 (RW,16,0x0000) */
#define PWC0        (PWM_BASE+0x08) /* PWM counter 0 (RW,16,0x0000) */
#define PWCON0      (PWM_BASE+0x0C) /* PWM contrlo register 0 (RW,16,0x0000) */
#define PWR1        (PWM_BASE+0x20) /* PWM register 1 (RW,16,0x0000) */
#define PWCY1       (PWM_BASE+0x24) /* PWM cycle register 1 (RW,16,0x0000) */
#define PWC1        (PWM_BASE+0x28) /* PWM counter 1 (RW,16,0x0000) */
#define PWCON1      (PWM_BASE+0x2C) /* PWM contrlo register 1 (RW,16,0x0000) */
#define PWINTSTS    (PWM_BASE+0x3C) /* PWM interrupt status register (RW,16,0x0000) */

/* bit field of PWCON0,1 register */
#define PWCON_PWR   (0x0001)    /* enable PWC */
#define PWCON_CLK1  (0x0000)    /* 1/1 CPUCLK */
#define PWCON_CLK4  (0x0002)    /* 1/4 CPUCLK */
#define PWCON_CLK16 (0x0004)    /* 1/16 CPUCLK */
#define PWCON_CLK32 (0x0006)    /* 1/32 CPUCLK */
#define PWCON_INTIE (0x0040)    /* enable interrupt */
#define PWCON_PWCOV (0x0080)    

/* bit field of PWINTSTS register */
#define PWINTSTS_INT1S      (0x0200)    /* CH1 interrupt generated */
#define PWINTSTS_INT0S      (0x0100)    /* CH0 interrupt generated */
#define PWINTSTS_INT1CLR    (0x0002)    /* CH1 interrupt clear */
#define PWINTSTS_INT0CLR    (0x0001)    /* CH0 interrupt clear */


/*****************************************************/
/*    port control register                           */
/*****************************************************/
#define PCR_BASE    (0xB7A01000)    /* base address */
#define GPPOA       (PCR_BASE+0x00) /* port A output register (RW,16,--) */
#define GPPIA       (PCR_BASE+0x04) /* port A input register (R,16,--)*/
#define GPPMA       (PCR_BASE+0x08) /* port A Mode register (RW,16,0x0000) */
#define GPIEA       (PCR_BASE+0x0C) /* port A interrupt enable (RW,16,0x0000) */
#define GPIPA       (PCR_BASE+0x10) /* port A interrupt Polarity (RW,16,0x0000) */
#define GPISA       (PCR_BASE+0x14) /* port A interrupt Status (RW,16,0x0000) */

#define GPPOB       (PCR_BASE+0x20) /* port B Output register (RW,16,--) */
#define GPPIB       (PCR_BASE+0x24) /* port B Input register (RW,16,--) */
#define GPPMB       (PCR_BASE+0x28) /* port B Mode register (RW,16,0x0000) */
#define GPIEB       (PCR_BASE+0x2C) /* port B interrupt enable (RW,16,0x0000) */
#define GPIPB       (PCR_BASE+0x30) /* port B interrupt Polarity (RW,16,0x0000) */
#define GPISB       (PCR_BASE+0x34) /* port B interrupt Status (RW,16,0x0000) */

#define GPPOC       (PCR_BASE+0x40) /* port C Output register (RW,16,--) */
#define GPPIC       (PCR_BASE+0x44) /* port C Input register (RW,16,--) */
#define GPPMC       (PCR_BASE+0x48) /* port C Mode register (RW,16,0x0000) */
#define GPIEC       (PCR_BASE+0x4C) /* port C interrupt enable (RW,16,0x0000) */
#define GPIPC       (PCR_BASE+0x50) /* port C interrupt Polarity (RW,16,0x0000) */
#define GPISC       (PCR_BASE+0x54) /* port C interrupt Status (RW,16,0x0000) */

#define GPPOD       (PCR_BASE+0x60) /* port D Output register (RW,16,--) */
#define GPPID       (PCR_BASE+0x64) /* port D Input register (RW,16,--) */
#define GPPMD       (PCR_BASE+0x68) /* port D Mode register (RW,16,0x0000) */
#define GPIED       (PCR_BASE+0x6C) /* port D interrupt enable (RW,16,0x0000) */
#define GPIPD       (PCR_BASE+0x70) /* port D interrupt Polarity (RW,16,0x0000) */
#define GPISD       (PCR_BASE+0x74) /* port D interrupt Status (RW,16,0x0000) */

#define GPPOE       (PCR_BASE+0x80) /* port E Output register (RW,16,--) */
#define GPPIE       (PCR_BASE+0x84) /* port E Input register (RW,16,--) */
#define GPPME       (PCR_BASE+0x88) /* port E Mode register (RW,16,0x0000) */
#define GPIEE       (PCR_BASE+0x8C) /* port E interrupt enable (RW,16,0x0000) */
#define GPIPE       (PCR_BASE+0x90) /* port E interrupt Polarity (RW,16,0x0000) */
#define GPISE       (PCR_BASE+0x94) /* port E interrupt Status (RW,16,0x0000) */

/* bit field of GPPOA/GPPOB/GPPOC/GPPOD/GPPOE register */
#define GPPOA_GPPOA (0x00FF)    /* GPPOA[7:0] */
#define GPPOB_GPPOB (0x00FF)    /* GPPOB[7:0] */
#define GPPOC_GPPOC (0x00FF)    /* GPPOC[7:0] */
#define GPPOD_GPPOD (0x00FF)    /* GPPOD[7:0] */
#define GPPOE_GPPOE (0x03FF)    /* GPPOE[9:0] */

/* bit field of GPPIA/GPPIB/GPPIC/GPPID/GPPIE register */
#define GPPIA_GPPIA (0x00FF)    /* GPPIA[7:0] */
#define GPPIB_GPPIB (0x00FF)    /* GPPIB[7:0] */
#define GPPIC_GPPIC (0x00FF)    /* GPPIC[7:0] */
#define GPPID_GPPID (0x00FF)    /* GPPID[7:0] */
#define GPPIE_GPPIE (0x03FF)    /* GPPIE[9:0] */

/* bit field of GPPMA/GPPMB/GPPMC/GPPMD/GPPME register */
#define GPPMA_GPPMA (0x00FF)    /* GPPMA[7:0] 0:input, 1:output */
#define GPPMB_GPPMB (0x00FF)    /* GPPMB[7:0] 0:input, 1:output */
#define GPPMC_GPPMC (0x00FF)    /* GPPMC[7:0] 0:input, 1:output */
#define GPPMD_GPPMD (0x00FF)    /* GPPMD[7:0] 0:input, 1:output */
#define GPPME_GPPME (0x03FF)    /* GPPME[9:0] 0:input, 1:output */

/* bit field of GPIEA/GPIEB/GPIEC/GPIED/GPIEE register */
#define GPIEA_GPIEA (0x00FF)    /* GPIEA[7:0] 0:interrupt disable, 1:interrupt enable */
#define GPIEB_GPIEB (0x00FF)    /* GPIEB[7:0] 0:interrupt disable, 1:interrupt enable */
#define GPIEC_GPIEC (0x00FF)    /* GPIEC[7:0] 0:interrupt disable, 1:interrupt enable */
#define GPIED_GPIED (0x00FF)    /* GPIED[7:0] 0:interrupt disable, 1:interrupt enable */
#define GPIEE_GPIEE (0x03FF)    /* GPIEE[9:0] 0:interrupt disable, 1:interrupt enable */

/* bit field of GPIPA/GPIPB/GPIPC/GPIPD/GPIPE register */
#define GPIPA_GPIPA (0x00FF)    /* GPIPA[7:0] 0:falling edge, 1:rising edge */
#define GPIPB_GPIPB (0x00FF)    /* GPIPB[7:0] 0:falling edge, 1:rising edge */
#define GPIPC_GPIPC (0x00FF)    /* GPIPC[7:0] 0:falling edge, 1:rising edge */
#define GPIPD_GPIPD (0x00FF)    /* GPIPD[7:0] 0:falling edge, 1:rising edge */
#define GPIPE_GPIPE (0x03FF)    /* GPIPE[9:0] 0:falling edge, 1:rising edge */

/* bit field of GPISA/GPISB/GPISC/GPISD/GPISE register */
#define GPISA_GPISA (0x00FF)    /* GPISA[7:0] 0:interrupt not occurred, 1:interrupt occurred */
#define GPISB_GPISB (0x00FF)    /* GPISB[7:0] 0:interrupt not occurred, 1:interrupt occurred */
#define GPISC_GPISC (0x00FF)    /* GPISC[7:0] 0:interrupt not occurred, 1:interrupt occurred */
#define GPISD_GPISD (0x00FF)    /* GPISD[7:0] 0:interrupt not occurred, 1:interrupt occurred */
#define GPISE_GPISE (0x03FF)    /* GPISE[9:0] 0:interrupt not occurred, 1:interrupt occurred */


/*****************************************************/
/*    ADC control register                           */
/*****************************************************/
#define ADC_BASE    (0xB6001000)    /* base address */
#define ADCON0      (ADC_BASE+0x00) /* ADC control 0 register (RW,16,0x0000) */
#define ADCON1      (ADC_BASE+0x04) /* ADC control 1 register (RW,16,0x0000) */
#define ADCON2      (ADC_BASE+0x08) /* ADC control 2 register (RW,16,0x0003) */
#define ADINT       (ADC_BASE+0x0C) /* AD interrupt control register (RW,16,0x0000) */
#define ADFINT      (ADC_BASE+0x10) /* AD Forced interrupt register (RW,16,0x0000) */
#define ADR0        (ADC_BASE+0x14) /* AD Result 0 register (RW,16,0x0000) */
#define ADR1        (ADC_BASE+0x18) /* AD Result 1 register (RW,16,0x0000) */
#define ADR2        (ADC_BASE+0x1C) /* AD Result 2 register (RW,16,0x0000) */
#define ADR3        (ADC_BASE+0x20) /* AD Result 3 register (RW,16,0x0000) */


/* bit field of ADCON0 register */
#define ADCON0_ADSNM    (0x0003)    /* ADSNM[1:0] */
#define ADCON0_CH0_3    (0x0000)    /* CH0->CH1->CH2->CH3 */
#define ADCON0_CH1_3    (0x0001)    /* CH1->CH2->CH3 */
#define ADCON0_CH2_3    (0x0002)    /* CH2->CH3 */
#define ADCON0_CH3_3    (0x0003)    /* CH3 */
#define ADCON0_ADRUN    (0x0010)    /* AD conversion start */
#define ADCON0_SCNC     (0x0040)    /* Stop after a round */

/* bit field of ADCON1 register */
#define ADCON1_ADSTM    (0x0003)    /* ADSTM[1:0] */
#define ADCON1_CH0      (0x0000)    /* CH0 */
#define ADCON1_CH1      (0x0001)    /* CH1 */
#define ADCON1_CH2      (0x0002)    /* CH2 */
#define ADCON1_CH3      (0x0003)    /* CH3 */
#define ADCON1_STS      (0x0010)    /* AD conversion start */

/* bit field of ADCON2 register */
#define ADCON2_ACKSEL   (0x0003)    /* ACKSEL[1:0] */
#define ADCON2_CLK2     (0x0001)    /* CPUCLK/2 */
#define ADCON2_CLK4     (0x0002)    /* CPUCLK/4 */
#define ADCON2_CLK8     (0x0003)    /* CPUCLK/8 */

/* bit field of ADINT register */
#define ADINT_INTSN     (0x0001)    /* AD conversion of ch7 finished (scan mode) */
#define ADINT_INTST     (0x0002)    /* AD conversion finished (select mode) */
#define ADINT_ADSNIE    (0x0004)    /* enable interrupt (scan mode) */
#define ADINT_ADSTIE    (0x0008)    /* enable interrupt (select mode) */

/* bit field of ADFINT register */
#define ADFINT_ADFAS    (0x0001)    /* Assert interrupt signal */

/* bit field of ADR0,ADR1,ADR2,ADR3 register */
#define ADR0_DT0    (0x03FF)    /* DT0[9:0] AD result */
#define ADR1_DT1    (0x03FF)    /* DT1[9:0] AD result */
#define ADR2_DT2    (0x03FF)    /* DT2[9:0] AD result */
#define ADR3_DT3    (0x03FF)    /* DT3[9:0] AD result */


/*****************************************************/
/*    DMA control register                           */
/*****************************************************/
#define DMA_BASE    (0x7BE00000)        /* base address */
#define DMAMOD      (DMA_BASE+0x0000)   /* DMA Mode register (RW,32,0x00000000) */
#define DMASTA      (DMA_BASE+0x0004)   /* DMA Status register (R,32,0x00000000) */
#define DMAINT      (DMA_BASE+0x0008)   /* DMA interrupt Status register (R,32,0x00000000) */
#define DMACMSK0    (DMA_BASE+0x0100)   /* Channel 0 Mask register (RW,32,0x00000001) */
#define DMACTMOD0   (DMA_BASE+0x0104)   /* Channel 0 Transfer Mode register (RW,32,0x00000040) */
#define DMACSAD0    (DMA_BASE+0x0108)   /* Channel 0 Source Address register (RW,32,0x00000000) */
#define DMACDAD0    (DMA_BASE+0x010C)   /* Channel 0 Destination Address register (RW,32,0x00000000) */
#define DMACSIZ0    (DMA_BASE+0x0110)   /* Channel 0 Transfer Size register (RW,32,0x00000000) */
#define DMACCINT0   (DMA_BASE+0x0114)   /* Channel 0 interrupt Clear register (W,32,--) */
#define DMACMSK1    (DMA_BASE+0x0200)   /* Channel 1 Mask register (RW,32,0x00000001) */
#define DMACTMOD1   (DMA_BASE+0x0204)   /* Channel 1 Transfer Mode register (RW,32,0x00000040) */
#define DMACSAD1    (DMA_BASE+0x0208)   /* Channel 1 Source Address register (RW,32,0x00000000) */
#define DMACDAD1    (DMA_BASE+0x020C)   /* Channel 1 Destination Address register (RW,32,0x00000000) */
#define DMACSIZ1    (DMA_BASE+0x0210)   /* Channel 1 Transfer Size register (RW,32,0x00000000) */
#define DMACCINT1   (DMA_BASE+0x0214)   /* Channel 1 interrupt Clear register (W,32,--) */

/* bit field of DMAMOD register */
#define DMAMOD_PRI  (0x00000001)    /* PRI bit */
#define DMAMOD_FIX  (0x00000000)    /* Priority of DMA channel : CH0 > CH1 */
#define DMAMOD_RR   (0x00000001)    /* Priority of DMA channel : Round robin */

/* bit field of DMASTA register */
#define DMASTA_STA0 (0x00000001)    /* Non-transmitted data is in CH0 */
#define DMASTA_STA1 (0x00000002)    /* Non-transmitted data is in CH1 */

/* bit field of DMAINT register */
#define DMAINT_IREQ0    (0x00000001)    /* CH0 interrupt */
#define DMAINT_IREQ1    (0x00000002)    /* CH1 interrupt */
#define DMAINT_ISTA0    (0x00000100)    /* CH0 abnormal end */
#define DMAINT_ISTA1    (0x00000200)    /* CH1 abnormal end */
#define DMAINT_ISTP0    (0x00010000)    /* CH0 abnormal end situation */
#define DMAINT_ISTP1    (0x00020000)    /* CH1 abnormal end situation */

/* bit field of DMAMSK0,1 register */
#define DMACMSK_MSK (0x00000001)    /* Mask */

/* bit field of DMATMOD0,1 register */
#define DMACTMOD_ARQ    (0x00000001)    /* Auto request */
#define DMACTMOD_ERQ    (0x00000000)    /* External request */
#define DMACTMOD_BYTE   (0x00000000)    /* Byte transmission */
#define DMACTMOD_HWORD  (0x00000002)    /* Half word transmission */
#define DMACTMOD_WORD   (0x00000004)    /* Word transmission */
#define DMACTMOD_SFA    (0x00000000)    /* Source data type(fixed address device) */
#define DMACTMOD_SIA    (0x00000008)    /* Source data type(incremental address device) */
#define DMACTMOD_DFA    (0x00000000)    /* Destination data type(fixed address device) */
#define DMACTMOD_DIA    (0x00000010)    /* Destination data type(incremental address device) */
#define DMACTMOD_BM     (0x00000000)    /* Bus request mode(burst mode) */
#define DMACTMOD_CSM    (0x00000020)    /* Bus request mode(cycle steal mode) */
#define DMACTMOD_IMK    (0x00000040)    /* interrupt mask */


/*****************************************************/
/*    interrupt control register                     */
/*****************************************************/
#define EIC_BASE    (0x7BF00000)    /* base address */
#define IRCL        (EIC_BASE+0x04) /* Extended interrupt Clear register (W,32,--) */
#define IRQA        (EIC_BASE+0x10) /* Extended interrupt IRQ register (RW,32,0x00000000) */
#define IDM         (EIC_BASE+0x14) /* Extended interrupt Mode control register (RW,32,0x00000000) */
#define ILC         (EIC_BASE+0x18) /* Extended interrupt IRQ Level control register
                                       (RW,32,0x00000000) */

/* bit field of IRCL register */
#define IRCL_IRCL   (0x0000007F)    /* IRCL[6:0] */

/* bit field of IRQA register */
#define IRQA_IRQ16  (0x00000001)    /* IRQ16 */
#define IRQA_IRQ17  (0x00000002)    /* IRQ17 */
#define IRQA_IRQ18  (0x00000004)    /* IRQ18 */
#define IRQA_IRQ19  (0x00000008)    /* IRQ19 */
#define IRQA_IRQ20  (0x00000010)    /* IRQ20 */
#define IRQA_IRQ21  (0x00000020)    /* IRQ21 */
#define IRQA_IRQ22  (0x00000040)    /* IRQ22 */
#define IRQA_IRQ23  (0x00000080)    /* IRQ23 */
#define IRQA_IRQ24  (0x00000100)    /* IRQ24 */
#define IRQA_IRQ25  (0x00000200)    /* IRQ25 */
#define IRQA_IRQ26  (0x00000400)    /* IRQ26 */
#define IRQA_IRQ27  (0x00000800)    /* IRQ27 */
#define IRQA_IRQ28  (0x00001000)    /* IRQ28 */
#define IRQA_IRQ29  (0x00002000)    /* IRQ29 */
#define IRQA_IRQ30  (0x00004000)    /* IRQ30 */
#define IRQA_IRQ31  (0x00008000)    /* IRQ31 */

/* bit field of IDM register */
#define IDM_IDM22   (0x00000040)    /* IRQ22 */
#define IDM_IDM26   (0x00000400)    /* IRQ26 */
#define IDM_IDM28   (0x00001000)    /* IRQ28 */
#define IDM_IDM30   (0x00004000)    /* IRQ31 */
#define IDM_IDMP22  (0x00000080)    /* IRQ22 */
#define IDM_IDMP26  (0x00000800)    /* IRQ26 */
#define IDM_IDMP28  (0x00002000)    /* IRQ28 */
#define IDM_IDMP30  (0x00008000)    /* IRQ31 */
#define IDM_INT_L_L (0x00000000)    /* level sensing, interrupt occurs when 'L' */
#define IDM_INT_L_H (0x0000AAAA)    /* level sensing, interrupt occurs when 'H' */
#define IDM_INT_E_F (0x00005555)    /* edge sensing, interrupt occurs when falling edge */
#define IDM_INT_E_R (0x0000FFFF)    /* edge sensing, interrupt occurs when rising edge */
#define IDM_IRQ22   (0x000000C0)    /* IRQ22 */
#define IDM_IRQ26   (0x00000C00)    /* IRQ26 */
#define IDM_IRQ28   (0x00003000)    /* IRQ28 */
#define IDM_IRQ31   (0x0000C000)    /* IRQ31 */


/* bit field of ILC register */
#define ILC_INT_LV1 (0x11111111)    /* interrupt level 1 */
#define ILC_INT_LV2 (0x22222222)    /* interrupt level 2 */
#define ILC_INT_LV3 (0x33333333)    /* interrupt level 3 */
#define ILC_INT_LV4 (0x44444444)    /* interrupt level 4 */
#define ILC_INT_LV5 (0x55555555)    /* interrupt level 5 */
#define ILC_INT_LV6 (0x66666666)    /* interrupt level 6 */
#define ILC_INT_LV7 (0x77777777)    /* interrupt level 7 */
#define ILC_ILC16   (0x00000007)    /* IRQ16, IRQ17 */
#define ILC_ILC18   (0x00000070)    /* IRQ18, IRQ19 */
#define ILC_ILC20   (0x00000700)    /* IRQ20, IRQ21 */
#define ILC_ILC22   (0x00007000)    /* IRQ22, IRQ23 */
#define ILC_ILC24   (0x00070000)    /* IRQ24, IRQ25 */
#define ILC_ILC26   (0x00700000)    /* IRQ26, IRQ27 */
#define ILC_ILC28   (0x07000000)    /* IRQ28, IRQ29 */
#define ILC_ILC30   (0x70000000)    /* IRQ30, IRQ31 */


/*****************************************************/
/*    DRAM control register                          */
/*****************************************************/
#define DCR_BASE    (0x78180000)    /* base address */
#define DBWC        (DCR_BASE+0x00) /* DRAM Bus Width control register (RW,32,0x00000000) */
#define DRMC        (DCR_BASE+0x04) /* DRAM control register (RW,32,0x00000000) */
#define DRPC        (DCR_BASE+0x08) /* DRAM Attribute parameter Setup register (RW,32,0x00000000)*/
#define SDMD        (DCR_BASE+0x0C) /* SDRAM Mode Setup register (RW,32,0x00000001) */
#define DCMD        (DCR_BASE+0x10) /* DRAM Command register (RW,32,0x00000000) */
#define RFSH0       (DCR_BASE+0x14) /* DRAM Refresh Cycle register 0 (RW,32,0x00000000) */
#define PDWC        (DCR_BASE+0x18) /* Power Down Mode control register (RW,32,0x00000003) */
#define RFSH1       (DCR_BASE+0x1C) /* DRAM Refresh Cycle register 1 (RW,32,0x00000000) */

/* bit field of DBWC register */
#define DBWC_DBDRAM0    (0x00000000)    /* DRAM disable */
#define DBWC_DBDRAM8    (0x00000001)    /* 8bit width */
#define DBWC_DBDRAM16   (0x00000002)    /* 16bit width */

/* bit field of DRMC register */
#define DRMC_8bit       (0x00000000)    /* DRAM column length : 8bit */
#define DRMC_9bit       (0x00000001)    /* DRAM column length : 9bit */
#define DRMC_10bit      (0x00000002)    /* DRAM column length : 10bit */
#define DRMC_SDRAM      (0x00000000)    /* DRAM architecture : SDRAM */
#define DRMC_EDO        (0x00000004)    /* DRAM architecture : EDO-DRAM */
#define DRMC_2CLK       (0x00000000)    /* SDRAM pre-charge latency : 2clock */
#define DRMC_CAS        (0x00000010)    /* SDRAM pre-charge latency : same as CAS latency */
#define DRMC_PD_DIS     (0x00000000)    /* automatic shift to SDRAM power down mode : disable */
#define DRMC_PD_EN      (0x00000040)    /* automatic shift to SDRAM power down mode : enable */
#define DRMC_CBR_STOP   (0x00000000)    /* CBR refresh : stop */
#define DRMC_CBR_EXE    (0x00000080)    /* CBR refresh : execution */

/* bit field of DRPC register */
#define DRPC_DRAMSPEC   (0x0000000F)    /* DRAMSPEC[3:0] */

/* bit field of SDMD register */
#define SDMD_CL2    (0x00000000)    /* SDRAM CAS latency : 2 */
#define SDMD_CL3    (0x00000001)    /* SDRAM CAS latency : 3 */
#define SDMD_MODEWR (0x00000080)    /* setting operation : valid */

/* bit field of DCMD register */
#define DCMD_S_NOP      (0x00000000)    /* No operation */
#define DCMD_S_PALL     (0x00000004)    /* SDRAM all bank pre-charge command */
#define DCMD_S_REF      (0x00000005)    /* SDRAM CBR refresh command */
#define DCMD_S_SELF     (0x00000006)    /* SDRAM self refresh start command */
#define DCMD_S_SREX     (0x00000007)    /* SDRAM self refresh stop command */
#define DCMD_EDO_NOP    (0x00000000)    /* No operation */
#define DCMD_EDO_PC     (0x00000004)    /* EDO-DRAM pre-charge cycle */
#define DCMD_EDO_REF    (0x00000005)    /* EDO-DRAM CBR refresh cycle */
#define DCMD_EDO_SELF   (0x00000006)    /* EDO-DRAM self refresh start cycle */
#define DCMD_EDO_SREX   (0x00000007)    /* EDO-DRAM self refresh stop cycle */

/* bit field of RFSH0 register */
#define RFSH0_RCCON  (0x00000001)   /* RCCON bit, refresh frequency = refclk(RFSH1)*2(RCCON=0) */
#define RFSH0_SINGLE (0x00000000)   /* RCCON bit, refresh frequency = refclk(RFSH1)  (RCCON=1) */
/* bit field of RFSH1 register */
#define RFSH1_RFSEL1    (0x000007FF)    /* RFSEL1[10:0], refckl(RFSH1) = CCLK/RFSEL1[10:0] */

/* bit field of PDWC register */
#define PDWC_1  (0x00000000)    /* when  1 or more cycles of idol state continue,
                                   it shifts to power down mode. */
#define PDWC_2  (0x00000001)    /*                   :                   */
#define PDWC_3  (0x00000002)    /*                   :                   */
#define PDWC_4  (0x00000003)    /*                   :                   */
#define PDWC_5  (0x00000004)    /*                   :                   */
#define PDWC_6  (0x00000005)    /*                   :                   */
#define PDWC_7  (0x00000006)    /*                   :                   */
#define PDWC_8  (0x00000007)    /*                   :                   */
#define PDWC_9  (0x00000008)    /*                   :                   */
#define PDWC_10 (0x00000009)    /*                   :                   */
#define PDWC_11 (0x0000000A)    /*                   :                   */
#define PDWC_12 (0x0000000B)    /*                   :                   */
#define PDWC_13 (0x0000000C)    /*                   :                   */
#define PDWC_14 (0x0000000D)    /*                   :                   */
#define PDWC_15 (0x0000000E)    /*                   :                   */
#define PDWC_16 (0x0000000F)    /* when 16 or more cycles of idol state continue,
                                   it shifts to power down mode. */



/*****************************************************/
/*    SSIO control register                          */
/*****************************************************/
#define SSIO_BASE   (0xB7B01000)        /* base address */
#define SSIOBUF     (SSIO_BASE+0x00)    /* transmiting/receiving buffer register (RW,8,0x00) */
#define SSIOST      (SSIO_BASE+0x04)    /* SSIO status register (RW,8,0x00) */
#define SSIOINT     (SSIO_BASE+0x08)    /* SSIO interrupt demand register (RW,8,0x00) */
#define SSIOINTEN   (SSIO_BASE+0x0C)    /* SSIO interrupt enable register (RW,8,0x00) */
#define SSIOCON     (SSIO_BASE+0x10)    /* SSIO control register (RW,8,0x00) */
#define SSIOTSCON   (SSIO_BASE+0x14)    /* SSIO test control register (RW,8,0x00) */

/* bit field of SSIOBUF register */
#define SSIOSTA_DUMMY   (0xFF)

/* bit field of SSIOST register */
#define SSIOSTA_BUSY    (0x01)  /* transmiting/receiving buffer busy */
#define SSIOSTA_OERR    (0x02)  /* overrun error */

/* bit field of SSIOINT register */
#define SSIOCON_TXCMP   (0x01)  /* transmit complete */
#define SSIOCON_RXCMP   (0x02)  /* receive complete */
#define SSIOCON_TREMP   (0x04)  /* transmit empty */

/* bit field of SSIOINTEN register */
#define SSIOCON_TXCMPEN (0x01)  /* transmit complete enable */
#define SSIOCON_RXCMPEN (0x02)  /* receive complete enable */
#define SSIOCON_TREMPEN (0x04)  /* transmit empty enable */

/* bit field of SSIOCON register */
#define SSIOCON_SLLSB   (0x00)  /* LSB */
#define SSIOCON_SLMSB   (0x20)  /* MSB */
#define SSIOCON_SLAVE   (0x10)  /* Slave */
#define SSIOCON_MASTER  (0x00)  /* Master */
#define SSIOCON_8CCLK   (0x00)  /* 1/8CCLK */
#define SSIOCON_16CCLK  (0x01)  /* 1/16CCLK */
#define SSIOCON_32CCLK  (0x02)  /* 1/32CCLK */

/* bit field of SSIOTSCON register */
#define SSIOTSCON_LBTST (0x80)  /* loop back test mode on*/
#define SSIOTSCON_NOTST (0x00)  /* test mode off */

/*****************************************************/
/*    I2C control register                          */
/*****************************************************/
#define I2C_BASE    (0xB7800000)    /* base address */
#define I2CCON      (I2C_BASE+0x00) /* I2C control register (RW,8,0x00) */
#define I2CSAD      (I2C_BASE+0x04) /* I2C slave address mode setting register (RW,8,0x00) */
#define I2CCLR      (I2C_BASE+0x08) /* I2C transmit speed setting register (RW,8,0x00) */
#define I2CSR       (I2C_BASE+0x0C) /* I2C status register (R,8,0x00) */
#define I2CIR       (I2C_BASE+0x10) /* I2C interrupt demand register (RW,8,0x00) */
#define I2CIMR      (I2C_BASE+0x14) /* I2C interrupt mask register  (RW,8,0x00) */
#define I2CDR       (I2C_BASE+0x18) /* I2C transmiting/receiving buffer register (RW,8,0x00) */
#define I2CBC       (I2C_BASE+0x1C) /* I2C transmit speed setting register (RW,8,0x00) */

/* bit field of I2CCON register */
#define I2CCON_EN       (0x01)  /* restart sequence start */
#define I2CCON_OC       (0x02)  /* I2C-bus hold */
#define I2CCON_STCM     (0x04)  /* communication start */
#define I2CCON_RESTR    (0x08)  /* carry out restart */
#define I2CCON_START    (0x10)  /* exist START byte */

/* bit field of I2CSAD register */
#define I2CSAD_RW_SND   (0x00)  /* data transmiting mode */
#define I2CSAD_RW_REC   (0x01)  /* data receiving mode */

/* bit field of I2CCLR register */
#define I2CCLR_CMD1 (0x00)  /* Standard-mode */
#define I2CCLR_CMD4 (0x01)  /* Fast-mode */

/* bit field of I2CSR register */
#define I2CSR_DAK   (0x01)  /* data ACKnowledge no receive */
#define I2CSR_AAK   (0x02)  /* slave address ACKnowledge no receive */

/* bit field of I2CIR register */
#define I2CIR_IR    (0x01)  /* interrupt demand */

/* bit field of I2CIMR register */
#define I2CIMR_MF   (0x01)  /* interrupt mask set */

/* bit field of I2CDR register */

/* bit field of I2CBC register */
#define I2CBC_100K60    (0x4B)  /* HCLK=60MHz,I2CMD=100kHz */
#define I2CBC_400K60    (0x13)  /* HCLK=60MHz,I2CMD=400kHz */
#define I2CBC_100K33    (0x2A)  /* HCLK=33MHz,I2CMD=100kHz */
#define I2CBC_400K33    (0x0B)  /* HCLK=33MHz,I2CMD=400kHz */
#define I2CBC_100K25    (0x20)  /* HCLK=25MHz,I2CMD=100kHz */
#define I2CBC_400K25    (0x08)  /* HCLK=25MHz,I2CMD=400kHz */
#define I2CBC_100K20    (0x19)  /* HCLK=20MHz,I2CMD=100kHz */
#define I2CBC_400K20    (0x07)  /* HCLK=20MHz,I2CMD=400kHz */


/*---------------------------------- ML675001 ------------------------------------*/
/*****************************************************/
/*    cache control register                         */
/*****************************************************/
#define CACHE_BASE  (0x78200000)        /* base address */
#define CON         (CACHE_BASE+0x04)   /* cache control register */
#define CACHE       (CACHE_BASE+0x08)   /* cachable register */
#define FLUSH       (CACHE_BASE+0x1C)   /* FLUSH register */
//#define DBEN        (CACHE_BASE+0x60)   /* DEBUG enable register */

/* bit field of CON register */
#define CON_WAY0    (0x00000000)    /* select Way0 (LCK bits) */
#define CON_WAY1    (0x10000000)    /* select Way1 (LCK bits) */
#define CON_WAY2    (0x20000000)    /* select Way2 (LCK bits) */
#define CON_WAY3    (0x30000000)    /* select Way3 (LCK bits) */
#define CON_LOAD    (0x08000000)    /* Load mode (F bit) */
#define CON_LOCK0   (0x00000000)    /* lock 0 Way (BNK bits) */
#define CON_LOCK1   (0x02000000)    /* lock 1 Way (BNK bits) */
#define CON_LOCK2   (0x04000000)    /* lock 2 Ways (BNK bits) */
#define CON_LOCK3   (0x06000000)    /* lock 3 Ways (BNK bits) */

/* bit field of CACHE register */
#define CACHE_BANK0     (0x00010000)    /* Bank0  : Cache enable */
#define CACHE_BANK8     (0x00000001)    /* Bank8  : Cache enable */
#define CACHE_BANK9     (0x00000002)    /* Bank9  : Cache enable */
#define CACHE_BANK10    (0x00000004)    /* Bank10 : Cache enable */
#define CACHE_BANK11    (0x00000008)    /* Bank11 : Cache enable */
#define CACHE_BANK12    (0x00000010)    /* Bank12 : Cache enable */
#define CACHE_BANK13    (0x00000020)    /* Bank13 : Cache enable */
#define CACHE_BANK24    (0x00000100)    /* Bank24 : Cache enable */
#define CACHE_BANK25    (0x00000200)    /* Bank25 : Cache enable */
#define CACHE_BANK26    (0x00000400)    /* Bank26 : Cache enable */
#define CACHE_BANK27    (0x00000800)    /* Bank27 : Cache enable */
#define CACHE_BANK28    (0x00001000)    /* Bank28 : Cache enable */
#define CACHE_BANK29    (0x00002000)    /* Bank29 : Cache enable */

/* bit field of FLUSH register */
#define FLUSH_FLUSH (0x00000001)    /* flush cache memory */

/* bit field of DEBUG register */
//#define DEBUG_DBG   (0x00000001)    /* debug mode */



/*****************************************************/
/*    Chip configuration register                    */
/*****************************************************/
#define CCR_BASE    (0xB7000000)    /* base address */
#define GPCTL       (CCR_BASE+0x00) /* port function control register (RW,16,0x0000) */
#define BCKCTL      (CCR_BASE+0x04) /* clock control register (RW,16,0x0000) */
#define CSSW        (CCR_BASE+0x08) /* external ROM/RAM chip cell control register (RW,16,0x0000) */
#define ROMSEL      (CCR_BASE+0x0C) /* ROM select register (RW,8,0x00) */

/* bit field of GPCTL */
#define GPCTL_GPCTL     (0x7FFF)    /* GPCTL[14:0] */
#define GPCTL_UART      (0x0001)    /* select 2nd function (UART) */
#define GPCTL_SIO       (0x0002)    /* select 2nd function (SIO) */
#define GPCTL_EXBUS     (0x0004)    /* select 2nd function (external bus) */
#define GPCTL_DMA0      (0x0008)    /* select 2nd function (DMA CH0) */
#define GPCTL_DMA1      (0x0010)    /* select 2nd function (DMA CH1) */
#define GPCTL_PWM       (0x0020)    /* select 2nd function (PWM) */
#define GPCTL_XWAIT     (0x0040)    /* select 2nd function (external bus wait input) */
#define GPCTL_XWR       (0x0080)    /* select 2nd function (external bus data direction) */
#define GPCTL_SSIO0     (0x0100)    /* select 2nd function (SSIO) */
#define GPCTL_I2C       (0x0200)    /* select 2nd function (I2C) */
#define GPCTL_EXINT0    (0x0400)    /* select 2nd function (EXINT0) */
#define GPCTL_EXINT1    (0x0800)    /* select 2nd function (EXINT1) */
#define GPCTL_EXINT2    (0x1000)    /* select 2nd function (EXINT2) */
#define GPCTL_EXINT3    (0x2000)    /* select 2nd function (EXINT3) */
#define GPCTL_EFIQ_N    (0x4000)    /* select 2nd function (EFIQ_N) */

/* bit field of BCKCTL */
#define BCKCTL_AD   (0x0001)    /* ADC */
#define BCKCTL_PWM  (0x0002)    /* PWM */
#define BCKCTL_ART0 (0x0004)    /* auto reload timer(CH0) */
#define BCKCTL_ART1 (0x0008)    /* auto reload timer(CH1) */
#define BCKCTL_ART2 (0x0010)    /* auto reload timer(CH2) */
#define BCKCTL_ART3 (0x0020)    /* auto reload timer(CH3) */
#define BCKCTL_ART4 (0x0040)    /* auto reload timer(CH4) */
#define BCKCTL_ART5 (0x0080)    /* auto reload timer(CH5) */
#define BCKCTL_DRAM (0x0100)    /* DRAM controller */
#define BCKCTL_DMA  (0x0200)    /* DMAC */
#define BCKCTL_UART (0x0400)    /* UART */
#define BCKCTL_SSIO (0x0800)    /* SSIO */
#define BCKCTL_I2C  (0x1000)    /* I2C */

/* bit field of CSSW register */
#define CSSW_CHG        (0x0001)    /* CHG bit */
#define CSSW_CHG_SET    (0xA5A5)    /* set CHG */
#define CSSW_CHG_RESET  (0x5A5A)    /* reset CHG */


/*****************************************************/
/*    interrupt number                               */
/*****************************************************/
#define INT_SYSTEM_TIMER    0
#define INT_WDT             1
#define INT_IVT             2
#define INT_GPIOA           4
#define INT_GPIOB           5
#define INT_GPIOC           6
#define INT_GPIOD           7
#define INT_GPIOE           7
#define INT_SOFTIRQ         8
#define INT_UART            9
#define INT_SIO             10
#define INT_AD              11
#define INT_PWM0            12
#define INT_PWM1            13
#define INT_SSIO            14
#define INT_I2C             15
#define INT_TIMER0          16
#define INT_TIMER1          17
#define INT_TIMER2          18
#define INT_TIMER3          19
#define INT_TIMER4          20
#define INT_TIMER5          21
#define INT_EX0             22
#define INT_DMA0            24
#define INT_DMA1            25
#define INT_EX1             26
#define INT_EX2             28
#define INT_EX3             31

#ifdef __cplusplus
};      /* End of 'extern "C"' */
#endif
#endif  /* End of ML675001.h */
