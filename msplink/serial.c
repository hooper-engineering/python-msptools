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

---------

This file contains code sourced from
https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c

Portions from Stack Overflow are licensed Creative Commons Attribution-Share Alike
https://creativecommons.org/licenses/by-sa/3.0/
*/

#include <errno.h>
#include <fcntl.h> 
#include <stdlib.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "msplink.h"

// Private functions

int set_interface_attribs(mspdev_t* mdev, int speed) {

    struct termios tty;

    if (tcgetattr(mdev->fd, &tty) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }

    if (cfsetospeed(&tty, (speed_t)speed) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }
    if (cfsetispeed(&tty, (speed_t)speed) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 0;             // allow the read to timeout after as few as 0 bytes
    tty.c_cc[VTIME] = 1;            // timeout after 0.1s

    if (tcsetattr(mdev->fd, TCSANOW, &tty) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }

    return MSP_OK;
}

// Public interface

int msplink_open(mspdev_t* mdev) {

    mdev->fd = open(mdev->devname, O_RDWR | O_NOCTTY | O_SYNC);

    if (mdev->fd < 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }

    if (set_interface_attribs(mdev, B115200) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }

    return MSP_OK;
}

int msplink_close(mspdev_t* mdev) {

    mdev->fd = close(mdev->fd);

    if (mdev->fd < 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }

    return MSP_OK;
}

int msplink_write(mspdev_t* mdev, uint8_t* data, size_t len) {

    int ret = write(mdev->fd, data, len);
    if ( ret < 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;  
    }

    if (ret != len) {
        return MSP_TX_FAIL;
    }

    return MSP_OK;
}

// either succeeds with full read count or fails with MSP_SYSCALL_FAIL or MSP_RX_FAIL
int msplink_read(mspdev_t* mdev, uint8_t* buf, size_t len) {

    int ret;
    int remaining_cnt = len;

    for (int i=0; i < mdev->read_retries; i++) {
        ret = read(mdev->fd, buf, remaining_cnt);

        if (ret<0) {
            mdev->errornum = errno;
            return MSP_SYSCALL_FAIL;
        }

        if (remaining_cnt == 0) {
            return len;
        }

        remaining_cnt -= ret;
        buf += ret;
    }

    return MSP_RX_FAIL;
}

int msplink_bytesavailable(mspdev_t* mdev) {
    int bytes_available;

    if (ioctl(mdev->fd, FIONREAD, &bytes_available) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }

    return bytes_available;
}

// This can be used to ensure the entire packet was sent before proceeding
int msplink_waituntilsent(mspdev_t* mdev) {
    if (tcdrain(mdev->fd) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }
    else {
	   return MSP_OK;
    }
}

// There can be problems with using this immediately after an open, so just use it
// before you send a request for data
// See https://stackoverflow.com/questions/13013387/clearing-the-serial-ports-buffer
int msplink_clearRxBuffer(mspdev_t* mdev) {
    if (tcflush(mdev->fd,TCIOFLUSH) != 0) {
        mdev->errornum = errno;
        return MSP_SYSCALL_FAIL;
    }
    else {
	   return MSP_OK;
    }
}



