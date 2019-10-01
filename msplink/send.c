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

#include <endian.h>
#include <stdint.h>
#include <stddef.h>

#include "send.h"
#include "msplink.h"
#include "serial.h"
#include "checksums.h"

/**
 *  MSP V1 packet sender
 *
 *  @param mdev        [in]    an MSP device pointer
 *  @param cmd         [in]    an MSP command number
 *  @param payload     [in]    the command payload data
 *  @param payload_len [in] lenght of the command payload data
 *
 *  Generates an MSP V1 packet with the given parameter data and sends
 *  it to the serial port. If the given payload_len is greater than 254,
 *  a JUMBO packet will be generated.
 *
 */
int send_V1(mspdev_t* mdev, uint8_t cmd, uint8_t* payload, uint16_t payload_len) {

    uint8_t checksum = 0;
    int ret;
    uint8_t buf[5];
    uint8_t* pBuf = buf;

    union {
        uint8_t bytes[2];
        uint16_t value;
    } payload_len_le;

    *(pBuf++) = '$';
    *(pBuf++) = 'M';
    *(pBuf++) = '<';

    if (payload_len > 254)      {*(pBuf++) = 255;}
    else                        {*(pBuf++) = payload_len;}

    *pBuf = cmd;

    checksum = payload_len ^ cmd;

    ret = msplink_write(mdev, buf, 5);
    if (ret<0) {return ret;}

    // Generate a JUMBO packet by putting the real payload size
    // in the first two bytes after the Command byte.
    // There is ambiguity in whether the length should include the two length bytes or not,
    // but based on the way the protocol description is written, I'll assume not.
    if (payload_len > 254) {
        payload_len_le.value = htole16(payload_len);
        ret = msplink_write(mdev, payload_len_le.bytes, 2);
        if (ret<0) {return ret;}

        checksum ^= payload_len_le.bytes[0];
        checksum ^= payload_len_le.bytes[1];
    }

    ret = msplink_write(mdev, payload, payload_len);
    if (ret<0) {return ret;}

    checksum = checksum_xor(payload, payload_len, checksum);

    ret = msplink_write(mdev, &checksum, 1);
    if (ret<0) {return ret;}

    return MSP_OK;
}


/**
 *  MSP V2 packet sender
 *
 *  @param mdev        [in]    an MSP device pointer
 *  @param flag        [in]    packet flag value
 *  @param cmd         [in]    an MSP command number
 *  @param payload     [in]    the command payload data
 *  @param payload_len [in] length of the command payload data
 *
 *  Generates an MSP V2 packet with the given parameter data and sends it to the serial port.
 *
 */
int send_V2(mspdev_t* mdev, uint8_t flag, uint16_t cmd, uint8_t* payload, uint16_t payload_len) {

    uint8_t checksum = 0;
    int ret;
    uint8_t buf[7];
    uint8_t* pBuf = buf;

    union byteswaps {
        uint8_t bytes[2];
        uint16_t value;
    } cmd_le, payload_len_le;

    cmd_le.value = htole16(cmd);
    payload_len_le.value = htole16(payload_len);

    *(pBuf++) = '$';
    *(pBuf++) = 'X';
    *(pBuf++) = '<';
    *(pBuf++) = flag;
    *(pBuf++) = cmd_le.bytes[0]; 
    *(pBuf++) = cmd_le.bytes[1];
    *(pBuf++) = payload_len_le.bytes[0];
    *pBuf = payload_len_le.bytes[1];

    ret = msplink_write(mdev, buf, 8);
    if (ret<0) {return ret;}

    ret = msplink_write(mdev, payload, payload_len);
    if (ret<0) {return ret;}

    checksum = checksum_crc8_dvb_s2(&buf[3], 5, 0);
    checksum = checksum_crc8_dvb_s2(payload, payload_len, checksum);

    ret = msplink_write(mdev, &checksum, 1);
    if (ret<0) {return ret;}

    return MSP_OK;
}
