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

#include <map>
#include <string.h>

#include "common_enb.h"
#include "srslte/common/buffer_pool.h"
#include "srslte/common/log.h"
#include "srslte/common/threads.h"
#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/srslte.h"

#include "srsenb/hdr/stack/upper/sync_queue.h"

#ifndef SRSENB_GTPU_H
#define SRSENB_GTPU_H

namespace srsenb {

class gtpu final : public gtpu_interface_rrc, public gtpu_interface_pdcp
{
public:
  gtpu();

  bool init(std::string               gtp_bind_addr_,
            std::string               mme_addr_,
            std::string               m1u_multiaddr_,
            std::string               m1u_if_addr_,
            pdcp_interface_gtpu*      pdcp_,
            stack_interface_gtpu_lte* stack_,
            srslte::log*              gtpu_log_,
            int                       sync_sequence_duration_,
            int                       sync_sequence_packets_,
            bool                      enable_mbsfn = false);
  void stop();

  // gtpu_interface_rrc
  void add_bearer(uint16_t rnti, uint32_t lcid, uint32_t addr, uint32_t teid_out, uint32_t* teid_in) override;
  void rem_bearer(uint16_t rnti, uint32_t lcid) override;
  void rem_user(uint16_t rnti) override;

  // gtpu_interface_pdcp
  void write_pdu(uint16_t rnti, uint32_t lcid, srslte::unique_byte_buffer_t pdu) override;

  // stack interface
  void handle_gtpu_s1u_rx_packet(srslte::unique_byte_buffer_t pdu, const sockaddr_in& addr);
  void handle_gtpu_m1u_rx_packet(srslte::unique_byte_buffer_t pdu, const sockaddr_in& addr);

private:
  static const int GTPU_PORT = 2152;

  srslte::byte_buffer_pool* pool  = nullptr;
  stack_interface_gtpu_lte* stack = nullptr;

  bool                         enable_mbsfn = false;
  std::string                  gtp_bind_addr;
  std::string                  mme_addr;
  srsenb::pdcp_interface_gtpu* pdcp     = nullptr;
  srslte::log*                 gtpu_log = nullptr;

  // Class to create
  class m1u_handler
  {
  public:
    explicit m1u_handler(gtpu* gtpu_) : parent(gtpu_) {}
    ~m1u_handler();
    m1u_handler(const m1u_handler&) = delete;
    m1u_handler(m1u_handler&&)      = delete;
    m1u_handler& operator=(const m1u_handler&) = delete;
    m1u_handler& operator=(m1u_handler&&) = delete;
    bool         init(std::string m1u_multiaddr_, std::string m1u_if_addr_, int sync_sequence_duration_, int sync_sequence_packets_);
    void         handle_rx_packet(srslte::unique_byte_buffer_t pdu, const sockaddr_in& addr);
    bool         init_mbms_sync();

  private:
    gtpu*                parent   = nullptr;
    pdcp_interface_gtpu* pdcp     = nullptr;
    srslte::log*         gtpu_log = nullptr;
    std::string          m1u_multiaddr;
    std::string          m1u_if_addr;
    sync_queue<srslte::sync_packet_t, srslte::sync_header_type0_t> queue;

    // Workaround in order to extend life of the objects created at M1U handle_rx_packet
    // These are the real buffers that contain the packets, the queue stores only the address
    // Make room for all the packets in the tx to isolate problems
    // All data_packets = 330330 aprox, all sync_packets = 165165 aprox
    srslte::sync_packet_t sync_data_packets[340000]; // Add parameter
    srslte::sync_header_type0_t sync_info_packets[170000]; // Add parameter

    int counter = 0; // Add max_counter as parameter
    int counter_info = 0; // Add max_counter_info as parameter

    bool initiated    = false;
    int  m1u_sd       = -1;
    int  lcid_counter = 0;
  };
  m1u_handler m1u;

  typedef struct {
    uint32_t teids_in[SRSENB_N_RADIO_BEARERS];
    uint32_t teids_out[SRSENB_N_RADIO_BEARERS];
    uint32_t spgw_addrs[SRSENB_N_RADIO_BEARERS];
  } bearer_map;
  std::map<uint16_t, bearer_map> rnti_bearers;

  // Socket file descriptor
  int fd = -1;

  void echo_response(in_addr_t addr, in_port_t port, uint16_t seq);

  /****************************************************************************
   * TEID to RNIT/LCID helper functions
   ***************************************************************************/
  void teidin_to_rntilcid(uint32_t teidin, uint16_t* rnti, uint16_t* lcid);
  void rntilcid_to_teidin(uint16_t rnti, uint16_t lcid, uint32_t* teidin);
};

} // namespace srsenb

#endif // SRSENB_GTPU_H
