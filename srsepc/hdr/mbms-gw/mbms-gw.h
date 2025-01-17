/*
 * Copyright 2013-2019 Software Radio Systems Limited
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

/******************************************************************************
 * File:        mbms-gw.h
 * Description: Top-level MBMS-GW class. Creates and links all
 *              interfaces and helpers.
 *****************************************************************************/

#ifndef MBMS_GW_H
#define MBMS_GW_H

#include "srslte/asn1/gtpc.h"
#include "srslte/common/buffer_pool.h"
#include "srslte/common/log.h"
#include "srslte/common/log_filter.h"
#include "srslte/common/logger_file.h"
#include "srslte/common/threads.h"
#include "srslte/srslte.h"
#include <cstddef>

namespace srsepc {

const uint16_t GTPU_RX_PORT = 2152;

typedef struct {
  std::string name;
  std::string sgi_mb_if_name;
  std::string sgi_mb_if_addr;
  std::string sgi_mb_if_mask;
  std::string m1u_multi_addr;
  std::string m1u_multi_if;
  int         m1u_multi_ttl;
  int         sync_sequence_packets;  // Number of packets in the SYNC sequence
} mbms_gw_args_t;

struct pseudo_hdr {
  uint32_t src_addr;
  uint32_t dst_addr;
  uint8_t  placeholder;
  uint8_t  protocol;
  uint16_t udp_len;
};

class mbms_gw : public thread
{
public:
  static mbms_gw* get_instance(void);
  static void     cleanup(void);
  int             init(mbms_gw_args_t* args, srslte::log_filter* mbms_gw_log);
  void            stop();
  void            run_thread();

private:
  /* Methods */
  mbms_gw();
  virtual ~mbms_gw();
  static mbms_gw* m_instance;

  int      init_sgi_mb_if(mbms_gw_args_t* args);
  int      init_m1_u(mbms_gw_args_t* args);
  void     handle_sgi_md_pdu(srslte::byte_buffer_t* msg);
  uint16_t in_cksum(uint16_t* iphdr, int count);
  void     synchronisation_information(uint16_t timestamp); // function to send SYNC PDU type 0
  void     send_sync_period_reference(); // function that uses SYNC PDU type 3 to send the absolute time reference

  /* Members */
  bool                      m_running;
  srslte::byte_buffer_pool* m_pool;
  srslte::log_filter*       m_mbms_gw_log;

  // SYNC protocol functionalities
  uint16_t timestamp;
  uint16_t packet_number;
  uint32_t elapsed_octet_counter;
  uint32_t total_number_of_packet; // change to uint8_t[3]
  uint64_t total_number_of_octet; // change to uint8_t[5]
  bool locked = false; // Send SYNC period reference only one time
  uint8_t current_sync_period; // Flag to differentiate between SYNC periods while sorting
  int sync_sequence_packets; // Packets per SYNC sequence

  bool m_sgi_mb_up;
  int  m_sgi_mb_if;

  bool               m_m1u_up;
  int                m_m1u;
  struct sockaddr_in m_m1u_multi_addr;
};

} // namespace srsepc

#endif // SGW_H
