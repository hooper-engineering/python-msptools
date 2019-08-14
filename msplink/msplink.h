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
#include <pthread.h>

#define READ_BUFFER_SIZE 1024
#define MSP_RETRY_DEFAULT 3

typedef struct {
    int fd;
    char* devname;
    int read_retries;
    uint8_t buf[READ_BUFFER_SIZE];
    int mspversion;
    int device_open;
    int errornum;
    pthread_mutex_t instanceLock;
} mspdev_t;

enum MSP_ERRORS {
    MSP_OK = 0,
    MSP_SYSCALL_FAIL = -1,
    MSP_LIB_INTERNAL_ERROR = -2,
    MSP_TX_FAIL = -3,
    MSP_RX_FAIL = -4,
    MSP_RX_SYNC_NOT_FOUND = -5,
    MSP_RX_CHECKSUM_MISMATCH = -6,
    MSP_OUT_OF_MEMORY = -7,
    MSP_RX_CLIENT_NACK = -8
};
