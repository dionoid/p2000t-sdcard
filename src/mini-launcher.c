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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <z80.h>

#include "constants.h"
#include "config.h"
#include "ports.h"
#include "memory.h"
#include "ram.h"
#include "fat32-mini.h"

// set printf io
#pragma printf "%d %c %s %lu"

// function prototypes
void init(void);
void clearscreen(void);
void clear_status_line(void);
void update_pagination(void);
void call_addr(uint16_t addr) __z88dk_fastcall;
// void store_screen(void) __z88dk_fastcall;
// void restore_screen(void) __z88dk_fastcall;

// dummy terminal functions
void print_error(char* str) { (void)str; }
void print(char* str) { (void)str; }
void print_recall(char* str) { (void)str;}

uint16_t highlight_id = 1;
uint8_t page_num = 1;

void highlight_refresh(void) {
    // update highlight
    uint8_t is_folder = 0;
    for (uint8_t i = 1; i <= 16; i++) {
        is_folder = vidmem[0x50*(i+DISPLAY_OFFSET) + 36] == ')';
        memcpy(vidmem + 0x50*(i+DISPLAY_OFFSET) + 2, (i == highlight_id) ? "\x07\x3E" : (is_folder ? " \x06" : " \x03"), 2);
    }
}

void main(void) {
    // initialize SD card
    init();

    // setup screen
    clearscreen();

    // build linked list for the root directory
    build_linked_list(_current_folder_cluster);
    read_folder(1, 1);
    update_pagination();
    highlight_refresh();

    // put in infinite loop and wait for program selection
    for(;;) {
        // wait for key-press
        if(keymem[0x0C] > 0) {
            uint8_t key0 = keymem[0];
            memset(keymem, 0x00, 0x0D);
            // key down
            if(key0 == 21)  {
                if (vidmem[0x50*(highlight_id+DISPLAY_OFFSET+1) + 4] != 0x00)
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
                        if (vidmem[0x50*(i+DISPLAY_OFFSET) + 4] != 0x00) {
                            highlight_id = i;
                            break;
                        }
                    }
                }
                highlight_refresh();
            }
            //key right
            if(key0 == 23)  {
                if (_num_of_pages == 1) continue;
                if (page_num < _num_of_pages) {
                    page_num++;
                } else {
                    page_num = 1; // wrap around
                }
                clearscreen();
                read_folder(page_num, 0);
                update_pagination();
                highlight_id = 1; // highlight first item in newly loaded folder
                highlight_refresh();
            }
            //key left
            if(key0 == 0)  {
                if (_num_of_pages == 1) continue;
                if (page_num > 1) {
                    page_num--;
                } else {
                    page_num = _num_of_pages; // wrap around
                }
                clearscreen();
                read_folder(page_num, 0);
                update_pagination();
                highlight_id = 1; // highlight first item in newly loaded folder
                highlight_refresh();
            }
            // space or enter key
            if(key0 == 17 || key0 == 52)  { // space or enter
                uint32_t cluster = find_file(highlight_id + PAGE_SIZE * (page_num-1));
                if(cluster != _root_dir_first_cluster) {
                    if(_current_attrib & 0x10) {
                        if(cluster == 0) { // if zero, this is the root directory
                            _current_folder_cluster = _root_dir_first_cluster;
                        } else {
                            _current_folder_cluster = cluster;
                        }

                        build_linked_list(_current_folder_cluster);
                        page_num = 1;
                        clearscreen();
                        read_folder(1, 1);
                        update_pagination();
                        highlight_id = 1; // highlight first item in newly loaded folder
                        highlight_refresh();

                    }
                    else {
                        if (memcmp(_ext, "CAS", 3) != 0 && memcmp(_ext, "PRG", 3) != 0) {
                            // unsupported file type
                            vidmem[0x50*(highlight_id + DISPLAY_OFFSET) + 2] = 0x01; // color line red
                            continue;
                        }
                        
                        build_linked_list(cluster);

                        if (memcmp(_ext, "CAS", 3) == 0) {
                            // set RAM bank to CASSETTE
                            set_ram_bank(RAM_BANK_CASSETTE);
                            clear_status_line();
                            strcpy(vidmem + 0x50*21, "\x03Loading file...");
                            store_cas_ram(_linkedlist[0], 0x0000);
                            set_ram_bank(0);
                            break; // break out of the loop to run the program
                        }

                        // load PRG into internal RAM
                        if(memory[0x605C] < 2) {
                            vidmem[0x50*(highlight_id + DISPLAY_OFFSET) + 2] = 0x01; // color line red
                            goto restore_state;
                        }

                        store_prg_intram(_linkedlist[0], PROGRAM_LOCATION);

                        // verify that the signature is correct
                        if(memory[PROGRAM_LOCATION] != 0x50) {
                            vidmem[0x50*(highlight_id + DISPLAY_OFFSET) + 2] = 0x01; // color line red
                            goto restore_state;
                        }

                        copy_to_ram(vidmem, VIDMEM_CACHE, 0x1000);
                        call_addr(PROGRAM_LOCATION + 0x10); // launch the program
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

static const uint8_t bottom_bar[] = {
    0x13, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00,
    0x13, 0x00, 0x28, 0x3D, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x28, 0x6B, 0x29, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x45, 0x4E, 0x54, 0x45, 0x52, 0x00, 0x00, 0x6A, 0x00, 0x00, 0x00, 0x30, 0x35, 0x30, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x6E, 0x24, 0x00,
    0x13, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2C, 0x2E, 0x00, 0x00, 0x00, 0x22, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00,
    0x06, 0x50, 0x61, 0x67, 0x69, 0x6E, 0x61, 0x00, 0x53, 0x63, 0x72, 0x6F, 0x6C, 0x6C, 0x00, 0x00, 0x53, 0x74, 0x61, 0x72, 0x74, 0x00, 0x41, 0x70, 0x70, 0x00, 0x00, 0x53, 0x63, 0x72, 0x6F, 0x6C, 0x6C, 0x00, 0x50, 0x61, 0x67, 0x69, 0x6E, 0x61,
    0x06, 0x54, 0x65, 0x72, 0x75, 0x67, 0x00, 0x00, 0x4F, 0x6D, 0x68, 0x6F, 0x6F, 0x67, 0x00, 0x00, 0x4F, 0x70, 0x65, 0x6E, 0x00, 0x00, 0x4D, 0x61, 0x70, 0x00, 0x00, 0x4F, 0x6D, 0x6C, 0x61, 0x61, 0x67, 0x00, 0x00, 0x00, 0x48, 0x65, 0x65, 0x6E
};

void clearscreen(void) {
    // clear screen
    memset(vidmem, 0x00, 0x780);
    strcpy(vidmem, "\x06P2000T SD-CARD");
    for (uint8_t i = 1; i < 19; i++) {
        strcpy(vidmem + 0x50*i, "\x04\x1D");
    }
    for (uint8_t i = 0; i < 5; i++) {
        memcpy(vidmem + 0x50 * (19 + i), bottom_bar + i * 40, 40);
    }
}

void update_pagination(void) {
    char pagina_str[32];
    sprintf(pagina_str, " \x03Pagina %d van %d", page_num, _num_of_pages);
    strcpy(vidmem + 39 - strlen(pagina_str), pagina_str);
}

void clear_status_line(void) {
    memset(vidmem + 0x50 * 19, 0x00, 5 * 0x50); // clear status line
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
        strcpy(vidmem + 0x50*10, "\x01Cannot connect to SD-CARD.");
        for(;;){}
    }
    read_partition(lba0);
}
