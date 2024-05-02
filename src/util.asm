;-------------------------------------------------------------------------------
;                                                                       
;   Author: Ivo Filot <ivo@ivofilot.nl>                                 
;                                                                       
;   P2000T-SDCARD is free software:                                     
;   you can redistribute it and/or modify it under the terms of the     
;   GNU General Public License as published by the Free Software        
;   Foundation, either version 3 of the License, or (at your option)    
;   any later version.                                                  
;                                                                       
;   P2000T-SDCARD is distributed in the hope that it will be useful,    
;   but WITHOUT ANY WARRANTY; without even the implied warranty         
;   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.             
;   See the GNU General Public License for more details.                
;                                                                       
;   You should have received a copy of the GNU General Public License   
;   along with this program.  If not, see http://www.gnu.org/licenses/. 
;                                                                       
;-------------------------------------------------------------------------------

SECTION code_user

PUBLIC _replace_bytes
PUBLIC _read_uint16_t
PUBLIC _read_uint32_t

;-------------------------------------------------------------------------------
; void replace_bytes(uint8_t* str, uint8_t org, uint8_t rep, uint16_t nrbytes) __z88dk_callee;
;-------------------------------------------------------------------------------
_replace_bytes:
    pop iy                  ; retrieve return address
    pop hl                  ; ramptr
    pop de                  ; e = org, d = rep
    pop bc                  ; number of bytes
    push iy                 ; put return address back on stack
nextbyte:
    ld a,(hl)
    cp e
    jr nz,skip
    ld a,d
    ld (hl),a
skip:
    inc hl                  ; next character
    dec bc                  ; decrement counter
    ld a,b
    or c
    jr nz,nextbyte
    ret

;-------------------------------------------------------------------------------
; uint16_t read_uint16_t(const uint8_t* data)
;-------------------------------------------------------------------------------
_read_uint16_t:
    pop iy                  ; return address
    pop de                  ; retrieve memory pointer
    ld a,(de)               ; lower byte
    ld l,a
    inc de
    ld a,(de)               ; upper byte
    ld h,a
    push iy
    ret                     ; result in HL

;-------------------------------------------------------------------------------
; uint32_t read_uint32_t(const uint8_t* data)
;-------------------------------------------------------------------------------
_read_uint32_t:
    pop iy                  ; return address
    pop bc                  ; retrieve memory pointer
    ld a,(bc)               ; byte 0
    ld l,a
    inc bc
    ld a,(bc)               ; byte 1
    ld h,a
    inc bc
    ld a,(bc)               ; byte 2
    ld e,a
    inc bc
    ld a,(bc)               ; byte 3
    ld d,a
    push iy
    ret                     ; result in DEHL