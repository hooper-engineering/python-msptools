/*
This file is part of python-msptools.

Python-msptools is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Python-msptools is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with python-msptools.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stddef.h>
#include "checksums.h"


const uint8_t CRC8_DVB_S2_LUT[256] = {
0x00, 0xd5, 0x7f, 0xaa, 0xfe, 0x2b, 0x81, 0x54,
0x29, 0xfc, 0x56, 0x83, 0xd7, 0x02, 0xa8, 0x7d,
0x52, 0x87, 0x2d, 0xf8, 0xac, 0x79, 0xd3, 0x06,
0x7b, 0xae, 0x04, 0xd1, 0x85, 0x50, 0xfa, 0x2f,
0xa4, 0x71, 0xdb, 0x0e, 0x5a, 0x8f, 0x25, 0xf0,
0x8d, 0x58, 0xf2, 0x27, 0x73, 0xa6, 0x0c, 0xd9,
0xf6, 0x23, 0x89, 0x5c, 0x08, 0xdd, 0x77, 0xa2,
0xdf, 0x0a, 0xa0, 0x75, 0x21, 0xf4, 0x5e, 0x8b,
0x9d, 0x48, 0xe2, 0x37, 0x63, 0xb6, 0x1c, 0xc9,
0xb4, 0x61, 0xcb, 0x1e, 0x4a, 0x9f, 0x35, 0xe0,
0xcf, 0x1a, 0xb0, 0x65, 0x31, 0xe4, 0x4e, 0x9b,
0xe6, 0x33, 0x99, 0x4c, 0x18, 0xcd, 0x67, 0xb2,
0x39, 0xec, 0x46, 0x93, 0xc7, 0x12, 0xb8, 0x6d,
0x10, 0xc5, 0x6f, 0xba, 0xee, 0x3b, 0x91, 0x44,
0x6b, 0xbe, 0x14, 0xc1, 0x95, 0x40, 0xea, 0x3f,
0x42, 0x97, 0x3d, 0xe8, 0xbc, 0x69, 0xc3, 0x16,
0xef, 0x3a, 0x90, 0x45, 0x11, 0xc4, 0x6e, 0xbb,
0xc6, 0x13, 0xb9, 0x6c, 0x38, 0xed, 0x47, 0x92,
0xbd, 0x68, 0xc2, 0x17, 0x43, 0x96, 0x3c, 0xe9,
0x94, 0x41, 0xeb, 0x3e, 0x6a, 0xbf, 0x15, 0xc0,
0x4b, 0x9e, 0x34, 0xe1, 0xb5, 0x60, 0xca, 0x1f,
0x62, 0xb7, 0x1d, 0xc8, 0x9c, 0x49, 0xe3, 0x36,
0x19, 0xcc, 0x66, 0xb3, 0xe7, 0x32, 0x98, 0x4d,
0x30, 0xe5, 0x4f, 0x9a, 0xce, 0x1b, 0xb1, 0x64,
0x72, 0xa7, 0x0d, 0xd8, 0x8c, 0x59, 0xf3, 0x26,
0x5b, 0x8e, 0x24, 0xf1, 0xa5, 0x70, 0xda, 0x0f,
0x20, 0xf5, 0x5f, 0x8a, 0xde, 0x0b, 0xa1, 0x74,
0x09, 0xdc, 0x76, 0xa3, 0xf7, 0x22, 0x88, 0x5d,
0xd6, 0x03, 0xa9, 0x7c, 0x28, 0xfd, 0x57, 0x82,
0xff, 0x2a, 0x80, 0x55, 0x01, 0xd4, 0x7e, 0xab,
0x84, 0x51, 0xfb, 0x2e, 0x7a, 0xaf, 0x05, 0xd0,
0xad, 0x78, 0xd2, 0x07, 0x53, 0x86, 0x2c, 0xf9};

uint8_t checksum_crc8_dvb_s2(const void * data, size_t size, uint8_t crc) {

    uint8_t * pos = (uint8_t *) data;
    uint8_t * end = pos + size;

    while (pos < end) crc = CRC8_DVB_S2_LUT[crc ^ *(pos++)];
    return crc;
}


uint8_t checksum_xor(const void * data, size_t size, uint8_t checksum)
{
    uint8_t *pos = (uint8_t *) data;

    for (size_t i=0; i<size; i++)
        checksum ^= *(pos++);

    return checksum;
}
