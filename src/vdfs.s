; > VDFS04/S
; Source for Virtual DFS ROM for WSS 6502Em emulator
;
; v0.01 MRB: Original
; v0.02 SPROW: Sprow update
; v0.03 JGH: Major tidy-up
; v0.04 JGH: Rearranged into separate ROM and Filing System code
;            Added FSClaim code, fuller help, better *CAT plus *EX and *INFO
;            Added Osword7F veneer
; v0.05 SJF: Changes to work with VDFS module in B-EM.  Enable to run
;            in ROM.
:

vdfsno  =   &11
oswpb   =   &70

ClaimFS     =   &FC5C       :\ *FSCLAIM ON|OFF flag
FSFlag      =   &FC5D       :\ FS id when claimed
PORT_CMD    =   &FC5E       :\ execute cmds on VDFS in host
PORT_A      =   &FC5F       :\ store A ready for command.

OS_CLI=&FFF7:OSBYTE=&FFF4:OSWORD=&FFF1:OSWRCH=&FFEE
OSASCI=&FFE3:OSGBPB=&FFD1:OSNEWL=&FFE7:OSFILE=&FFDD:OSARGS=&FFDA

:
ORG     &8000
.start
EQUS "MRB"                         :\ No language entry
JMP Service                        :\ Service entry
EQUB &82:EQUB ROMCopyright-&8000
.ROMVersion
EQUB &04
.ROMTitle
EQUS "Virtual DFS":EQUB 0:EQUS "0.05 (17 Sep 2016)"
.ROMCopyright
EQUB 0:EQUS "(C)1995 MRB, 2004 JGH, 2016 SJF":EQUB 0
EQUD 0
:
\ As this ROM requires support from the emulator and that may not
\ be enabled (or supported) check that it works before responding
\ to any ROM calls.

.Service
PHP:PHA
LDA #&FE:STA PORT_CMD:CMP #&00:BEQ ServEnabled
PLA:PLP:RTS

.ServEnabled
TXA:PHA:TYA:PHA            :\ Save all registers
TSX:LDA &0103,X                    :\ Get service number
CMP #&01:BNE P%+5:JMP ServWorkspace:\ Private workspace
CMP #&03:BNE P%+5:JMP ServFSStart  :\ FS startup
CMP #&04:BNE P%+5:JMP ServCommand  :\ *command
CMP #&08:BNE P%+5:JMP ServOsword   :\ OSWORD
CMP #&09:BNE P%+5:JMP ServHelp     :\ *DELETEHIMEM
CMP #&0F:BNE P%+5:JMP ServVectors  :\ Vectors have changed
CMP #&10:BNE P%+5:JMP ServShut     :\ Shut Spool/Exec
CMP #&12:BNE P%+5:JMP ServFSSelect :\ Select a filing system
CMP #&25:BNE P%+5:JMP ServFSInfo   :\ Request FS info
CMP #&26:BNE P%+5:JMP ServShut     :\ Shut all channels
CMP #&27:BNE P%+5:JMP ServReset    :\ Reset occured
.ServExit
PLA:TAY:PLA:TAX:PLA:PLP            :\ Restore all registers
RTS
.ServClaim
PLA:PLA:PLA:PLA                    :\ Lose stacked registers
LDA #0:RTS                         :\ Exit with A=0 to claim call
:
\ ===========================
\ ROM Administration Routines
\ ===========================
.ServWorkspace
PLA:CLC:ADC PageOffset:PHA         :\ Add offset to push PAGE up by
JMP ServExit
.PrROMTitleNL               :\ Print NL, ROM title
JSR OSNEWL
.PrROMTitle                 :\ Print ROM title
LDX #0
.PrRTLp
LDA ROMTitle,X:BEQ PrRTDone
JSR OSWRCH:INX:BNE PrRTLp
.PrRTDone
RTS
.PrROMVersion
JSR PrSpace:INX
.PrRVLp
LDA ROMTitle,X:CMP #'!':BCC PrRVDone
JSR OSWRCH:INX:BNE PrRVLp
.PrRVDone
JMP OSNEWL
\ ------------
\ ROM Commands
\ ------------
.ServCommand
LDX #&00
.CmdLookNext
PLA:PHA:TAY                 :\ Get text offset back
.CmdLookLp
LDA (&F2),Y:CMP #&2E:BEQ CmdMatchDot
AND #&DF:CMP ROMCommands,X:BNE CmdNoMatch
INX:INY:LDA ROMCommands,X
CMP #&0D:BNE CmdLookLp      :\ Not a full command
LDA (&F2),Y                 :\ Get following character
CMP #'A':BCS CmdCheckNext:\ Ensure command ends with non-alpha
DEY                         :\ Prepare to skip spaces
.CmdFoundSpc
INY:LDA (&F2),Y             :\ Move past any spaces
CMP #' ':BEQ CmdFoundSpc
LDA ROMCommands+1,X:STA &B0 :\ Get command address
LDA ROMCommands+2,X:STA &B1
JSR JumpB0:TAY:BNE CmdQuit  :\ Call command routine
JMP ServClaim               :\ Exit with service claimed
.CmdMatchDot
INX:LDA ROMCommands,X
CMP #&0D:BNE CmdMatchDot    :\ Find end of table entry
BEQ CmdFoundSpc             :\ Jump to enter command
.CmdNoMatch
LDA ROMCommands,X:INX
CMP #&0D:BNE CmdNoMatch     :\ Find end of table entry
.CmdCheckNext
INX:INX:LDA ROMCommands,X
BNE CmdLookNext             :\ Loop until terminator found
.CmdQuit
PLA:TAY:PLA:TAX:PLA:PLP     :\ Exit unclaimed
RTS
.JumpB0
JMP (&B0)
\ -----------------
\ ROM Command Table
\ -----------------
.ROMCommands
\ Filing system selection
EQUS "DISC"   :EQUB 13:EQUW disc
EQUS "DISK"   :EQUB 13:EQUW disk
EQUS "ADFS"   :EQUB 13:EQUW adfs
EQUS "FADFS"  :EQUB 13:EQUW fadfs
EQUS "VDFS"   :EQUB 13:EQUW vdfs
EQUS "FSCLAIM":EQUB 13:EQUW fsclaim
\ Utility commands
;EQUS "OSW"+CHR$(ASC"7"AND&DF)+"F":EQUB 13:EQUW fdc
EQUS "OSW7F"  :EQUB 13:EQUW fdc
EQUS "PAGE"   :EQUB 13:EQUW page
EQUS "QUIT"   :EQUB 13:EQUW quit
EQUS "DESKTOP":EQUB 13:EQUW quit
EQUS "PAGE"   :EQUB 13:EQUW page
EQUS "SHADOW" :EQUB 13:EQUW shadow
EQUS "INSERT" :EQUB 13:EQUW insert
\ SRAM commands
EQUS "SRLOAD" :EQUB 13:EQUW srload
EQUS "SRSAVE" :EQUB 13:EQUW srsave
EQUS "SRREAD" :EQUB 13:EQUW srread
EQUS "SRWRITE":EQUB 13:EQUW srwrite
EQUB 0
\ ----------------------------------
\ Detailed help routine
\ Based on Sprow's code in VDFS 0.02
\ ----------------------------------
.ServHelp
LDA (&F2),Y
CMP #&0D:BEQ HelpTitle      :\ No subject, just print ROM title
CMP #&2E:BEQ L8514          :\ '.', print full info
AND #&DF:CMP #&56:BNE L8538 :\ Doesn't start with 'V', check for "DFS"
INY:JMP L8538               :\ Move past 'V', check for "DFS"
.L8514
JSR PrROMTitleNL            :\ Print ROM title
JSR PrROMVersion
LDX #ROMCommandHelp AND 255
LDY #ROMCommandHelp DIV 256 :\ Point to help text
JSR PrintText:JMP ServExit
.HelpTitle
JSR PrROMTitleNL            :\ Just print ROM title
JSR PrROMVersion
JMP ServExit
.L8538
DEY:LDX #&FF
.L853B
INX:INY                     :\ Point to next characters
LDA (&F2),Y:AND #&DF
CMP L854C,X:BEQ L853B       :\ If matches "DFS", loop to next char
LDA L854C,X:BEQ L8514       :\ If at end, jump to print full info
JMP ServExit
.PrintText
STX &E8:STY &E9             :\ Store text pointer
LDY #&00:BEQ L8525          :\ Jump into loop
.L8522
CMP #10:BNE HelpNot10
TYA:PHA:LDA #135:JSR OSBYTE :\ Find current screen mode
CPY #0:BEQ Help80
CPY #3:BNE HelpNot80
.Help80
PLA:TAY:LDA #' ':BNE HelpNot10
.HelpNot80
PLA:TAY:JSR OSNEWL
JSR PrSpace
.HelpNot10
JSR OSASCI
.L8525
LDA (&E8),Y:BEQ L8530       :\ If &00 terminator, end
INY:BNE L8522               :\ Loop to print character
INC &E9:BMI L8522           :\ Increment high byte and loop back
.L8530
RTS
.PrSpace
LDA #' ':JMP OSWRCH
.PrHex
PHA:LSR A:LSR A:LSR A:LSR A
JSR PrNyb:PLA:.PrNyb
AND #15:CMP #10:BCC PrDigit
ADC #6:.PrDigit:ADC #48
JMP OSWRCH
.L854C
EQUS "DFS":EQUB 0           :\ *DELETEHIMEM match string
.ROMCommandHelp
EQUS "Filing system selection:":EQUB 13
EQUS "  DISK, DISC, ADFS, FADFS :":EQUB 10
EQUS "Select VDFS if claimed":EQUB 13
EQUS "  VDFS : Select VDFS":EQUB 13
EQUS "  FSCLAIM ON : Claim DISC, ADFS":EQUB 13
EQUS "  FSCLAIM OFF : Release DISC, ADFS":EQUB 13
EQUS "  OSW7F (NONE)(<ver>) :":EQUB 10
EQUS "Emulate Osword &7F memory corruption":EQUB 13
EQUB 13
EQUS "Utility commands:":EQUB 13
EQUS "  QUIT or DESKTOP : return to RISC OS":EQUB 13
EQUS "  PAGE : force PAGE location":EQUB 13
EQUS "  SHADOW : dummy command":EQUB 13
EQUB 13
EQUS "Sideays RAM commands:":EQUB 13
EQUS "  SRLOAD <fsp> <address> (<r#>) (Q)":EQUB 13
EQUS "  SRSAVE <fsp> <start> <end> (<r#>) (Q)":EQUB 13
EQUS "  SRSAVE <fsp> <start> <+ln> (<r#>) (Q)":EQUB 13
EQUS "  SRREAD <start> <end> <swadd> (<r#>)":EQUB 13
EQUS "  SRREAD <start> <+len> <swadd> (<r#>)":EQUB 13
EQUS "  SRWRITE <start> <end> <swadd> (<r#>)":EQUB 13
EQUS "  SRWRITE <start> <+len> <swadd> (<r#>)":EQUB 13
EQUB 13
EQUS "VDFS commands:":EQUB 13
EQUS "  BACK : return to previous directory":EQUB 13
EQUS "  DIR : change current directory":EQUB 13
EQUS "  LIB : change current library":EQUB 13
EQUS "  INFO : show info on single file":EQUB 13
EQUS "  EX : show info on all files in CSD":EQUB 13
EQUS "  ACCESS, BACKUP, COMPACT, COPY,":EQUB 10
EQUS "DESTROY, DRIVE, ENABLE, FORM, FREE,":EQUB 13
EQUS "  MAP, MOUNT, RENAME, TITLE, VERIFY, ":EQUB 10
EQUS "WIPE : trapped and ignored":EQUB 13
EQUB 0
\ --------------------
\ ROM Command routines
\ --------------------
.quit
LDA #&00                    :\ A=0 - claimed
LDA #&FF:STA PORT_CMD       :\ Return to host
:
.page:.shadow:.insert
LDA #&00                    :\ Ignore command
RTS
:

.srload LDA #&D0
        BNE srfile
.srsave LDA #&D3
.srfile STA PORT_CMD        :\ parse command on host.
        LDA #&00            :\ query current FS
        TAY
        JSR OSARGS
        LDX #<oswpb         ;\ YX = parameter block.
        LDY #>oswpb
        CMP FSFlag
        BEQ srfus           :\ FS is us so execute on host.
        LDA #&43            :\ execute via OSWORD
        JMP OSWORD
.srfus  LDA #&D2            :\ .
        STA PORT_CMD
        RTS

.srread: LDA #&D4:STA PORT_CMD:RTS \ Pass to host and return
.srwrite:LDA #&D1:STA PORT_CMD:RTS \ Pass to host and return
:
:
.PageOffset:EQUB &00        :\ Amount to raise PAGE by
\ ======================
\ Filing System Routines
\ ======================
\ --------------------------------
\ Filing system selection commands
\ --------------------------------
.disc:.disk:.adfs
.fadfs:.vdfs                :\ On entry, X=4,11,18,26,33
TXA:LSR A:LSR A:LSR A:TAX   :\ X=0,1,2,3,4
LDA FSValues,X:TAY          :\ Get filing system number
CPY #vdfsno:BEQ SelectFS    :\ Always select VDFS
BIT ClaimFS:BPL SelectFSExit:\ If not claimed, exit with A<>0
.SelectFS
LDX #&12:LDA #&8F:JSR OSBYTE:\ Select filing system
LDA #&00                    :\ A=0 - claimed
.SelectFSExit
RTS
.FSValues
EQUB 4:EQUB 4:EQUB 8        :\ DISC, DISK, ADFS
EQUB 8:EQUB vdfsno          :\ FADFS, VDFS
:
.fsclaim
LDA (&F2),Y:AND #&DF
CMP #'O':BNE fsclaimStatus
INY:LDA (&F2),Y:AND #&DF
CMP #'N':LDA #0:SBC #0
EOR #&FF:STA ClaimFS
LDA #0:RTS
.fsclaimStatus
LDA #'O':JSR OSWRCH
LDA ClaimFS:AND #8:PHP:ORA #'F'
JSR OSWRCH:PLP:BNE P%+5:JSR OSWRCH
JSR OSNEWL:LDA #0:RTS
\ --------------------------
\ Filing System Service Code
\ --------------------------
.ServVectors
LDA #0:\STA FSFlag          :\ Clear FSflag, and exit
.ServShut                   :\ Doesn't do anything
JMP ServExit
.ServFSSelect
CPY #vdfsno:BEQ FSSelect    :\ VDFS
BIT ClaimFS:BPL FSSelectNone:\ If not claimed, don't check for DFS or ADFS
CPY #&04:BEQ FSSelect       :\ Select DFS
CPY #&08:BEQ FSSelect       :\ Select ADFS
.FSSelectNone
JMP ServExit                :\ Exit unclaimed
.FSSelect
TYA:PHA                     :\ Save FS id
LDA #&06:JSR CallFSCV       :\ Inform current FS new FS taking over
LDX #0:LDY #&1B             :\ Set up new vectors
.ClaimVecLp
TYA:STA &212,X
LDA #&FF:STA &213,X         :\ vector=&FFxx
LDA VectorTable+0,X:STA &D9F+0,Y
LDA VectorTable+1,X:STA &D9F+1,Y
LDA &F4:STA &D9F+2,Y        :\ exvec=&RRxxyy
INY:INY:INY:INX:INX
CPX #&0E:BNE ClaimVecLp
LDA #&8F:LDX #&0F:JSR OSBYTE:\ Notify that vectors have changed
PLA:STA FSFlag              :\ Store FS id
JMP ServClaim
.CallFSCV
JMP (&021E)
.VectorTable
EQUW File:EQUW Args:EQUW BGet:EQUW BPut
EQUW GBPB:EQUW Find:EQUW FSC
\ ---------------------------------
\ Select FS on BREAK and maybe boot
\ ---------------------------------
.ServFSStart
TYA:PHA
LDA #&7A:JSR OSBYTE         :\ Check what keys are pressed
CPX #&FF:BEQ FSStartGo      :\ No keys pressed, select me
CPX #&51:BEQ FSStartGo      :\ 'S' pressed, select me
PLA:JMP ServExit            :\ Exit
.FSStartGo
JSR PrROMTitle              :\ Print filing system name
JSR OSNEWL:JSR OSNEWL
LDY FSFlag:JSR SelectFS     :\ Select filing system
PLA:BNE P%+5:JSR HostBoot   :\ If booting, get !Boot from host
JMP ServClaim               :\ Exit, claimed
.HostBoot
EQUB &03:EQUB &D3           :\ Do !Boot
.ServFSInfo
LDX #&00
.FSInfoLp
LDA L81FB,X:STA (&F2),Y
INY:INX:CPX #44:BNE FSInfoLp:\ Copy filing system name
PLA:TYA:PHA:JMP ServExit    :\ Adjust stacked Y and exit
.L81FB
EQUS "DISK    ":EQUB &11:EQUB &15:EQUB &04
EQUS "DISC    ":EQUB &11:EQUB &15:EQUB &04
EQUS "ADFS    ":EQUB &30:EQUB &3A:EQUB &08
EQUS "VDFS    ":EQUB &80:EQUB &FF:EQUB &11
:
.ServReset
JMP ServExit                :\ NOP out for the moment
:
\ -------------------------------------------
\ Pass filing system calls to host and return
\ -------------------------------------------
.File STA PORT_A:LDA #&06:STA PORT_CMD:RTS
.Args STA PORT_A:LDA #&05:STA PORT_CMD:RTS
.BGet STA PORT_A:LDA #&04:STA PORT_CMD:RTS
.BPut STA PORT_A:LDA #&03:STA PORT_CMD:RTS
.GBPB STA PORT_A:LDA #&02:STA PORT_CMD:RTS
.Find STA PORT_A:LDA #&01:STA PORT_CMD:RTS
:
\ -------------------
\ File System Control
\ -------------------
.FSC
CMP #&08:BEQ FSCDone        :\ Jump if *command warning
CMP #&0A:BNE P%+5:JMP Info  :\ Info on a file
CMP #&09:BNE P%+5:JMP Ex    :\ Examine directory
CMP #&05:BNE P%+5:JMP Cat   :\ Catalogue directory
CMP #&03:BNE P%+5:JMP FSCommandLookup:\ Filing system commands
CMP #&06:BNE FSCemul:LDA #&77:JSR OSBYTE:LDA#&06
.FSCemul
STA PORT_A:LDA #&00:STA PORT_CMD :\ Pass to emulator and return
.FSCDone
RTS
:
\ ----------------------
\ Filing System Commands
\ ----------------------
.FSCommandLookup
PHA:TXA:PHA:TYA:PHA
STX &F2:STY &F3:LDX #&00
.L833C
LDY #&00
.L833E
LDA (&F2),Y:CMP #&2E:BEQ L8364
AND #&DF:CMP FSCommands,X:BNE L8370
INX:INY:LDA FSCommands,X
CMP #&0D:BNE L833E
LDA (&F2),Y
CMP #'A':BCS FSCmdCheckNext
DEY
.L8354
INY:LDA (&F2),Y
CMP #' ':BEQ L8354
LDA FSCommands+1,X:STA &B0
LDA FSCommands+2,X:STA &B1
PLA:PLA:PLA                 :\ Lose stacked registers
JMP (&B0)
.L8364
INX:LDA FSCommands,X
CMP #&0D:BNE L8364
BEQ L8354
.L8370
LDA FSCommands,X:INX
CMP #&0D:BNE L8370
.FSCmdCheckNext
INX:INX:LDA FSCommands,X
BNE L833C                   :\ Loop until terminator found
.L8384                      :\ No match, pass to host to try
PLA:TAY:PLA:TAX:PLA         :\ Restore registers
STA PORT_A:LDA #&00:STA PORT_CMD :\ Pass FSC to host to try
RTS                         :\ And return
\ ---------------------------
\ Filing System Command Table
\ ---------------------------
.FSCommands
EQUS "ACCESS" :EQUB 13:EQUW access
EQUS "BACK"   :EQUB 13:EQUW back
EQUS "BACKUP" :EQUB 13:EQUW backup
EQUS "COMPACT":EQUB 13:EQUW compact
EQUS "COPY"   :EQUB 13:EQUW copy
EQUS "DELETE" :EQUB 13:EQUW delete
EQUS "DESTROY":EQUB 13:EQUW destroy
EQUS "DIR"    :EQUB 13:EQUW dir
EQUS "DRIVE"  :EQUB 13:EQUW drive
EQUS "ENABLE" :EQUB 13:EQUW enable
EQUS "EX"     :EQUB 13:EQUW ex
EQUS "FORM"   :EQUB 13:EQUW form
EQUS "FREE"   :EQUB 13:EQUW free
EQUS "INFO"   :EQUB 13:EQUW info
EQUS "LIB"    :EQUB 13:EQUW lib
EQUS "MAP"    :EQUB 13:EQUW free
EQUS "MOUNT"  :EQUB 13:EQUW mount
EQUS "RENAME" :EQUB 13:EQUW rename
EQUS "RESCAN" :EQUB 13:EQUW rescan
EQUS "TITLE"  :EQUB 13:EQUW title
EQUS "VERIFY" :EQUB 13:EQUW verify
EQUS "WIPE"   :EQUB 13:EQUW wipe
EQUB 0
:
\ ------------------------------
\ Filing System Command Routines
\ ------------------------------
.F2toXY
TYA:CLC:ADC &F2:TAX:LDA &F3:ADC #0:TAY:RTS
.access   :RTS
.back     :LDA #&D5:STA PORT_CMD:RTS :\ Pass to host and return
.backup   :RTS
.compact  :RTS
.copy     :RTS
.delete   :RTS
.destroy  :RTS
.dir      :LDA #&D7:STA PORT_CMD:RTS :\ Pass to host and return
.drive    :RTS
.enable   :RTS
.ex       :JSR F2toXY:LDA #&09:JMP CallFSCV
.form     :RTS
.free     :RTS
.info     :JSR F2toXY:LDA #&0A:JMP CallFSCV
.lib      :LDA #&D8:STA PORT_CMD:RTS :\ Pass to host and return
.mount    :RTS
.rename   :JSR F2toXY:LDA #&0C:JMP CallFSCV
.rescan   :LDA #&D9:STA PORT_CMD:RTS :\ Pass to host and return
.wipe     :RTS
.title    :RTS
.verify   :RTS
:
\ ---------------------------
\ Functions performed locally
\ ---------------------------
.Info
STX &B0:STY &B1:LDY #0
.InfoLp
LDA (&B0),Y:STA &0D20,Y:INY
CPY #&20:BCC InfoLp
LDA #&01:BNE CatExInfo
.Ex
LDA #&FF:BNE CatExInfo
.Cat
LDA #&80
.CatExInfo                  :\ b7=multiple files, b0=full info
PHA
\ &D01 = GBPB block
\ &D0E = FILE block
\ &D20 = pathname
LDA #&40 :STA &0D00         :\ Null RTI routine
LDA #&01 :STA &0D01         :\ Fetch one item
LDA #&20 :STA &0D02:STA &0D0E
LDA #&0D :STA &0D03:STA &0D0F:\ =>Filename
LDA #&FF :STA &0D06
LDA #&00 :STA &0D04
STA &0D05:STA &0D07
STA &0D08:STA &0D09:STA &0D0A
STA &0D0B:STA &0D0C:STA &0D0D
.CatLoop
PLA:PHA:BPL CatSingleFile
LDA #&09
LDX #&01:LDY #&0D:JSR OSGBPB:\ Read catalogue entry
LDA &0D01:BEQ CatDone       :\ If no more, exit
.CatSingleFile
LDX #&0E:LDY #&0D
LDA #&05:JSR OSFILE         :\ Read info on this entry
PHA:JSR CatPrint:PLA:TAY    :\ Print object name
PLA:PHA:ROR A:BCC CatNoInfo
TYA:PHA:LDY #3:LDX #5
.CatAddrLp1
TYA:PHA:LDY #4
.CatAddrLp2
LDA &0D0E,X:JSR PrHex
DEX:DEY:BNE CatAddrLp2
JSR PrSpace
TXA:CLC:ADC #8:TAX
PLA:TAY:DEY:BNE CatAddrLp1
PLA:TAY
.CatNoInfo
TYA:JSR CatAttrs
PLA:PHA:ROR A:BCC P%+5:JSR OSNEWL
PLA:PHA:BMI CatLoop:PLA     :\ Loop until no more returned
.CatExit
RTS
.CatDone
PLA:ROR A:BCS CatExit       :\ *EX, no newline needed
JMP OSNEWL
.CatPrint
LDX #&00
.CatPrtLp1
LDA &0D20,X:CMP #' ':BCC CatPrSpc
JSR OSWRCH:INX:BNE CatPrtLp1:\ Loop until control character
.CatPrSpc
LDA #' '
.CatPrLp2
JSR OSWRCH:INX
CPX #13:BCC CatPrLp2        :\ Print spaces until column 13
.CatCharNone
RTS
.CatAttrs
LDX #7:LDY #8
AND #2:BEQ P%+5:JSR CatCharD:\ 'D'
LDY #3:LDA #&08:JSR CatChar :\ 'L'
LDY #1:LDA #&02:JSR CatChar :\ 'W'
LDY #0:LDA #&01:JSR CatChar :\ 'R'
LDA #'/':JSR OSWRCH      :\ '\'
LDY #5:LDA #&20:JSR CatChar :\ 'w'
LDY #4:LDA #&10:JSR CatChar :\ 'r'
JMP CatPrSpc
.CatChar
BIT &0D1C:BEQ CatCharNone
.CatCharD
LDA CatAttrChars,Y:INX:JMP OSWRCH
.CatAttrChars
EQUS "RWELrwePD"
\ ================
\ OSWORD emulation
\ ================
.fdc
LDA (&F2),Y:CMP #13:BEQ fdcstatus
AND #&DF
CMP #'A':BEQ fdcAcorn
CMP #'W':BEQ fdcWatford
LDA #3:BNE fdcSet
.fdcWatford
INY
.fdcAcorn
INY:PHA:LDA (&F2),Y:AND #7:STA &B0
PLA:AND #16:LSR A:LSR A:ADC &B0
:\ A=0/1/2/3   = A090/A120/A210/ALL
:\ A=5/7/8/9   = W110/W130/W14x/W154
.fdcSet
ASL A:ASL A:ASL A
CLC:ADC #Osw7FTable AND 255:STA Osw7FAddr+0
LDA #0:ADC #Osw7FTable DIV 256:STA Osw7FAddr+1
LDA #0:RTS
.fdcstatus
JSR fdcFetchAddr:LDY #2
.fdcstLp1
LDA (&B0),Y:BEQ fdcstName
JSR OSASCI:INY:BNE fdcstLp1
.fdcstName
JSR fdcFetchPtr
LDA (&B0),Y:BEQ fdcstDone
LDA #':':JSR OSWRCH
JMP fdcSpace
.fdcstLp2
LDA (&B0),Y:BEQ fdcstDone
PHA:LDA #'&':JSR OSWRCH
LDA #&10:JSR PrHex
PLA:JSR PrHex:INY
.fdcSpace
JSR PrSpace:JSR PrSpace:JSR PrSpace
JMP fdcstLp2
.fdcstDone
JSR OSNEWL
.fdcstEnd
LDA #0:RTS
.fdcFetchAddr
LDA Osw7FAddr+0:STA &B0
LDA Osw7FAddr+1:STA &B1
RTS
.fdcFetchPtr
LDY #0:LDA (&B0),Y:PHA
INY:LDA (&B0),Y:STA &B1
PLA:STA &B0:DEY
RTS
.ServOsword
LDA &EF:CMP #127            :\ Check OSWORD number
BNE P%+5:JSR Osword7F       :\ If FM disk access, play with memory
PLA:TAY:PLA:TAX:PLA:PLP     :\ Restore registers
STA PORT_A:LDA #&40:STA PORT_CMD :\Pass OSWORD call to emulator and return
\ -------------------------------------------------------------
\ Corrupt bits of memory to simulate effects of real OSWORD &7F
\ -------------------------------------------------------------
.Osword7F
LDA &B0:PHA:LDA &B1:PHA     :\ Code calling Osword7F may be using &B0/1
JSR fdcFetchAddr
JSR fdcFetchPtr
.Osw7FLp
LDA (&B0),Y:BEQ Osw7FDone:TAX
EOR &1000,X:ROL A:EOR #&23:STA &1000,X
INY:BNE Osw7FLp
.Osw7FDone
PLA:STA &B1:PLA:STA &B0     :\ A,X,Y preserved outside here
RTS
:
.Osw7FAddr
EQUW Osw7FTableNone         :\ Pointer to address table
.Osw7FTable
EQUW Osw7FAcorn1:EQUS "A090":EQUB 0:EQUB 0
EQUW Osw7FAcorn1:EQUS "A120":EQUB 0:EQUB 1
EQUW Osw7FAcorn2:EQUS "A210":EQUB 0:EQUB 2
.Osw7FTableNone
EQUW Osw7FNone :EQUS "NONE":EQUB 0:EQUB 3
EQUW Osw7FAll  :EQUS "ALL ":EQUB 0:EQUB 4
EQUW Osw7FWatf :EQUS "W110":EQUB 0:EQUB 5
EQUW Osw7FWatf :EQUS "W120":EQUB 0:EQUB 6
EQUW Osw7FWatf :EQUS "W130":EQUB 0:EQUB 7
EQUW Osw7FWatf :EQUS "W14x":EQUB 0:EQUB 8
EQUW Osw7FWat5 :EQUS "W15x":EQUB 0:EQUB 9
.Osw7FNone
EQUB 0
.Osw7FAcorn1:\ Locations corrupted by Acorn DFS 0.90/1.20
EQUB &72:EQUB &73:EQUB &74:EQUB &75:EQUB &80:EQUB &82
EQUB &83:EQUB &85:EQUB &C8:EQUB &C9:EQUB &D3:EQUB &D5
EQUB &D6:EQUB 0
.Osw7FAcorn2:\ Locations corrupted by Acorn DFS 2.10/2.20
EQUB &87:EQUB &88:EQUB &89:EQUB &8B:EQUB &8C:EQUB &8D
EQUB &8E:EQUB &D3:EQUB &D6:EQUB &DE:EQUB &DF:EQUB &E0
EQUB &E1:EQUB 0
.Osw7FWatf :\ Locations corrupted by Watford 1.00-1.44
EQUB &42:EQUB &43:EQUB &4A:EQUB &78:EQUB &88:EQUB &89
EQUB &8A:EQUB 0
.Osw7FWat5 :\ Locations corrupted by Watford 1.54
EQUB &30:EQUB &36:EQUB &38:EQUB &3F:EQUB &42:EQUB &43
EQUB &4A:EQUB &78:EQUB &88:EQUB &89:EQUB &8A:EQUB &A0
EQUB &A1:EQUB &A2:EQUB &A3:EQUB &A4:EQUB &A5:EQUB &A6
EQUB &A7:EQUB &A8:EQUB &A9:EQUB &AA:EQUB 0
:
.Osw7FAll  :\ Composite of all locations
EQUB &30:EQUB &33:EQUB &36:EQUB &38:EQUB &3F:EQUB &42
EQUB &43:EQUB &44:EQUB &4A:EQUB &72:EQUB &73:EQUB &74
EQUB &75:EQUB &78:EQUB &79:EQUB &7A:EQUB &7B:EQUB &80
EQUB &82:EQUB &83:EQUB &85:EQUB &87:EQUB &88:EQUB &89
EQUB &8A:EQUB &8B:EQUB &8C:EQUB &8D:EQUB &8E:EQUB &A0
EQUB &A1:EQUB &A2:EQUB &A3:EQUB &A4:EQUB &A5:EQUB &A6
EQUB &A7:EQUB &A8:EQUB &A9:EQUB &AA:EQUB &C8:EQUB &C9
EQUB &D3:EQUB &D5:EQUB &D6:EQUB &DE:EQUB &DF:EQUB &E0
EQUB &E1::EQUB 0
.end
;
SAVE "VDFS5", start, end
; PRINT" *SAVE VDFS ";~mcode%;" ";~O%;" 0 FFFBBC00"
;
; This ROM originally trapped to the host by executing a instruction
; with opcode &03, officially undefined on a real 6502, the emulator
; would use this to hand control to the host VFS component.  Modern
; emulators emulate the behaviour of undocument instructions so this
; version instead uses a port at the end of the hard disk I/O space.
; The command port is FC5E and accepts the same "opcodes" as the
; original implementation as detailed below, i.e. the action is
; initiated by writing the op-code to this port.  Unlike the original
; implementation the emulator does not do an implied RTS so needs to
; provided within this ROM.  If the value of A is signifcant it can be
; stored to port FC5F before storing the opcode to port FC5E.
; &00 FSC
; &01 OSFIND
; &02 OSGBPB
; &03 OSBPUT
; &04 OSBGET
; &05 OSARGS
; &06 OSFILE
:
; &40 OSWORD
; &41 Does nothing
:
; &80 Read CMOS low level location X, return in Y and A
; &81 Write Y to CMOS low level location X
; &82 Read EPROM low level location X, return in Y and A
; &83 Write Y to EPROM low level location X
:
; &D0 *SRLOAD
; &D1 *SRWRITE
; &D2 *DRIVE
; &D3 Load/Run/Exec !BOOT
; &D4 Does nothing
; &D5 *BACK
; &D6 *MOUNT
; &D7 *DIR
; &D8 *LIB
:
; &FF Quit
;
; END
