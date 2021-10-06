; vdfs.asm
;
; VDFS for B-Em
; Copyright 2018 Steve Fosdick.
;
; This module implements the ROM part of a Virtual Disk Filing
; System, one in which a part of filing system of the host is
; exposed to the guest through normal OS calls on the guest.
;
; This particular implementation comes in two parts:
;
; 1. This ROM which runs on the guest, the emulated BBC.  This
;    forwards ROM service calls to the module running in the host
;    and, when selected as the current filing system, takes over
;    the filing system vectors.  Calls made to the filing system
;    vectors are also forwarded to the host module.  Under control
;    of the host module this ROM then performs certain operations
;    with the BBC MOS.
;
; 2. The vdfs.c module which runs as part of the emultor, on the host.
;
; This ROM passes control to the host module by writing to a pair
; of ports in the expansion area in FRED.  Two further ports are
; used to communicate a small set of flags and the number of the
; filing system VDFS was selected as.
;
; The host module returns control to this ROM by jumping to an
; address in a dispatch table whose size and address are stored
; at the very beginning of the ROM where the language entry point
; would be if this were a language ROM.

fsno_vdfs   =   &11                 ; VDFS normal filing system no.
fsno_dfs    =   &04                 ; DFS filing system no.
fsno_adfs   =   &08                 ; ADFS filing system no.

; Values for the flags in port_flags, below

claim_adfs  =   &80
claim_dfs   =   &40

; The interface between this ROM and the vdfs.c module, four ports
; in the FRED 1Mhz bus area.

port_flags  =   &FC5C               ; various flags.
port_fsid   =   &FC5D               ; filing system ID.
port_cmd    =   &FC5E               ; execute actions on host.
port_a      =   &FC5F               ; store A ready for command.

; Base of some temporary space used during *CAT, *EX, *INFO, *LCAT

cat_tmp     =   &0100
stack       =   &0100

; OS entry points.

OSCLI       =   &FFF7
OSBYTE      =   &FFF4
OSWORD      =   &FFF1
OSWRCH      =   &FFEE
OSNEWL      =   &FFE7
OSASCI      =   &FFE3
OSFILE      =   &FFDD
OSARGS      =   &FFDA
OSBGET      =   &FFD7
OSBPUT      =   &FFD4
OSGBPB      =   &FFD1
OSFIND      =   &FFCE
OSRDRM      =   &FFB9

; OS vectors.

BRKV        =   &0202
EVNTV       =   &0220

; Zero page workspace - note overlaps.

romtab      =   &A8
romid       =   &AA
copywr      =   &AB

dmpadd      =   &A8
dmpcnt      =   &AC

filechan    =   &A8
lineno      =   &A9

ltflag      =   &AB
ltpchr      =   &AC

gbpbcmd     =   &AB
argsblk     =   &AC

prtextws    =   &A8

; Standard BBC Micro service ROM header except that what would
; be the language entry point in a language ROM contains details
; of the dispatch table the vdfs.c module uses to transfer control
; top this ROM.

            org     &8000
.start      equb    (dispend-disptab)/2 ; no. entires in table.
            equw    disptab
            jmp     service
            equb    &82                 ; ROM type.
            equb    copyright-start
.romversion equb    &06
.romtitle   equs    "B-Em VDFS", &00
            include "version.asm"
.copyright  equb    &00
            equs    "(C) 2018 Steve Fosdick, GPL3", &00
            equd    0
.banner     equs    "Virtual DFS", &00
.msg_nclaim equs    "ADFS is not being claimed", &00
.msg_claim  equs    "ADFS is being claimed", &00
.msg_nopen  equs    "No open files",&0d,&0a,&00
.msg_files  equs    "Chan Name       Mode Ptr      Ext",&0d,&0a,&00
.msg_option equs    "Option: ",0
.msg_off    equs    "0 (Off)",0
.msg_load   equs    "1 (Load)",0
.msg_run    equs    "2 (Run)",0
.msg_exec   equs    "3 (Exec)",0
.msg_dir    equs    "Dir. ", 0
.msg_lib    equs    "Lib. ", 0
.msg_mmb    equs    " MMB Support",&00

; The dispatch table.  This needs to be in the same order as
; enum vdfs_action in the vdfs.c module.

.disptab    equw    serv_done       ; all done.
            equw    fsstart         ; normal filing system start.
            equw    fsboot          ; filing system start at boot.
            equw    fs_info         ; give OS filing system info.
            equw    fs_claim        ; say which filing systems claimed.
            equw    dir_cat         ; *CAT  via OSFSC
            equw    dir_ex          ; *EX   via OSFSC
            equw    pr_all          ; *INFO via ISFSC
            equw    cmd_dump        ; *DUMP
            equw    cmd_list        ; *LIST
            equw    cmd_print       ; *PRiNT
            equw    cmd_type        ; *TYPE
            equw    cmd_roms        ; *ROMS
            equw    help_short
            equw    help_all
            equw    help_vdfs
            equw    help_utils
            equw    help_sram
            equw    tube_exec       ; start execution ob tube proc.
            equw    tube_init       ; initialise tube.
            equw    tube_explode    ; explode character set for tube.
            equw    osw7f_stat
            equw    break_type
            equw    cmd_files       ; *FILES command.
            equw    none_open       ; "No open files" message.
            equw    osw_tail        ; finish a command with an OSWORD call.
            equw    close_all
            equw    cmd_build       ; *BUILD
            equw    cmd_append      ; *APPEND.
            equw    prt_dbase       ; helper for *DBASE.
            equw    mmb_dboot       ; Boot on behalf of *DBOOT.
            equw    stack_print     ; Print whatever is at the bottom of the stack.
            equw    newlret         ; Print newline, set A=0, RTS
            equw    mmb_dabout      ; Print MMB "about" message.
.dispend

; Stubs to transfer control to the vdfs.c module.

.service    sta     port_a
            lda     #&00
            sta     port_cmd
.serv_done  rts
.file       sta     port_a
            lda     #&01
            sta     port_cmd
            rts
.args       sta     port_a
            lda     #&02
            sta     port_cmd
            rts
.bget       sta     port_a
            lda     #&03
            sta     port_cmd
            rts
.bput       sta     port_a
            lda     #&04
            sta     port_cmd
            rts
.gbpb       sta     port_a
            lda     #&05
            sta     port_cmd
            rts
.find       sta     port_a
            lda     #&06
            sta     port_cmd
            rts
.fsc        sta     port_a
            lda     #&07
            sta     port_cmd
            rts

; Filing system startup.  This is called directly when the filing
; system was selected by ROM service call &12 or by OS command.

.fsstart
{
            tya
            pha
            lda     #&06            ; Inform current FS new FS taking over
            jsr     callfscv
            ldx     #&00
            ldy     #&1B            ; Set up new vectors to point
.vecloop    tya                     ; into the extended vector area
            sta     &212,x          ; at &FFxx
            lda     #&FF
            sta     &213,x
            lda     vectab,x        ; Set the extended vector to the
            inx                     ; address in this ROM.
            sta     &0d9f,y
            iny
            lda     vectab,x
            inx
            sta     &0d9f,y
            iny
            lda     &F4             ; and include our ROM number in
            sta     &0d9f,y         ; the extended vector.
            iny
            cpx     #&0e
            bne     vecloop
            pla
            pha
            sta     port_fsid
            lda     #&8f
            ldx     #&0f
            jsr     OSBYTE          ; Notify that vectors have changed
            pla
            tay
            lda     #&00
            rts
.callfscv   jmp     (&021E)
.vectab     equw    file
            equw    args
            equw    bget
            equw    bput
            equw    gbpb
            equw    find
            equw    fsc
}

.prtitle    ldx     #&00

.prmsg
{
            lda     banner,x
.loop       jsr     OSWRCH
            inx
            lda     banner,x
            bne     loop
            rts
}

; Filing system boot.  This is called in response to ROM service
; call &03 when the code in vdfs.c has determined that VDFS is the
; filing system selected.

.fsboot     pha                     ; save the OPT 4 setting.
            tya                     ; save the boot flag.
            pha
            jsr     prtitle         ; announce the filing system
            jsr     OSNEWL
            jsr     OSNEWL
            ldy     #fsno_vdfs
            jsr     fsstart         ; same setup as for call &12.
            pla
            bne     noboot          ; process !BOOT?
            pla                     ; get back OPT 4
            bne     doboot
            rts
.noboot     pla
            lda     #&00
            rts

; MMB boot option.  This is used for MMFS emulation to check th
; boot option on the DFS disc and then fall through to process
; this exactly the same as when VDFS is starting and looking at the
; boot option.

.mmb_dboot  lda     #&0d            ; Set up data address of OSGBPB.
            ldx     #<stack
            ldy     #>stack
            stx     stack
            sta     stack+1
            sty     stack+2
            stx     stack+3
            stx     stack+4
            lda     #&05            ; Ask the current filing system for
            jsr     OSGBPB          ; the title/boot option.
            ldx     stack+&0d       ; Get length of title.
            lda     stack+&0e,x     ; Get boot option.
            beq     noboot

; Process boot option.  This is common to VDFS booting as a filing
; system and the MMFS DBOOT command.

.doboot     cmp     #&04
            bcs     noboot2
            tax
            lda     bootcmd-1,x
            sta     stack
            ldx     #&07
.bootlp     lda     bootnam,x
            sta     stack+1,x
            dex
            bpl     bootlp
            ldx     #<stack
            ldy     #>stack
            jsr     OSCLI
            lda     #&00
.noboot2    rts
.noboot1    pla
            lda     #&00
            rts
.bootcmd    equs    "LRE"
.bootnam    equs    ".!BOOT",&0d

; Filing system info.  This is in response to ROM service call
; &25 which is master-specific.  This is where we tell the OS
; which filing system names, numbers and ranges of file handles
; we can respond to.  Which filing system entries we give back
; depends on what filing systems we are claiming to be.

.fs_info
{
            ldx     #&00            ; always copy the VDFS entry.
.loopv      lda     vdfs_ent,x
            sta     (&f2),y
            iny
            inx
            cpx     #&0b
            bne     loopv
            bit     port_flags      ; only copy ADFS if we're
            bpl     noadfs          ; claiming to be ADFS.
            ldx     #&00
.loopa      lda     adfs_ent,x
            sta     (&f2),y
            iny
            inx
            cpx     #&0b
            bne     loopa
            bit     port_flags      ; only copy DFS (DISC/DISK) if
.noadfs     bvc     nodfs           ; we're claiming to be DFS.
            ldx     #&00
.loopd      lda     dfs_ents,x
            sta     (&f2),y
            iny
            inx
            cpx     #&16
            bne     loopd
.nodfs      lda     #&25
            rts
.vdfs_ent   equs    "VDFS    ", &60, &7F, &11
.adfs_ent   equs    "ADFS    ", &30, &3A, &08
.dfs_ents   equs    "DISK    ", &11, &15, &04
            equs    "DISC    ", &11, &15, &04
}

; Filing system claim.  This prints info about which other filing
; systems VDFS is claiming.

.fs_claim
{
            bit     port_flags
            bmi     adfs_yes
            ldx     #msg_nclaim-banner
            bne     adfs_msg
.adfs_yes   ldx     #msg_claim-banner
.adfs_msg   jsr     prmsg
            jsr     OSNEWL
            bit     port_flags
            bvs     dfs_yes
            ldx     #msg_nclaim-banner+1
            bne     dfs_msg
.dfs_yes    ldx     #msg_claim-banner+1
.dfs_msg    jsr     prmsg
}

.newlret    jsr     OSNEWL
            lda     #&00
            rts

; Routines to list information about files.

            macro   outchr char
            lda     #char
            jsr     OSWRCH
            endmacro

            macro   outtwo char
            outchr  char
            jsr     OSWRCH
            endmacro

            macro   outspc
            outchr  ' '
            endmacro

            macro   twospc
            outtwo  ' '
            endmacro

            macro   outcnt char
            outchr  char
            inx
            endmacro

            macro   pr_attr mask, char
            lda     #mask
            bit     cat_tmp+&0c
            beq     notset
            outcnt  char
.notset
            endmacro

.pr_dfs     ldx     #&00
            lda     cat_tmp         ; print DFS directory.
            jsr     OSWRCH
            inx
            lda     cat_tmp+1
            jsr     OSWRCH
            inx

.pr_basic
{
            ldy     #&02            ; print characters of the name.
.loop       lda     cat_tmp,y
            jsr     OSWRCH
            inx
            iny
            cpy     #&0c
            bne     loop
            outcnt  ' '
            jsr     OSWRCH
            inx
            bit     cat_tmp+&0d     ; test most significant byte of
            bvc     notdir          ; attributes for the directory flag.
            outcnt  'D'
.notdir     pr_attr &08, 'L'
            pr_attr &02, 'W'
            pr_attr &01, 'R'
            rts
}

.pr_others  outcnt  '/'
            pr_attr &20, 'w'
            pr_attr &10, 'r'
            rts

.pr_pad
{
            cpx     #&14
            bcs     done
            lda     #' '
.spcloop    jsr     OSWRCH
            inx
            cpx     #&14
            bne     spcloop
.done       rts
}

.hexfour    lda     cat_tmp+3,x
            jsr     hexbyt
            lda     cat_tmp+2,x
            jsr     hexbyt
            lda     cat_tmp+1,x
            jsr     hexbyt
            lda     cat_tmp,x

.hexbyt     pha
            lsr     A
            lsr     A
            lsr     A
            lsr     A
            jsr     hexnyb
            pla
            and     #&0f
.hexnyb     ora     #'0'
            cmp     #'9'+1
            bcc     ddig
            adc     #&06
.ddig       jmp     OSWRCH


            macro   hexout addr
            lda     addr
            jsr     hexbyt
            endmacro

.pr_all     ldx     #&00
            jsr     pr_basic
            jsr     pr_others
            jsr     pr_pad
            twospc
            hexout  cat_tmp+&11
            hexout  cat_tmp+&10
            hexout  cat_tmp+&0f
            hexout  cat_tmp+&0e
            twospc
            hexout  cat_tmp+&15
            hexout  cat_tmp+&14
            hexout  cat_tmp+&13
            hexout  cat_tmp+&12
            twospc
            hexout  cat_tmp+&19
            hexout  cat_tmp+&18
            hexout  cat_tmp+&17
            hexout  cat_tmp+&16
            jmp     OSNEWL

.opt_tab    equb    msg_off-banner
            equb    msg_load-banner
            equb    msg_run-banner
            equb    msg_exec-banner

.dir_cat    lda     cat_tmp+&01     ; Is there a title?
            beq     notitle
            ldx     #&00
.title_lp   jsr     OSWRCH          ; print the title.
            inx
            lda     cat_tmp+&01,x
            bne     title_lp
            lda     #' '            ; pad out to 20 characters.
.title_pad  jsr     OSWRCH
            inx
            cpx     #&14
            bcc     title_pad
.notitle    ldx     #msg_option-banner
            jsr     prmsg           ; print "Option: "
            lda     cat_tmp         ; get boot option.
            cmp     #&04
            bcs     opt_hex
            tax
            lda     opt_tab,x
            tax
            jsr     prmsg
            jmp     opt_done
.opt_hex    jsr     hexbyt
.opt_done   jsr     OSNEWL
            ldx     #msg_dir-banner ; print "Dir. "
            jsr     prmsg
            lda     #&10            ; get the directory as text.
            sta     port_cmd
            ldx     #&00
            lda     cat_tmp         ; is there one?
            beq     nodir
.cat_dirlp  jsr     OSWRCH
            inx
            lda     cat_tmp,x
            bne     cat_dirlp
.nodir      lda     #' '            ; pad to 20 characters.
.dir_pad    jsr     OSWRCH
            inx
            cpx     #&0f
            bne     dir_pad
            ldx     #msg_lib-banner ; print "Lib. "
            jsr     prmsg
            lda     #&11            ; get the library as text.
            sta     port_cmd
            lda     cat_tmp         ; is there one?
            beq     nolib
            ldx     #&00
.cat_liblp  jsr     OSWRCH
            inx
            lda     cat_tmp,x
            bne     cat_liblp
.nolib      jsr     OSNEWL
            jsr     OSNEWL
            lda     #&20            ; DFS mode?
            bit     port_flags
            bne     dfs_cat

            lda     #&0c            ; Fetch the first entry.
            sta     port_cmd
            bcs     cat_done
.cat_loop   ldx     #&00
            jsr     pr_basic
            jsr     pr_others
            jsr     pr_pad
.adfs_cat   lda     #&0c            ; Fetch the next entry.
            sta     port_cmd
            bcc     cat_loop
            jmp     OSNEWL

            lda     #&0d            ; fetch one directory entry.
            sta     port_cmd
            bcs     dfs_none1
.dfs_lp1    jsr     pr_dfs
            jsr     pr_pad
.dfs_cat    lda     #&0d            ; fetch one directory entry.
            sta     port_cmd
            bcc     dfs_lp1         ; end of entries, to 2nd pass.
            jsr     OSNEWL
            jsr     OSNEWL
.dfs_none1  lda     #&0f            ; rewind to first entry again.
            sta     port_cmd
            bcs     cat_done
.dfs_lp2    jsr     pr_dfs
            jsr     pr_pad
            lda     #&0e            ; fetch one directory entry.
            sta     port_cmd
            bcc     dfs_lp2
            jmp     OSNEWL
.cat_done   rts

.dir_ex     lda     #&20            ; DFS mode?
            bit     port_flags
            bne     ex_dfs
            beq     ex_adfs

.ex_adfs_lp jsr     pr_all
.ex_adfs    lda     #&0c
            sta     port_cmd
            bcc     ex_adfs_lp
            rts

.ex_dfs_lp  jsr     pr_all
.ex_dfs     lda     #&0d
            sta     port_cmd
            bcc     ex_dfs_lp
            rts

.not_found
{
            ldx     #end-msg
.loop       lda     msg,x
            sta     stack,x
            dex
            bpl     loop
            jmp     stack
.msg        brk
            equb    &d6
            equs    "Not found"
            equb    &00
.end
}

; The *DUMP command.

.cmd_dump
{
            lda     #&40
            jsr     OSFIND
            tay
            beq     not_found
            pha
            lda     dmpcnt          ; Need to seek?
            ora     dmpcnt+1
            ora     dmpcnt+2
            ora     dmpcnt+3
            beq     noseek
            lda     #&01
            ldx     #dmpcnt
            jsr     OSARGS
.noseek     lda     #&87            ; find screen mode.
            jsr     OSBYTE
            lda     #&08
            cpy     #&00
            beq     wide
            cpy     #&03
            bne     narrow
.wide       asl     a
.narrow     sta     dmpcnt
            pla
            tay
            bit     &FF
            bmi     gotesc
.linlp      lda     dmpadd+2
            jsr     hexbyt
            lda     dmpadd+1
            jsr     hexbyt
            lda     dmpadd
            jsr     hexbyt
            outspc
            ldx     dmpcnt
.getlp      jsr     OSBGET
            bcs     skip
            sta     stack,X
            jsr     hexbyt
            outspc
            dex
            bne     getlp
            clc
.skip       php
            bcc     ascii
.endlp      outtwo  '*'
            outspc
            lda     #&00
            sta     stack,X
            dex
            bne     endlp
.ascii      ldx     dmpcnt
.asclp      lda     stack,X
            and     #&7F
            cmp     #&7F
            beq     nonprt
            cmp     #&20
            bcs     print
.nonprt     lda     #'.'
.print      jsr     OSWRCH
            dex
            bne     asclp
            jsr     OSNEWL
            plp
            bcs     eof
            lda     dmpcnt
            clc
            adc     dmpadd
            sta     dmpadd
            lda     #&00
            adc     dmpadd+1
            sta     dmpadd+1
            bcc     noinc
            inc     dmpadd+2
.noinc      bit     &FF
            bpl     linlp
.gotesc     outchr  &7E
.eof        lda     #&00
            jmp     OSFIND
}

; Useful subroutines.

.bcdbyt     pha
            php
            lsr     A
            lsr     A
            lsr     A
            lsr     A
            plp
            jsr     bcdnyb
            pla
.bcdnyb     and     #&0f
            bne     bcddig
            bcc     bcddig
            outspc
            sec
            rts
.bcddig     ora     #'0'
            jsr     OSWRCH
            clc
            rts

.outesc
{
            tax
            bmi     high
.high2      cmp     #' '
            bcc     low
            inx
            bmi     del
            cmp     #'|'
            bne     notbar
            jsr     OSWRCH
.notbar     jmp     OSWRCH
.high       outchr  '|'
            outchr  '!'
            txa
            and     #&7f
            tax
            jmp     high2
.low        ora     #&40
            tax
            outchr  '|'
            txa
            jmp     OSWRCH
.del        outchr  '|'
            lda     #'?'
            jmp     OSWRCH
}

.line_num
{
            sed
            sec
            lda     #&00
            adc     lineno
            sta     lineno
            lda     #&00
            adc     lineno+1
            sta     lineno+1
            cld
            sec
            jsr     bcdbyt
            lda     lineno
            php
            lsr     A
            lsr     A
            lsr     A
            lsr     A
            plp
            jsr     bcdnyb
            lda     lineno
            clc
            jsr     bcdnyb
            outspc
            rts
}

; The *LIST and *TYPE commands.

.cmd_list   lda     #&00
            sta     lineno
            sta     lineno+1
            beq     lstype

.cmd_type   lda     #&80

.lstype
{
            sta     ltflag
            lda     #&40
            jsr     OSFIND
            tay
            bne     found
            jmp     not_found
.pline      tax
            jsr     line_num
            txa
.chrlp      cmp     #&0D
            beq     newlin
            cmp     #&0A
            beq     newlin
            sta     ltpchr
            jsr     outesc
.rdchr      jsr     OSBGET
            bcc     chrlp
.eof        jsr     OSNEWL
            lda     #&00
            jmp     OSFIND
.newlin     cmp     ltpchr
            beq     blalin
            pha
            lda     ltpchr
            cmp     #&0D
            beq     nl2nd
            cmp     #&0A
            beq     nl2nd
            pla
            sta     ltpchr
.blalin     jsr     OSNEWL
.found      bit     &FF
            bmi     gotesc
            jsr     OSBGET
            bcs     eof
            bit     ltflag
            bmi     chrlp
            bpl     pline
.nl2nd      lda     #&00
            sta     ltpchr
            pla
            jmp     rdchr
.gotesc     lda     #&7E
            jsr     OSBYTE
            lda     #&00
            jmp     OSFIND
}

.cmd_print
{
            lda     #&40
            jsr     OSFIND
            tay
            bne     found
            jmp     not_found
.chrlp      jsr     OSWRCH
.found      jsr     OSBGET
            bcs     eof
            bit     &FF
            bpl     chrlp
            lda     #&7E
            jsr     OSBYTE
.eof        lda     #&00
            jmp     OSFIND
}

; *APPEND command.

.cmd_append
{
            lda     #&c0            ; Open for update (to keep contents).
            jsr     OSFIND
            tay
            bne     found
            jmp     not_found
.found      sty     filechan
            lda     #&02            ; Read file length.
            ldx     #argsblk
            jsr     OSARGS
            lda     #&12            ; Temporarily set our own ROM bank as RAM.
            sta     port_cmd
            lda     argsblk         ; Set the length in the parameter
            sta     gbpbpb+&09      ; block for OSGBPB.
            lda     argsblk+1
            sta     gbpbpb+&0A
            lda     argsblk+2
            sta     gbpbpb+&0B
            lda     argsblk+3
            sta     gbpbpb+&0C
            lda     #&01            ; First GBPB to set file pointer.
            sta     gbpbcmd
            bne     build_gbpb
}

; *BUILD command.

.cmd_build
{
            lda     #&80            ; Open for writing (discard previous).
            jsr     OSFIND
            tay
            bne     found
            jmp     not_found
.found      sty     filechan
            lda     #&02            ; First GBPB to use current pointer.
            sta     gbpbcmd
            lda     #&12            ; Temporarily set our own ROM bank as RAM.
            sta     port_cmd
}

; Common code for *APPEND/*BUILD using OSGBPB.

.build_gbpb
{
            lda     #&00            ; Start at line zero.
            sta     lineno
            sta     lineno+1
.line_lp    jsr     line_num        ; Print the new line number.
            lda     #&00            ; Read a line of input into RAM
            ldx     #<oswpb         ; at the end of this ROM.
            ldy     #>oswpb
            jsr     OSWORD
            bcs     escape          ; Escape indicates EOF.
            iny
            sty     gbpbpb+5        ; Number of bytes to transfer.
            lda     #&00
            sta     gbpbpb+6
            sta     gbpbpb+7
            sta     gbpbpb+8
            lda     #<buffer        ; Address of 1st byte.
            sta     gbpbpb+1
            lda     #>buffer
            sta     gbpbpb+2
            lda     #&ff
            sta     gbpbpb+3
            sta     gbpbpb+4
            lda     filechan        ; Save channel num in OSPGPB pblock.
            sta     gbpbpb
            lda     gbpbcmd
            ldx     #<gbpbpb
            ldy     #>gbpbpb
            jsr     OSGBPB
            lda     #&02            ; Next call will write at current pointer.
            sta     gbpbcmd
            jmp     line_lp

.escape     lda     #&13            ; Restore our RAM bank status.
            sta     port_cmd
            lda     #&7c            ; Clear Escape without flushing anything.
            jsr     OSBYTE
            lda     #&00            ; Close the file.
            ldy     filechan
            jsr     OSFIND
            jsr     OSNEWL
            lda     #&00            ; Declare command implemented by this ROM.
            rts

.oswpb      equw    buffer          ; Buffer address for input.
            equb    &ff             ; Maximum line length.
            equb    &00             ; Minimum ASCII value.
            equb    &ff             ; Maximum ASCII value.
}

; *ROMS

.cmd_roms
{
            lda     #&aa
            ldx     #&00
            ldy     #&ff
            jsr     OSBYTE
            stx     romtab
            sty     romtab+1
            jsr     OSNEWL
            ldy     #&0f
.rmloop     sty     romid
            lda     #&08
            sta     port_cmd
            bcs     gotram
            lda     (romtab),y
            bne     gotrom
.next       dey
            bpl     rmloop
            jsr     OSNEWL
            lda     #&00
            rts
.gotrom     tax
            jsr     prinfo
            outspc
            jsr     cparen
            jsr     rdcpyr
            jsr     prtitl
            ldy     romid
            jmp     next
.gotram     jsr     rdcpyr
            sta     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #&00
            bne     empty
            inc     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #'('
            bne     empty
            inc     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #'C'
            bne     empty
            inc     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #')'
            bne     empty
            lda     #&06
            sta     &f6
            ldy     romid
            jsr     OSRDRM
            tax
            jsr     prinfo
            jsr     rparen
            jsr     prtitl
            ldy     romid
            jmp     next
.empty      ldx     #&00
            jsr     prinfo
            jsr     rparen
            jsr     OSNEWL
            ldy     romid
            jmp     next

.rdcpyr     lda     #&07
            sta     &f6
            lda     #&80
            sta     &f7
            ldy     romid
            jsr     OSRDRM
            sta     copywr
            rts

.prinfo     outchr  'R'
            outchr  'o'
            outchr  'm'
            outspc
            lda     romid
            cmp     #&0A
            bcs     geten
            outchr  '0'
            lda     romid
            jmp     both
.geten      outchr  '1'
            lda     romid
            sec
            sbc     #&0a
.both       and     #&0f
            clc
            adc     #'0'
            jsr     OSWRCH
            outspc
            outchr  ':'
            outspc
            outchr  '('
            txa
            and     #&80
            beq     notsrv
            lda     #'S'
            bne     issrv
.notsrv     lda     #' '
.issrv      jsr     OSWRCH
            txa
            and     #&40
            beq     space
            lda     #'L'
            bne     islng
.space      lda     #' '
.islng      jmp     OSWRCH

.prtitl     outspc
            lda     #&09
            sta     &f6
            lda     #&80
            sta     &f7
.tloop      ldy     romid
            jsr     OSRDRM
            cmp     #&7f
            bcs     cchar
            cmp     #' '
            bcs     pchar
            cmp     #&00
            bne     cchar
            lda     #' '
.pchar      jsr     OSWRCH
.cchar      inc     &f6
            lda     &f6
            cmp     copywr
            bne     tloop
            jmp     OSNEWL

.romchk     lda     copywr
            sta     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #&00
            bne     chkfai
            inc     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #'('
            bne     chkfai
            inc     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #'C'
            bne     chkfai
            inc     &f6
            ldy     romid
            jsr     OSRDRM
            cmp     #')'
            bne     chkfai
            clc
            rts
.chkfai     sec
            rts
.copyst     equs    ")C("
            equb    &00

.rparen     outchr  'R'
.cparen     lda     #')'
            jmp     OSWRCH
}

; Help text and suboutines to print parts of it.

.pr_rom_vers
{
            ldx     #&ff
            bne     start
.loop1      jsr     OSWRCH
.start      inx
            lda     romtitle,x
            bne     loop1
            lda     #' '
.loop2      jsr     OSWRCH
            inx
            lda     romtitle,x
            bne     loop2
            rts
}

.help_title jsr     OSNEWL
            jsr     pr_rom_vers
            jmp     OSNEWL

.help_short
{
            tya
            pha
            jsr     help_title
            ldx     #&ff
            bne     start
.loop       jsr     OSWRCH
.start      inx
            lda     helpkeys,x
            bne     loop
            lda     #&ea            ; check tube presence.
            ldx     #&00
            ldy     #&f
            jsr     OSBYTE
            txa
            beq     done            ; skip if no tube.
            jsr     OSNEWL
            ldx     #&00
            lda     tubemsg         ; print *HELP message.
.tubemlp    jsr     OSWRCH
            inx
            lda     tubemsg,X
            bne     tubemlp
.done       pla
            tay
            lda     #&09
            rts
.helpkeys   equs    "  VDFS",  &0d, &0a
            equs    "  SRAM",  &0d, &0a
            equs    "  UTILS", &0d, &0a, &00
}

.pr_text_xy
{
            lda     prtextws        ; save ZP workspace.
            pha
            lda     prtextws+1
            pha
            stx     prtextws        ; set up address in ZP
            sty     prtextws+1
            jsr     OSNEWL
            lda     #&87            ; find screen mode.
            jsr     OSBYTE
            cpy     #&00
            beq     wide0
            cpy     #&03
            beq     wide3
            ldy     #&00
.nloop      lda     (prtextws),y
            beq     done
            cmp     #&0a
            beq     nlnarrow
.nnotnl     jsr     OSASCI
.nnext      iny
            bne     nloop
            inc     &a9
            bne     nloop
.nlnarrow   jsr     OSNEWL          ; in narrow mode we treat an LF
            twospc                  ; character as a newline followed
            jmp     nnext           ; by two spaces.
.wide3      ldy     #&00
.wide0      lda     (prtextws),y
            beq     done
            cmp     #&0a
            bne     wnotnl          ; in wide mode we treat an LF
            lda     #' '            ; character as a space.
.wnotnl     jsr     OSASCI
            iny
            bne     wide0
            inc     prtextws+1
            bne     wide0
.done       pla
            sta     prtextws+1
            pla
            sta     prtextws
            rts
}

.help_txt_v equs    "Filing system selection:", &0d
            equs    "  DISK, DISC, ADFS, FADFS:", &0a
            equs    "Select VDFS if claimed", &0d
            equs    "  VDFS: Select VDFS", &0d
            equs    "  FSCLAIM on|off|+a|-a|+d|-d : ", &0a
            equs    "control claiming of DFS/ADFS", &0d
            equb    &0d
            equs    "VDFS commands:", &0d
            equs    "  BACK : return to previous directory", &0d
            equs    "  DELETE : delete file or empty dir", &0d
            equs    "  CDIR : create a new directory", &0d
            equs    "  DIR : change current directory", &0d
            equs    "  LIB : change current library", &0d
            equs    "  INFO : show info on single file", &0d
            equs    "  EX : show info on all files in CSD", &0d
            equs    "  ACCESS, BACKUP, COMPACT, COPY,", &0a
            equs    "DESTROY, DRIVE, ENABLE, FORM,", &0d
            equs    "  FREE, MAP, MOUNT, TITLE, VERIFY,", &0a
            equs    "WIPE : trapped and ignored", &0d
            equb    &00

.help_txt_u equs    "Utility commands:", &0d
            equs    "  QUIT or DESKTOP : terminate emulator", &0d
            equs    "  DUMP : dump a file in hex and ASCII", &0d
            equs    "  LIST : list a file with line numbers", &0d
            equs    "  PAGE : force PAGE location", &0d
            equs    "  PRINT : display a file verbatim", &0d
            equs    "  SHADOW : dummy command", &0d
            equs    "  TYPE : display a file on screen", &0d
            equb    &00

.help_txt_s equs    "Sideways RAM commands:", &0d
            equs    "  ROMS", &0d
            equs    "  SRLOAD <fsp> <address> (<r#>) (Q)", &0d
            equs    "  SRSAVE <fsp> <start> <end> (<r#>) (Q)", &0d
            equs    "  SRSAVE <fsp> <start> <+ln> (<r#>) (Q)", &0d
            equs    "  SRREAD <start> <end> <swadd> (<r#>)", &0d
            equs    "  SRREAD <start> <+len> <swadd> (<r#>)", &0d
            equs    "  SRWRITE <start> <end> <swadd> (<r#>)", &0d
            equs    "  SRWRITE <start> <+len> <swadd> (<r#>)", &0d
            equb    &00

.help_all
{
            tya
            pha
            jsr     help_title
            ldx     #<help_txt_v
            ldy     #>help_txt_v
            jsr     pr_text_xy
            ldx     #<help_txt_u
            ldy     #>help_txt_u
            jsr     pr_text_xy
            ldx     #<help_txt_s
            ldy     #>help_txt_s
            jsr     pr_text_xy
            pla
            tay
            lda     #&09
            rts
}

.help_vdfs
{
            tya
            pha
            ldx     #<help_txt_v
            ldy     #>help_txt_v
            jsr     pr_text_xy
            pla
            tay
            lda     #&09
            rts
}

.help_utils
{
            tya
            pha
            ldx     #<help_txt_u
            ldy     #>help_txt_u
            jsr     pr_text_xy
            pla
            tay
            lda     #&09
            rts
}

.help_sram
{
            tya
            pha
            ldx     #<help_txt_s
            ldy     #>help_txt_s
            jsr     pr_text_xy
            pla
            tay
            lda     #&09
            rts
}

.osw7f_stat
{
            lda     vtab,x
            tax
            lda     vals,x
.loop_val   jsr     OSWRCH
            inx
            lda     vals,x
            bne     loop_val
            jsr     OSNEWL
            lda     #&00
            rts
.vals       equs    "No OSWORD 7F corruption",&00
.all        equs    "All known DFSes",&00
.ac1        equs    "Acorn 0.90 and 1.20", &00
.ac2        equs    "Acorn 2.10", &00
.watf       equs    "Watford 1.10, 1.20, 1.30 and 1.4x",&00
.wat5       equs    "Watford 1.5x", &00
.vtab       equb    &00
            equb    all-vals
            equb    ac1-vals
            equb    ac2-vals
            equb    watf-vals
            equb    wat5-vals
}

; Start executation in the tube.  This will be called at the tail
; or a */, *RUN or when an unrecognised command is satified by a
; file on disk and the code concerned needs to execute over the tube.

.tube_exec  lda     #&D1            ; claim the tube.
            jsr     &0406
            bcc     tube_exec
            lda     #&04            ; start executation at the 32 bit
            ldx     #&c0            ; address in &c0 which is set by
            ldy     #&00            ; code in vdfs.c
            jmp     &0406

            include "tubehost.asm"

.tube_init
{
            lda     #<TubeEvHnd     ; point EVENTV to tube host
            sta     EVNTV
            lda     #>TubeEvHnd
            sta     EVNTV+1
            lda     #<TubeBrkHnd    ; point BRKV to tube host
            sta     BRKV
            lda     #>TubeBrkHnd
            sta     BRKV+1
            lda     #&8E
            sta     &FEE0
            ldy     #&00            ; copy the tube host code into low memory
.copy1      lda     TubeHost1,Y
            sta     &0400,Y
            lda     TubeHost2,Y
            sta     &0500,Y
            lda     TubeHost3,Y
            sta     &0600,Y
            dey
            bne     copy1
            jsr     TubeCall
            ldx     #TubeBrkLen
.copyz      lda     TubeHostZ,X
            sta     TubeBrkHnd,X
            dex
            bpl     copyz
            lda     #&00
            rts
}

.tube_explode
{
            cpy     #&00
            beq     notube          ; if no tube.
            lda     #&14            ; explode character set.
            ldx     #&06
            jsr     OSBYTE
.imsglp     bit     &FEE0           ; wait for character to be send from tube
            bpl     imsglp
            lda     &FEE1           ; fetch the character.
            beq     done            ; end of message?
            jsr     OSWRCH
            jmp     imsglp
.notube     lda     #&fe
.done       rts
}

.break_type
{
            lda     #&fd            ; get last break type.
            ldx     #&00
            ldy     #&ff
            jsr     OSBYTE
            stx     port_a
            lda     #&09
            sta     port_cmd
            rts
}

.cmd_files {
            ldx     #msg_files-banner
            jsr     prmsg
.fileloop   lda     cat_tmp         ; print the channel number.
            jsr     hexbyt
            outspc
            jsr     OSWRCH
            jsr     OSWRCH
            ldx     #&00            ; print characters of the name.
.charloop   lda     cat_tmp+&03,x
            jsr     OSWRCH
            inx
            cpx     #&0a
            bne     charloop
            outspc
            lda     #&20            ; test if open for writing.
            bit     cat_tmp+&0d
            beq     openin
            lda     #&10            ; test if open for reading.
            bit     cat_tmp+&0d
            BNE     openup
            lda     #'O'
            ldx     #'U'
            ldy     #'T'
            bne     mode
.openup     lda     #'U'
            ldx     #'P'
            bne     spcmode
.openin     lda     #'I'
            ldx     #'N'
.spcmode    ldy     #' '
.mode       jsr     OSWRCH
            txa
            jsr     OSWRCH
            tya
            jsr     OSWRCH
            twospc
            bit     cat_tmp+&0d
            bvs     isdir
            ldx     #&0e
            jsr     hexfour
            outspc
            ldx     #&12
            jsr     hexfour
.next       jsr     OSNEWL
            lda     #&0a
            sta     port_cmd
            bcc     fileloop
            lda     #&00
            rts
.isdir      jsr     prdir
            ldx     #&03
            lda     #' '
.dirlp      jsr     OSWRCH
            dex
            bpl     dirlp
            jsr     prdir
            jmp     next
.prdir      outchr  '<'
            outchr  'D'
            outchr  'I'
            outchr  'R'
            lda     #'>'
            jmp     OSWRCH
}

.none_open  ldx     #msg_nopen-banner
            jsr     prmsg
            lda     #&00
            rts

.osw_tail   jsr     OSWORD
            lda     #&00
            ldx     &f4
            rts

.close_all  txa                     ; Only need to save X as we know
            pha                     ; both A and Y are zero.
            lda     #&77            ; Close EXEC and SPOOL files.
            jsr     OSBYTE
            pla
            tax
            lda     #&00
            tay
            sta     port_a
            lda     #&0b
            sta     port_cmd
            rts

.prt_dbase
{
            ldx     #&00
            lda     text,x
.loop1      jsr     OSWRCH
            inx
            lda     text,x
            bne     loop1
            lda     cat_tmp
            jsr     OSWRCH
            lda     cat_tmp+1
.loop2      jsr     OSWRCH
            inx
            lda     text,x
            bne     loop2
            lda     cat_tmp+2
            jsr     OSWRCH
            lda     cat_tmp+3
            jsr     OSWRCH
            jsr     OSNEWL
            lda     #&00
            rts
.text       equs    "MMB Base: ",&00,&0d,&0a
            equs    "MMB Size: ",&00
}

.stack_print
{
            bit     &ff
            bmi     escape
.loop       lda     cat_tmp,x
            jsr     OSWRCH
            dex
            bpl     loop
            sty     port_cmd
            rts
}

.escape
{
            lda     #&7e
            jsr     OSBYTE
            ldx     #&09
.esclp      lda     escmsg,x
            sta     stack,x
            dex
            bpl     esclp
            jmp     stack
.escmsg     brk
            equb    &11
            equs    "Escape"
            equb    &00
}

.mmb_dabout
            jsr     pr_rom_vers
            ldx     #msg_mmb-banner
            jsr     prmsg
            jmp     newlret

.end
.gbpbpb     equb    &00
            equd    &00000000
            equd    &00000000
            equd    &00000000
.buffer     equb    &00

            save    "vdfs6", start, end
