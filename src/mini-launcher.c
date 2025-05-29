/**************************************************************************
 *                                                                        *
 *   Author: Ivo Filot <ivo@ivofilot.nl>                                  *
 *                                                                        *
 *   P2000T-SDCARD is free software:                                      *
 *   you can redistribute it and/or modify it under the terms of the      *
 *   GNU General Public License as published by the Free Software         *
 *   Foundation, either version 3 of the License, or (at your option)     *
 *   any later version.                                                   *
 *                                                                        *
 *   P2000T-SDCARD is distributed in the hope that it will be useful,     *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty          *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.              *
 *   See the GNU General Public License for more details.                 *
 *                                                                        *
 *   You should have received a copy of the GNU General Public License    *
 *   along with this program.  If not, see http://www.gnu.org/licenses/.  *
 *                                                                        *
 **************************************************************************/

#include <string.h>
#include <stdint.h>
#include <z80.h>

#include "constants.h"
#include "config.h"
#include "ports.h"
#include "memory.h"
#include "ram.h"
#include "fat32.h"

// function prototypes
void init(void);
void clearscreen(void);
void clear_status_line(void);
void call_addr(uint16_t addr) __z88dk_fastcall;

// dummy terminal functions
void print_error(char* str) { (void)str; }
void print(char* str) { (void)str; }
void print_recall(char* str) { (void)str;}

uint16_t highlight_id = 1;

void highlight_refresh(void) {
    // update highlight
    for (uint8_t i = 1; i <= 16; i++) {
        memcpy(vidmem + 0x50*(i+2) + 2, (i == highlight_id) ? "\x07\x3E" : "\x03 ", 2);
    }
}

void main(void) {
    // initialize SD card
    init();

    // setup screen
    clearscreen();

    // build linked list for the root directory
    build_linked_list(_current_folder_cluster);
    read_folder(-1, 0);
    highlight_refresh();

    // put in infinite loop and wait for program selection
    for(;;) {
        // wait for key-press
        if(keymem[0x0C] > 0) {
            uint8_t key0 = keymem[0];
            memset(keymem, 0x00, 0x0D);
            // key down
            if(key0 == 21)  {
                if (vidmem[0x50*(highlight_id+2+1) + 4] != 0x00)
                    highlight_id++;
                else
                    highlight_id = 1; // wrap around
                highlight_refresh();
            }
            // key up
            if(key0 == 2)  {
                if (highlight_id > 1)
                    highlight_id--;
                else {
                    for (int8_t i = 16; i >= 0; i--) {
                        if (vidmem[0x50*(i+2) + 4] != 0x00) {
                            highlight_id = i;
                            break;
                        }
                    }
                }
                highlight_refresh();
            }
            // space or enter key
            if(key0 == 17 || key0 == 52)  { // space or enter
                uint32_t clus = read_folder(highlight_id, 0);
                if(clus != _root_dir_first_cluster) {
                    if(_current_attrib & 0x10) {
                        if(clus == 0) { // if zero, this is the root directory
                            _current_folder_cluster = _root_dir_first_cluster;
                        } else {
                            _current_folder_cluster = clus;
                        }
                        build_linked_list(_current_folder_cluster);
                        clearscreen();
                        read_folder(-1, 0);
                        highlight_id = 1; // highlight first item in newly loaded folder
                        highlight_refresh();
                    }
                    else {
                        // load cas data to external RAM
                        uint32_t cluster = read_folder(highlight_id, 0);
                        build_linked_list(cluster);

                        if (memcmp(_ext, "CAS", 3) == 0) {
                            // set RAM bank to CASSETTE
                            set_ram_bank(RAM_BANK_CASSETTE);
                            clear_status_line();
                            strcpy(vidmem + 0x50*21, "\x03\x08Loading file...");
                            store_cas_ram(_linkedlist[0], 0x0000);
                            set_ram_bank(0);
                            break; // break out of the loop to run the program
                        } else {
                            // load PRG into internal RAM
                            if(memory[0x605C] < 2) {
                                vidmem[0x50*(highlight_id + 2) + 2] = 0x01; // color line red
                                goto restore_state;
                            }

                            store_prg_intram(_linkedlist[0], PROGRAM_LOCATION);

                            // verify that the signature is correct
                            if(memory[PROGRAM_LOCATION] != 0x50) {
                                vidmem[0x50*(highlight_id + 2) + 2] = 0x01; // color line red
                                goto restore_state;
                            }

                            // transfer copy of current screen to external RAM
                            copy_to_ram(vidmem, VIDMEM_CACHE, 0x1000);

                            // launch the program
                            call_addr(PROGRAM_LOCATION + 0x10);

                            // retrieve copy of current screen
                            copy_from_ram(VIDMEM_CACHE, vidmem, 0x1000);
                            memset(keymem, 0x00, 0x0D);
restore_state:
                            build_linked_list(_current_folder_cluster);
                        }
                    }
                }
            }
        }
    }
}

static const uint8_t bottom_bar[] = {
    0x13, 0x00, 0x60, 0x76, 0x70, 0x20, 0x20, 0x20, 0x20, 0x60, 0x6E, 0x64, 0x00, 0x00, 0x3C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x6C, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x79, 0x30, 0x00,
    0x13, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x00, 0x00, 0x35, 0x53, 0x50, 0x41, 0x54, 0x49, 0x45, 0x00, 0x42, 0x41, 0x4C, 0x4B, 0x6A, 0x00, 0x00, 0x29, 0x3D, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00,
    0x06, 0x50, 0x61, 0x67, 0x69, 0x6E, 0x61, 0x20, 0x53, 0x63, 0x72, 0x6F, 0x6C, 0x6C, 0x20, 0x20, 0x53, 0x74, 0x61, 0x72, 0x74, 0x20, 0x41, 0x70, 0x70, 0x00, 0x20, 0x53, 0x63, 0x72, 0x6F, 0x6C, 0x6C, 0x20, 0x50, 0x61, 0x67, 0x69, 0x6E, 0x61,
    0x06, 0x54, 0x65, 0x72, 0x75, 0x67, 0x00, 0x20, 0x4F, 0x6D, 0x68, 0x6F, 0x6F, 0x67, 0x00, 0x20, 0x4F, 0x70, 0x65, 0x6E, 0x20, 0x20, 0x4D, 0x61, 0x70, 0x00, 0x20, 0x4F, 0x6D, 0x6C, 0x61, 0x61, 0x67, 0x20, 0x20, 0x20, 0x48, 0x65, 0x65, 0x6E
};

void clearscreen(void) {
    // clear screen
    memset(vidmem, 0x00, 0x780);
    strcpy(vidmem, "\x06\x0DP2000T SD-CARD\x0C       \x03Pagina 1 van 1");
    for (uint8_t i = 2; i < 20; i++) {
        strcpy(vidmem + 0x50*i, "\x04\x1D\x03");
    }
    for (uint8_t i = 0; i < 4; i++) {
        memcpy(vidmem + 0x50 * (20 + i), bottom_bar + i * 40, 40);
    }
}

void clear_status_line(void) {
    memset(vidmem + 0x50 * 20, 0x00, 4 * 0x50); // clear status line
}

void init(void) {
    // deactivate SD-card
    sdcs_set();

    // set the CACHE bank
    set_ram_bank(RAM_BANK_CACHE);

    // turn LEDs off
    z80_outp(PORT_LED_IO, 0x00);

    // activate and mount sd card
    uint32_t lba0;
    if(init_sdcard() != 0 || (lba0 = read_mbr()) == 0) {
        strcpy(vidmem + 0x50*10, "\0x01Cannot connect to SD-CARD.");
        for(;;){}
    }
    read_partition(lba0);
}
