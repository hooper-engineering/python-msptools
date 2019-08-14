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


/*
Assumptions:
- Once a packet has started transmission, all inter-byte spacing will be less than 0.1 seconds
*/

#include <stdint.h>
#include <endian.h>

#include "parse.h"
#include "msplink.h"
#include "serial.h"
#include "checksums.h"

#define MAX_SYNC_SEARCH_BYTES 50                // Number of bytes to search for sync before giving up

// Rx buffer should have been flushed before TX, so there shouldn't be much to weed through.
int get_sync(mspdev_t* mdev) {

    int ret=0;
    uint8_t buf;

    for (int i=0; i < MAX_SYNC_SEARCH_BYTES; i++) {

        ret = msplink_read(mdev, &buf, 1);      // Note: If the retry count in msplink_read() is exhausted

        // Bail if no bytes are available after mdev->read_retries
        // This should prevent excessive time-wasting when no bytes are showing up.
        if (ret == MSP_RX_FAIL) {break;}        // Special case, the error MSP_RX_SYNC_NOT_FOUND is more informative to user.
        else if (ret<0)         {return ret;}   // Something worse happened!
                                                
        if (buf == '$')
            return MSP_OK;
    }

    return MSP_RX_SYNC_NOT_FOUND;
}

/**
 *  MSP V2 packet parser
 *
 *  @param mdev     [in]    an MSP device pointer
 *  @param response [out]   an MSP packet pointer to hold returned data
 *
 *  @warning Do not call this function directly.
 *
 *  -At this point sync byte, MSP version char, and direction char are consumed {'$', ['M', 'X'], ['<','!']}
 *  -Read flag, function, payload_size fields
 *  -Determine if payload is too big
 *  -Read payload and checksum
 *  -Calculate checksum and compare
 *
 */
int parse_V2(mspdev_t* mdev, mspPacket_t* pkt) {
    int ret = 0;

    uint8_t checksum = 0;

    union {
        uint8_t bytes[5];
        struct {
            uint8_t flag;
            uint16_t function;
            uint16_t payload_size;
        } __attribute__((packed)) values;
    } buffer;

    ret = msplink_read(mdev, buffer.bytes, 5);
    if (ret<0) {return ret;}

    checksum = checksum_crc8_dvb_s2(buffer.bytes, 5, 0);

    pkt->flag = buffer.values.flag;
    pkt->function = le16toh(buffer.values.function);
    pkt->payload_size = le16toh(buffer.values.payload_size);

    if (pkt->payload_size > READ_BUFFER_SIZE-1) {
        // TODO: dynamically allocate buffer
        return MSP_OUT_OF_MEMORY;
    }

    ret = msplink_read(mdev, mdev->buf, pkt->payload_size+1);
    if (ret<0) {return ret;}

    pkt->payload = mdev->buf;
    pkt->checksum = mdev->buf[pkt->payload_size];

    checksum = checksum_crc8_dvb_s2(mdev->buf, pkt->payload_size, checksum);

    if (pkt->checksum != checksum)
        {return MSP_RX_CHECKSUM_MISMATCH;}
    else
        {return MSP_OK;}
}

/**
 *  MSP V1 packet parser
 *
 *  @param mdev     [in]    an MSP device pointer
 *  @param response [out]   an MSP packet pointer to hold returned data
 *
 *  @warning Do not call this function directly.
 *
 *  -At this point sync byte, MSP version char, and direction char are consumed {'$', ['M', 'X'], ['<','!']}
 *  -Read payload size and command byte
 *  -Determine if a JUMBO packet was received (length=255) and consume actual length from start of payload (2 bytes)
 *  -Determine if a V2 packet is encapsulated in this V1 packet (function=255) and transfer to V2 parser if so
 *  -Determine if buffer is large enough for rx data
 *  -Read payload and checksum byte
 *  -Calculate checksum and compare
 *
 */
int parse_V1(mspdev_t* mdev, mspPacket_t* pkt) {

    int ret = 0;
    uint8_t checksum = 0;

    uint8_t buf[2];

    union {
        uint8_t bytes[2];
        uint16_t value;
    } payload_len_le;

    pkt->flag = 0;      // V1 has no flag field

    ret = msplink_read(mdev, buf, 2);
    if (ret<0) {return ret;}

    checksum = checksum_xor(buf, 2, 0);
    pkt->payload_size = buf[0];
    pkt->function = buf[1];

    if (pkt->payload_size == 0xff) {        // JUMBO packet

        // Actual payload size is the first two bytes of the payload
        ret = msplink_read(mdev, payload_len_le.bytes, 2);
        if (ret<0) {return ret;}

        checksum = checksum_xor(payload_len_le.bytes, 2, checksum);

        pkt->payload_size = le16toh(payload_len_le.value);
    }

    // If function == 0xff, the payload is a V2 packet.
    // if this is the case, we can just ignore the V1 checksum because the V2
    // checksum is already checked

    if (pkt->function == 0xff) {
        ret = parse_V2(mdev, pkt);

        if (ret<0)  {return ret;}
        else        {return MSP_OK;}
    }

    if (pkt->payload_size > READ_BUFFER_SIZE-1) {
        // TODO: dynamically allocate buffer
        return MSP_OUT_OF_MEMORY;
    }

    ret = msplink_read(mdev, mdev->buf, pkt->payload_size+1);
    if (ret<0) {return ret;}

    pkt->payload = mdev->buf;
    pkt->checksum = mdev->buf[pkt->payload_size];

    checksum = checksum_xor(mdev->buf, pkt->payload_size, checksum);

    if (pkt->checksum != checksum)  {return MSP_RX_CHECKSUM_MISMATCH;}
    else                            {return MSP_OK;}
}

/**
 *  MSP packet parser
 *
 *  @param mdev     [in]    an MSP device pointer
 *  @param response [out]   an MSP packet pointer to hold returned data
 *
 *  -Block until all Tx bytes have gone out
 *  -Look for sync byte '$'
 *  -Look for MSP version character 'M' or 'X'
 *  -Split path based on MSP packet version
 *
 */
int parse_packet(mspdev_t* mdev, mspPacket_t* response) {

    int ret = 0;
    uint8_t headbytes[2];

    ret = msplink_waituntilsent(mdev);
    if (ret<0) {return ret;}

    ret = get_sync(mdev);
    if (ret<0) {return ret;}

    ret = msplink_read(mdev, headbytes, 2);
    if (ret<0) {return ret;}

    response->version = headbytes[0];       // 'X' or 'M'
    response->direction = headbytes[1]; // '<' '>' or '!'

    switch (response->version) {
        case MSP_V1:
            ret = parse_V1(mdev, response);
            if (ret<0) {return ret;}
            break;
        case MSP_V2:
            ret = parse_V2(mdev, response);
            if (ret<0) {return ret;}
            break;
        default:
            return MSP_LIB_INTERNAL_ERROR;
    }

    if (response->direction == MSP_DIR_ERROR) {return MSP_RX_CLIENT_NACK;}

    return MSP_OK;
}
