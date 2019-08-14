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

#pragma once

#include <stdint.h>
#include "msplink.h"

#define MSP_V1  'M'
#define MSP_V2  'X'

#define MSP_DIR_TOCLIENT '<'
#define MSP_DIR_TOHOST   '>'
#define MSP_DIR_ERROR    '!'


// MSP v1,v1 jumbo, v2, v2 over v1-- is pretty tangled, so you just end up having
// to stuff this structure with a decision tree instead of being able to do
// struct templates on a copied buffer.

typedef struct {
    char version;               // M or X
    char direction;             // <, >, or !
    uint8_t flag;               // V1 flag is always set to 0
    uint16_t function;          // V1 command field maps here
    uint16_t payload_size;
    uint8_t* payload;
    uint8_t checksum;
} mspPacket_t;



int parse_packet(mspdev_t* mdev, mspPacket_t* response);
