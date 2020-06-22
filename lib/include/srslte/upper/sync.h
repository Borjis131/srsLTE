/*
 * Copyright 2020 Universitat Politecnica de Valencia
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#ifndef SRSLTE_SYNC_PROTOCOL_H
#define SRSLTE_SYNC_PROTOCOL_H

#include <stdint.h>
#include <boost/intrusive/list.hpp>

#include "srslte/common/log.h"
#include "srslte/common/common.h"

namespace srslte {

const uint8_t SYNC_PDU_TYPE_MASK = 0xF0; // Check first 4 bits only
const uint8_t SYNC_PDU_TYPE_0 = 0x00;
const uint8_t SYNC_PDU_TYPE_1 = 0x10;
const uint8_t SYNC_PDU_TYPE_3 = 0x30;
const uint8_t SYNC_PDU_UNSUPPORTED_TYPE = 0xFF;
const uint8_t SYNC_HEADER_TYPE_0_LEN_BYTES = 18;
const uint8_t SYNC_HEADER_TYPE_1_LEN_BYTES = 11;
const uint8_t SYNC_HEADER_TYPE_3_LEN_BYTES = 19;

/****************************************************************************
 * SYNC Header prototype for common PDU fields
 * struct created to inherit from it
 * 
 *        | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 *
 * 1      |   PDU TYPE 0  |     spare     |
 * 2      |     Time Stamp (1st Octet)    |
 * 3      |     Time Stamp (2nd Octet)    |
 * 4      |   Packet Number (1st Octet)   |
 * 5      |   Packet Number (2nd Octet)   |
 * 6      |Elapsed Octet Counter (1st Oct)|
 * 7      |Elapsed Octet Counter (2nd Oct)|
 * 8      |Elapsed Octet Counter (3rd Oct)|
 * 9      |Elapsed Octet Counter (4th Oct)|
 ***************************************************************************/

struct sync_common_header_type_t/*: public boost::intrusive::list_base_hook<>*/{
    uint8_t pdu_type; // Needs 4 bits only
    uint16_t timestamp;
    uint16_t packet_number;
    uint32_t elapsed_octet_counter;
    
    uint16_t get_timestamp(){
        return timestamp;
    }

    bool operator<(const sync_common_header_type_t& r) const{
        // Check 4 bits of sync_period
        if((pdu_type & 0x0F) < (r.pdu_type & 0x0F)){
            return true;
        } else if ((pdu_type & 0x0F) > (r.pdu_type & 0x0F)){
            return false;
        } else { // Same sync_period
            if(timestamp < r.timestamp){
                return true;
            } else if (timestamp > r.timestamp){
                return false;
            } else { // Same timestamp
                if(packet_number < r.packet_number){
                    return true;
                } else { // l.packet_number > r.packet_number
                    return false;
                }
            }
        }
    }
};

inline bool operator==(const sync_common_header_type_t& l, const sync_common_header_type_t& r){
    if(l.timestamp == r.timestamp && l.packet_number == r.packet_number && l.pdu_type == r.pdu_type){
        return true;
    } else {
        return false;
    }
}

inline bool operator!=(const sync_common_header_type_t& l, const sync_common_header_type_t& r){ return !(l == r); }

/****************************************************************************
 * SYNC Header for PDU type 0
 * Transfer of Synchronisation Information without payload
 * Ref: 3GPP TS 25.446 v10.1.0 Section 5
 *
 *        | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 *
 * 1      |   PDU TYPE 0  |     spare     |
 * 2      |     Time Stamp (1st Octet)    |
 * 3      |     Time Stamp (2nd Octet)    |
 * 4      |   Packet Number (1st Octet)   |
 * 5      |   Packet Number (2nd Octet)   |
 * 6      |Elapsed Octet Counter (1st Oct)|
 * 7      |Elapsed Octet Counter (2nd Oct)|
 * 8      |Elapsed Octet Counter (3rd Oct)|
 * 9      |Elapsed Octet Counter (4th Oct)|
 * 10     |Total Number Of Packet (1st Oc)|
 * 11     |Total Number Of Packet (2nd Oc)|
 * 12     |Total Number Of Packet (3rd Oc)|
 * 13     |Total Number Of Octet (1st Oct)|
 * 14     |Total Number Of Octet (2nd Oct)|
 * 15     |Total Number Of Octet (3rd Oct)|
 * 16     |Total Number Of Octet (4th Oct)|
 * 17     |Total Number Of Octet (5th Oct)|
 * 18     |      Header CRC       |Padding|
 ***************************************************************************/

// Is it possible to inherit from list_base_hook only in common_header and not in pa
typedef struct: sync_common_header_type_t, public boost::intrusive::list_base_hook<>{
    // uint8_t pdu_type; // Needs 4 bits only
    // uint16_t timestamp;
    // uint16_t packet_number;
    // uint32_t elapsed_octet_counter;
    uint8_t total_number_of_packet[3];
    uint8_t total_number_of_octet[5];
    uint8_t header_crc; // Needs 6 bits only
} sync_header_type0_t;

/****************************************************************************
 * SYNC Header for PDU type 1
 * Transfer of User Data for MBMS with uncompressed header
 * Ref: 3GPP TS 25.446 v10.1.0 Section 5
 *
 *        | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 *
 * 1      |   PDU TYPE 1  |     spare     |
 * 2      |     Time Stamp (1st Octet)    |
 * 3      |     Time Stamp (2nd Octet)    |
 * 4      |   Packet Number (1st Octet)   |
 * 5      |   Packet Number (2nd Octet)   |
 * 6      |Elapsed Octet Counter (1st Oct)|
 * 7      |Elapsed Octet Counter (2nd Oct)|
 * 8      |Elapsed Octet Counter (3rd Oct)|
 * 9      |Elapsed Octet Counter (4th Oct)|
 * 10     |     Header CRC        |Pay CRC|
 * 11     |          Payload CRC          |
 * ...    |         Payload Fields        |
 * ...    | Payload Fields|    Padding    |  Rounded up to octets
 * Last   |        Spare Extension        |  Last 0-4 octets
 ***************************************************************************/

typedef struct: sync_common_header_type_t{
    // uint8_t pdu_type; // Needs 4 bits only
    // uint16_t timestamp;
    // uint16_t packet_number;
    // uint32_t elapsed_octet_counter;
    uint16_t crc; // Header and Payload CRC
} sync_header_type1_t;

/****************************************************************************
 * SYNC Header for PDU type 2
 * Transfer of User Data for MBMS with compressed header
 * Not used in this implementation
***************************************************************************/

/****************************************************************************
 * SYNC Header for PDU type 3
 * Transfer of Synchronisation Information with Length of Packets
 * Ref: 3GPP TS 25.446 v10.1.0 Section 5
 *
 *        | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 *
 * 1      |   PDU TYPE 3  |     spare     |
 * 2      |     Time Stamp (1st Octet)    |
 * 3      |     Time Stamp (2nd Octet)    |
 * 4      |   Packet Number (1st Octet)   |
 * 5      |   Packet Number (2nd Octet)   |
 * 6      |Elapsed Octet Counter (1st Oct)|
 * 7      |Elapsed Octet Counter (2nd Oct)|
 * 8      |Elapsed Octet Counter (3rd Oct)|
 * 9      |Elapsed Octet Counter (4th Oct)|
 * 10     |Total Number Of Packet (1st Oc)|
 * 11     |Total Number Of Packet (2nd Oc)|
 * 12     |Total Number Of Packet (3rd Oc)|
 * 13     |Total Number Of Octet (1st Oct)|
 * 14     |Total Number Of Octet (2nd Oct)|
 * 15     |Total Number Of Octet (3rd Oct)|
 * 16     |Total Number Of Octet (4th Oct)|
 * 17     |Total Number Of Octet (5th Oct)|
 * 18     |     Header CRC        |Pay CRC|
 * 19     |          Payload CRC          |
 * ...    |   Length of the 1st Packet    |
 * ...    | Len of 1st P  |  Len of 2nd P |
 * ...                   ...                
 * ...    |   Length of the Nth Packet    |  1.5*(Packet Number - 1) + 2 for odd, 1.5 * Packet Number for even
 * ...    | Len of Nth P  |    Padding    |  Rounded up to octets
 * Last   |        Spare Extension        |  Last 0-4 octets
 ***************************************************************************/

typedef struct: sync_common_header_type_t{
    // uint8_t pdu_type; // Needs 4 bits only
    // uint16_t timestamp;
    // uint16_t packet_number;
    // uint32_t elapsed_octet_counter;
    uint8_t total_number_of_packet[3];
    uint8_t total_number_of_octet[5];
    uint16_t crc; // Header and Payload CRC
} sync_header_type3_t;

struct sync_packet_t: public boost::intrusive::list_base_hook<>{
    sync_header_type1_t header;
    srslte::unique_byte_buffer_t payload;

    uint16_t get_timestamp(){
        return header.get_timestamp();
    }

    bool operator<(const sync_packet_t& r) const{
        return (header<r.header);
    }
};

inline bool operator==(const sync_packet_t& l, const sync_packet_t& r){ return (l.header == r.header); }

inline bool operator!=(const sync_packet_t& l, const sync_packet_t& r){ return !(l == r); }

bool sync_write_header_type0(sync_header_type0_t* header, srslte::byte_buffer_t* pdu, srslte::log* sync_log);
bool sync_write_header_type1(sync_header_type1_t* header, srslte::byte_buffer_t* pdu, srslte::log* sync_log);
bool sync_write_header_type3(sync_header_type3_t* header, srslte::byte_buffer_t* pdu, srslte::log* sync_log);

// This function returns the pdu type of the sync packet
uint8_t sync_read_header(srslte::byte_buffer_t* pdu, sync_common_header_type_t* header, srslte::log* sync_log);

bool sync_read_common_header_type(srslte::byte_buffer_t* pdu, sync_common_header_type_t* header, srslte::log* sync_log);
bool sync_read_header_type0(srslte::byte_buffer_t* pdu, sync_header_type0_t* header, srslte::log* sync_log);
bool sync_read_header_type1(srslte::byte_buffer_t* pdu, sync_header_type1_t* header, srslte::log* sync_log);
bool sync_read_header_type3(srslte::byte_buffer_t* pdu, sync_header_type3_t* header, srslte::log* sync_log);

inline bool sync_header_pdu_type_check(sync_common_header_type_t* header, srslte::log* sync_log){
    if((header->pdu_type & SYNC_PDU_TYPE_MASK) != SYNC_PDU_TYPE_0 && 
        (header->pdu_type & SYNC_PDU_TYPE_MASK) != SYNC_PDU_TYPE_1 &&
        (header->pdu_type & SYNC_PDU_TYPE_MASK) != SYNC_PDU_TYPE_3){
        sync_log->error("sync_header - Unhandled pdu type: 0x%x\n", header->pdu_type);
        return false;
    }
    return true;
}

} // namespace srslte

#endif // SRSLTE_SYNC_PROTOCOL_H