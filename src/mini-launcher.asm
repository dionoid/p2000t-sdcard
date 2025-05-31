SECTION code_user

PUBLIC _call_addr
; PUBLIC _store_screen
; PUBLIC _restore_screen

_call_addr:
    jp (hl)

; _store_screen:
;     ld a,24         ; number of lines to copy
;     ld hl,0x5000    ; start of primary screen memory
;     ld de,0x5000+40 ; start of secondary screen memory
; loop0:    
;     ld bc, 40       ; number of columns per line
;     ldir            ; copy one line from primary to secondary screen
;     dec a           ; decrement line counter
;     ret z           ; if done, exit loop
;     ld bc, 80       ; move to next line
;     add hl,bc       ; increment screen pointer
;     ex de,hl        ; and swap pointers
;     jr loop0        ; move to next line

; _restore_screen:
;     ld a,24         ; number of lines to copy
;     ld hl,0x5000+40 ; start of secondary screen memory
;     ld de,0x5000    ; start of primary screen memory
; loop1:    
;     ld bc, 40       ; number of columns per line
;     ldir            ; copy one line from primary to secondary screen
;     dec a           ; decrement line counter
;     ret z           ; if done, exit loop
;     ld bc, 80       ; move to next line
;     ex de,hl        ; and swap pointers
;     add hl,bc       ; increment screen pointer
;     jr loop1        ; move to next line
