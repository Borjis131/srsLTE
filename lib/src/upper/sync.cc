/*
 * Copyright 2020 Universidad Politecnica de Valencia
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

#include "srslte/upper/sync.h"
#include "srslte/common/int_helpers.h"

namespace srslte {

/****************************************************************************
 * Header pack/unpack helper functions
 * Ref: 3GPP TS 25.446 v10.1.0 Section 5
 ***************************************************************************/

// Not ready to be used
bool sync_write_header_type0(sync_header_type0_t* header, srslte::byte_buffer_t* pdu, srslte::log* sync_log){
    if(!sync_header_pdu_type_check(header, sync_log)){
        sync_log->error("sync_write_header_type0 - Unhandled SYNC PDU Type. PDU Type: 0x%x\n", header->pdu_type);
        return false;
    }

    // This is a message itself, not a header. See how to implement it
    /*
    if(pdu->get_headroom() < SYNC_HEADER_TYPE_0_LEN_BYTES){
      sync_log->error("sync_write_header_type0 - No room in PDU for header\n");
      return false;
    }

    pdu->msg -= SYNC_HEADER_TYPE_0_LEN_BYTES;
    pdu->N_bytes += SYNC_HEADER_TYPE_0_LEN_BYTES;*/

    uint8_t* ptr = pdu->msg;
    *ptr = header->pdu_type;
    ptr++;
    uint16_to_uint8(header->timestamp, ptr);
    ptr += 2;
    uint16_to_uint8(header->packet_number, ptr);
    ptr += 2;
    uint32_to_uint8(header->elapsed_octet_counter, ptr);
    ptr += 4;
    uint24_to_uint8_mod(header->total_number_of_packet, ptr);
    ptr += 3;
    uint40_to_uint8_mod(header->total_number_of_octet, ptr);
    ptr += 5;
    *ptr = header->header_crc;
    return true;
}

bool sync_write_header_type1(sync_header_type1_t* header, srslte::byte_buffer_t* pdu, srslte::log* sync_log){
    if(!sync_header_pdu_type_check(header, sync_log)){
        sync_log->error("sync_write_header_type1 - Unhandled SYNC PDU Type. PDU Type: 0x%x\n", header->pdu_type);
        return false;
    }

    // Make room for header in pdu
    if(pdu->get_headroom() < SYNC_HEADER_TYPE_1_LEN_BYTES){
      sync_log->error("sync_write_header_type1 - No room in PDU for header\n");
      return false;
    }
    pdu->msg -= SYNC_HEADER_TYPE_0_LEN_BYTES;
    pdu->N_bytes += SYNC_HEADER_TYPE_0_LEN_BYTES;

    uint8_t* ptr = pdu->msg;
    *ptr = header->pdu_type;
    ptr++;
    uint16_to_uint8(header->timestamp, ptr);
    ptr += 2;
    uint16_to_uint8(header->packet_number, ptr);
    ptr += 2;
    uint32_to_uint8(header->elapsed_octet_counter, ptr);
    ptr += 4;
    uint16_to_uint8(header->crc, ptr);
    return true;
}
} // namespace srslte