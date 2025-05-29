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

#include "fat32.h"

#define MAX_FILENAME_LENGTH          39 // 3 * 13

uint8_t _sectors_per_cluster = 0;
uint16_t _reserved_sectors = 0;
uint8_t _number_of_fats = 0;
uint32_t _sectors_per_fat = 0;
uint32_t _root_dir_first_cluster = 0;
uint32_t _linkedlist[16];
uint32_t _fat_begin_lba = 0;
uint32_t _SECTOR_begin_lba = 0;
uint32_t _lba_addr_root_dir = 0;
uint32_t _filesize_current_file = 0;
uint32_t _current_folder_cluster = 0;

uint8_t _filename[MAX_FILENAME_LENGTH+1];
char _ext[4] = {0};
char _short_name[9] = {0};
uint8_t _current_attrib = 0;

uint8_t _curr_file_ctr = 0;


/**
 * @brief Read the Master Boot Record
 * 
 * @return uint32_t sector-address of the first partition
 */
uint32_t read_mbr(void) {
    // read the first sector of the SD card
    read_sector(0x00000000);

    if(ram_read_uint16_t(SDCACHE0 + 510) != 0xAA55) {
        return 0;
    } else {
        return ram_read_uint32_t(SDCACHE0 + 0x1C6);
    }
}

/**
 * @brief Read metadata of the partition
 * 
 * @param lba0 address of the partition
 */
void read_partition(uint32_t lba0) {
    // read the volume ID (first sector of the partition)
    read_sector(lba0);

    // collect data
    _sectors_per_cluster = ram_read_uint8_t(SDCACHE0 + 0x0D);
    _reserved_sectors = ram_read_uint16_t(SDCACHE0 + 0x0E);
    _number_of_fats = ram_read_uint8_t(SDCACHE0 + 0x10);
    _sectors_per_fat = ram_read_uint32_t(SDCACHE0 + 0x24);
    _root_dir_first_cluster = ram_read_uint32_t(SDCACHE0 + 0x2C);
    _current_folder_cluster = _root_dir_first_cluster;

    // consolidate variables
    _fat_begin_lba = lba0 + _reserved_sectors;
    _SECTOR_begin_lba = lba0 + _reserved_sectors + (_number_of_fats * _sectors_per_fat);
}

/**
 * @brief Read the contents of the root folder and search for a file identified 
 *        by file id. When a negative file_id is supplied, the directory is
 *        simply scanned and the list of files are outputted to the screen.
 * 
 * @param file_id ith file in the folder
 * @return uint32_t first cluster of the file or directory
 */
uint32_t read_folder(int16_t file_id, uint8_t casrun) {
    (void)casrun; // unused parameter, but needed for compatibility

    // loop over the clusters and read directory contents
    uint8_t ctr = 0;                // counter over clusters
    uint16_t fctr = 0;              // counter over directory entries (files and folders)
    uint32_t totalfilesize = 0;     // collect size of files in folder
    uint8_t stopreading = 0;        // whether to break of reading procedure
    uint16_t loc = 0;               // current entry position
    uint8_t firstPos = 0;
    uint8_t lfn_found = 0; 

    while(_linkedlist[ctr] != 0xFFFFFFFF && ctr < F_LL_SIZE && stopreading == 0) {
        
        // print cluster number and address
        uint32_t caddr = calculate_sector_address(_linkedlist[ctr], 0);

        // loop over all sectors per cluster
        for(uint8_t i=0; i<_sectors_per_cluster && stopreading == 0; i++) {
            read_sector(caddr);            // read sector data
            loc = SDCACHE0;
            for(uint8_t j=0; j<16; j++) { // 16 file tables per sector
                // check first position
                firstPos = ram_read_uint8_t(loc);
                _current_attrib = ram_read_uint8_t(loc + 0x0B);    // attrib byte

                // continue if an unused entry is encountered 0xE5
                if(firstPos == 0xE5) {
                    loc += 32;  // next file entry location
                    continue;
                }

                // early exit if a zero is read
                if(firstPos == 0x00) {
                    stopreading = 1;
                    break;
                }

                // check for LFN entry
                if ((_current_attrib & 0x0F) == 0x0F) {
                    if (file_id < 0) {
                        if (!lfn_found) {
                            lfn_found = 1;  // indicate LNF found
                            memset(_filename, 0, MAX_FILENAME_LENGTH+1);
                        }
                        uint8_t seq = firstPos & 0x1F;  // LFN sequence number
                        uint8_t k = 0;
                        if (seq <= 3) {
                            // extract characters from LFN entry
                            for (k = 0; k < 5; k++) _filename[(seq - 1) * 13 + k] = ram_read_uint8_t(loc + 1 + k * 2);
                            for (k = 0; k < 6; k++) _filename[(seq - 1) * 13 + 5 + k] = ram_read_uint8_t(loc + 14 + k * 2);
                            for (k = 0; k < 2; k++) _filename[(seq - 1) * 13 + 11 + k] = ram_read_uint8_t(loc + 28 + k * 2);
                        }
                    }
                }

                // check for SFN entry
                if((_current_attrib & 0x0F) == 0x00) {

                    // get short name and extension
                    copy_from_ram(loc, _short_name, 8);
                    copy_from_ram(loc+8, _ext, 3);

                    if(((_current_attrib & 0x10) && memcmp(_short_name, ". ", 2) != 0) 
                        || memcmp(_ext, "CAS", 3) == 0
                        || memcmp(_ext, "PRG", 3) == 0) {

                        fctr++;
                        if (file_id < 0 || fctr == file_id) {
                            const uint32_t fc = grab_cluster_address_from_fileblock(loc);
                            _filesize_current_file = ram_read_uint32_t(loc + 0x1C);
                            
                            if(file_id < 0) {

                                totalfilesize += _filesize_current_file;
                            
                                // if no LFN found, the SFN filename needs to be formatted
                                if (!lfn_found) {
                                    //copy_from_ram(loc, _filename, 8);
                                    memcpy(_filename, _short_name, 8);
                                    memcpy(_filename + 9, _ext, 4); // copy extension (incl terminator)
                                    // if file, inject dot before extension
                                    _filename[8] = (_current_attrib & 0x10) ? '\0' : '.';
                                    // remove superfluous spaces before extension
                                    uint8_t k = 0;
                                    for (k = 7; k >= 1 && _filename[k] == ' '; k--);
                                    if (k < 7) memcpy(&_filename[k+1], &_filename[8], 5);
                                }

                                if(_current_attrib & 0x10) {
                                    // directory entry
                                    if (memcmp(_short_name, "..", 2) == 0)
                                        strcpy(_filename, "(map terug)");

                                    strcpy(vidmem + 0x50*(fctr+2) + 4, _filename);
                                    vidmem[0x50*(fctr+2) + 4 + strlen(_filename)] = '/';
                                } else {
                                    // file entry          
                                    strcpy(vidmem + 0x50*(fctr+2) + 4, _filename);
                                }

                                if(fctr % 16 == 0) {
                                    stopreading = 1;
                                    break;
                                }
                            }

                            if(fctr == file_id) {
                                return fc;
                            }
                        }
                    }
                    lfn_found = 0; // reset LFN tracking 
                }
                loc += 32;  // next file entry location
            }
            caddr++;    // next sector
        }
        ctr++;  // next cluster
    }

    return _root_dir_first_cluster;
}


/**
 * @brief Build a linked list of sector addresses starting from a root address
 * 
 * @param nextcluster first cluster in the linked list
 */
void build_linked_list(uint32_t nextcluster) {
    // counter over clusters
    uint8_t ctr = 0;

    // clear previous linked list
    memset(_linkedlist, 0xFF, F_LL_SIZE * sizeof(uint32_t));

    // try grabbing next cluster
    while(nextcluster < 0x0FFFFFF8 && nextcluster != 0 && ctr < F_LL_SIZE) {
        _linkedlist[ctr] = nextcluster;
        read_sector(_fat_begin_lba + (nextcluster >> 7));
        uint8_t item = nextcluster & 0b01111111;
        nextcluster = ram_read_uint32_t(item * 4);
        ctr++;
    }
}

/**
 * @brief Calculate the sector address from cluster and sector
 * 
 * @param cluster which cluster
 * @param sector which sector on the cluster (0-Nclusters)
 * @return uint32_t sector address (512 byte address)
 */
uint32_t calculate_sector_address(uint32_t cluster, uint8_t sector) {
    return _SECTOR_begin_lba + (cluster - 2) * _sectors_per_cluster + sector;   
}

/**
 * @brief Construct sector address from file entry
 * 
 * @return uint32_t 
 */
uint32_t grab_cluster_address_from_fileblock(uint16_t loc) {
    return (uint32_t)ram_read_uint16_t(loc + 0x14) << 16 | 
                     ram_read_uint16_t(loc + 0x1A);
}

/**
 * @brief Store a file in the external ram
 * 
 * @param faddr    cluster address of the file
 * @param ram_addr first position in ram to store the file
 * @param verbose  whether to show progress
 */
void store_cas_ram(uint32_t faddr, uint16_t ram_addr) {
    build_linked_list(faddr);

    // count number of clusters
    uint8_t ctr = 0;
    uint8_t total_sectors = _filesize_current_file / 512 + 
                            (_filesize_current_file % 512 != 0 ? 1 : 0);
    uint32_t caddr = 0;
    uint16_t nbytes = 0;    // count number of bytes
    uint8_t sector_ctr = 0; // counter sector

    ctr = 0;
    while(_linkedlist[ctr] != 0xFFFFFFFF && ctr < 16 && nbytes < _filesize_current_file) {

        // calculate address of sector
        caddr = calculate_sector_address(_linkedlist[ctr], 0);

        // loop over all sectors given a cluster and copy the data to RAM
        for(uint8_t i=0; i<_sectors_per_cluster; i++) {

            if(sector_ctr == 0) {
                // program length and transfer address
                read_sector(caddr); // read sector data
                ram_write_uint16_t(0x8000, ram_read_uint16_t(SDCACHE0 + 0x0030));
                ram_write_uint16_t(0x8002, ram_read_uint16_t(SDCACHE0 + 0x0032));
                ram_transfer(0x100, ram_addr, 0x100);
                ram_addr += 0x100;
            } else {
                // open command for sending sector retrieval address
                open_command();
                cmd17(caddr);    // prime SD-card for data retrieval

                // perform fast data transfer using custom assembly routines
                switch(sector_ctr % 5) {
                    case 0:
                        // preamble is first 0x100 bytes of sector
                        fast_sd_to_ram_last_0x100(ram_addr);
                        ram_addr += 0x100;
                    break;
                    case 2:
                        // preamble is last 0x100 bytes of sector
                        fast_sd_to_ram_first_0x100(ram_addr);
                        ram_addr += 0x100;
                    break;
                    default: // 1,3,4 are complete blocks
                        fast_sd_to_ram_full(ram_addr);
                        ram_addr += 0x200;
                    break;
                }

                // close command
                close_command();
            }

            nbytes += 512;
            if(nbytes >= _filesize_current_file) {
                break;
            }
            caddr++;
            sector_ctr++;
        }
        ctr++;
    }
}

/**
 * @brief Store a file in the internal ram
 * 
 * @param faddr    cluster address of the file
 * @param ram_addr first position in ram to store the file
 */
void store_prg_intram(uint32_t faddr, uint16_t ram_addr) {
    build_linked_list(faddr);

    // count number of clusters
    uint8_t ctr = 0;
    uint8_t cursec = 0;
    uint8_t total_sectors = _filesize_current_file / 512 + 
                            (_filesize_current_file % 512 != 0 ? 1 : 0);
    uint32_t caddr = 0;
    uint16_t nbytes = 0;    // count number of bytes

    ctr = 0;
    while(_linkedlist[ctr] != 0xFFFFFFFF && ctr < 16 && nbytes < _filesize_current_file) {

        // calculate address of sector
        caddr = calculate_sector_address(_linkedlist[ctr], 0);

        // loop over all sectors given a cluster and copy the data to RAM
        for(uint8_t i=0; i<_sectors_per_cluster; i++) {

            // copy sector over to internal memory
            open_command();
            cmd17(caddr);
            fast_sd_to_intram_full(ram_addr);
            close_command();

            // increment ram pointer
            ram_addr += 0x200;

            nbytes += 512;
            if(nbytes >= _filesize_current_file) {
                break;
            }
            caddr++;
            cursec++;
        }
        ctr++;
    }
}