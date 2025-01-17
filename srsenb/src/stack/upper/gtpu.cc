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
#include "srslte/upper/gtpu.h"
#include "srsenb/hdr/stack/upper/gtpu.h"
#include "srslte/common/network_utils.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include <pthread.h>
#include <inttypes.h>

#include "srslte/upper/sync.h"

using namespace srslte;
namespace srsenb {

gtpu::gtpu() : m1u(this) {}

bool gtpu::init(std::string                  gtp_bind_addr_,
                std::string                  mme_addr_,
                std::string                  m1u_multiaddr_,
                std::string                  m1u_if_addr_,
                srsenb::pdcp_interface_gtpu* pdcp_,
                stack_interface_gtpu_lte*    stack_,
                srslte::log*                 gtpu_log_,
                int                          sync_sequence_duration_,
                int                          sync_sequence_packets_,
                bool                         enable_mbsfn_)
{
  pdcp          = pdcp_;
  gtpu_log      = gtpu_log_;
  gtp_bind_addr = gtp_bind_addr_;
  mme_addr      = mme_addr_;
  pool          = byte_buffer_pool::get_instance();
  stack         = stack_;

  // Set up socket
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    gtpu_log->error("Failed to create socket\n");
    return false;
  }
  int enable = 1;
#if defined(SO_REUSEADDR)
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    gtpu_log->error("setsockopt(SO_REUSEADDR) failed\n");
#endif
#if defined(SO_REUSEPORT)
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
    gtpu_log->error("setsockopt(SO_REUSEPORT) failed\n");
#endif

  struct sockaddr_in bindaddr;
  bzero(&bindaddr, sizeof(struct sockaddr_in));
  bindaddr.sin_family      = AF_INET;
  bindaddr.sin_addr.s_addr = inet_addr(gtp_bind_addr.c_str());
  bindaddr.sin_port        = htons(GTPU_PORT);

  if (bind(fd, (struct sockaddr*)&bindaddr, sizeof(struct sockaddr_in))) {
    gtpu_log->error("Failed to bind on address %s, port %d\n", gtp_bind_addr.c_str(), GTPU_PORT);
    gtpu_log->console("Failed to bind on address %s, port %d\n", gtp_bind_addr.c_str(), GTPU_PORT);
    return false;
  }

  stack->add_gtpu_s1u_socket_handler(fd);

  // Start MCH socket if enabled
  enable_mbsfn = enable_mbsfn_;
  if (enable_mbsfn) {
    if (not m1u.init(m1u_multiaddr_, m1u_if_addr_, sync_sequence_duration_, sync_sequence_packets_)) {
      return false;
    }
  }
  return true;
}

void gtpu::stop()
{
  if (fd) {
    close(fd);
  }
}

// gtpu_interface_pdcp
void gtpu::write_pdu(uint16_t rnti, uint32_t lcid, srslte::unique_byte_buffer_t pdu)
{
  gtpu_log->info_hex(pdu->msg, pdu->N_bytes, "TX PDU, RNTI: 0x%x, LCID: %d, n_bytes=%d", rnti, lcid, pdu->N_bytes);

  // Check valid IP version
  struct iphdr* ip_pkt = (struct iphdr*)pdu->msg;
  if (ip_pkt->version != 4 && ip_pkt->version != 6) {
    gtpu_log->error("Invalid IP version to SPGW\n");
    return;
  } else if (ip_pkt->version == 4) {
    if (ntohs(ip_pkt->tot_len) != pdu->N_bytes) {
      gtpu_log->error("IP Len and PDU N_bytes mismatch\n");
    }
    gtpu_log->debug("S1-U PDU -- IP version %d, Total length %d\n", ip_pkt->version, ntohs(ip_pkt->tot_len));
    gtpu_log->debug("S1-U PDU -- IP src addr %s\n", srslte::gtpu_ntoa(ip_pkt->saddr).c_str());
    gtpu_log->debug("S1-U PDU -- IP dst addr %s\n", srslte::gtpu_ntoa(ip_pkt->daddr).c_str());
  }

  gtpu_header_t header;
  header.flags        = GTPU_FLAGS_VERSION_V1 | GTPU_FLAGS_GTP_PROTOCOL;
  header.message_type = GTPU_MSG_DATA_PDU;
  header.length       = pdu->N_bytes;
  header.teid         = rnti_bearers[rnti].teids_out[lcid];

  struct sockaddr_in servaddr;
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl(rnti_bearers[rnti].spgw_addrs[lcid]);
  servaddr.sin_port        = htons(GTPU_PORT);

  if (!gtpu_write_header(&header, pdu.get(), gtpu_log)) {
    gtpu_log->error("Error writing GTP-U Header. Flags 0x%x, Message Type 0x%x\n", header.flags, header.message_type);
    return;
  }
  if (sendto(fd, pdu->msg, pdu->N_bytes, MSG_EOR, (struct sockaddr*)&servaddr, sizeof(struct sockaddr_in)) < 0) {
    perror("sendto");
  }
}

/* Warning: This function is called before calling gtpu::init() during MCCH initialization.
 * If access to any element created in init (such as gtpu_log) is required, it must be considered
 * the case of it being NULL.
 */
void gtpu::add_bearer(uint16_t rnti, uint32_t lcid, uint32_t addr, uint32_t teid_out, uint32_t* teid_in)
{
  // Allocate a TEID for the incoming tunnel
  rntilcid_to_teidin(rnti, lcid, teid_in);
  if (gtpu_log) {
    gtpu_log->info("Adding bearer for rnti: 0x%x, lcid: %d, addr: 0x%x, teid_out: 0x%x, teid_in: 0x%x\n",
                   rnti,
                   lcid,
                   addr,
                   teid_out,
                   *teid_in);
  }

  // Initialize maps if it's a new RNTI
  if (rnti_bearers.count(rnti) == 0) {
    for (int i = 0; i < SRSENB_N_RADIO_BEARERS; i++) {
      rnti_bearers[rnti].teids_in[i]   = 0;
      rnti_bearers[rnti].teids_out[i]  = 0;
      rnti_bearers[rnti].spgw_addrs[i] = 0;
    }
  }

  rnti_bearers[rnti].teids_in[lcid]   = *teid_in;
  rnti_bearers[rnti].teids_out[lcid]  = teid_out;
  rnti_bearers[rnti].spgw_addrs[lcid] = addr;
}

void gtpu::rem_bearer(uint16_t rnti, uint32_t lcid)
{
  gtpu_log->info("Removing bearer for rnti: 0x%x, lcid: %d\n", rnti, lcid);

  rnti_bearers[rnti].teids_in[lcid]  = 0;
  rnti_bearers[rnti].teids_out[lcid] = 0;

  // Remove RNTI if all bearers are removed
  bool rem = true;
  for (int i = 0; i < SRSENB_N_RADIO_BEARERS; i++) {
    if (rnti_bearers[rnti].teids_in[i] != 0) {
      rem = false;
    }
  }
  if (rem) {
    rnti_bearers.erase(rnti);
  }
}

void gtpu::rem_user(uint16_t rnti)
{
  rnti_bearers.erase(rnti);
}

void gtpu::handle_gtpu_s1u_rx_packet(srslte::unique_byte_buffer_t pdu, const sockaddr_in& addr)
{
  gtpu_log->debug("Received %d bytes from S1-U interface\n", pdu->N_bytes);

  gtpu_header_t header;
  if (not gtpu_read_header(pdu.get(), &header, gtpu_log)) {
    return;
  }

  switch (header.message_type) {
    case GTPU_MSG_ECHO_REQUEST:
      // Echo request - send response
      echo_response(addr.sin_addr.s_addr, addr.sin_port, header.seq_number);
      break;
    case GTPU_MSG_DATA_PDU: {
      uint16_t rnti = 0;
      uint16_t lcid = 0;
      teidin_to_rntilcid(header.teid, &rnti, &lcid);

      bool user_exists = (rnti_bearers.count(rnti) > 0);

      if (not user_exists) {
        gtpu_log->error("Unrecognized RNTI for DL PDU: 0x%x - dropping packet\n", rnti);
        return;
      }

      if (lcid < SRSENB_N_SRB || lcid >= SRSENB_N_RADIO_BEARERS) {
        gtpu_log->error("Invalid LCID for DL PDU: %d - dropping packet\n", lcid);
        return;
      }

      gtpu_log->info_hex(
          pdu->msg, pdu->N_bytes, "RX GTPU PDU rnti=0x%x, lcid=%d, n_bytes=%d", rnti, lcid, pdu->N_bytes);

      pdcp->write_sdu(rnti, lcid, std::move(pdu));
    } break;
    default:
      break;
  }
}

void gtpu::handle_gtpu_m1u_rx_packet(srslte::unique_byte_buffer_t pdu, const sockaddr_in& addr)
{
  m1u.handle_rx_packet(std::move(pdu), addr);
}

void gtpu::echo_response(in_addr_t addr, in_port_t port, uint16_t seq)
{
  gtpu_log->info("TX GTPU Echo Response, Seq: %d\n", seq);

  gtpu_header_t        header;
  unique_byte_buffer_t pdu = allocate_unique_buffer(*pool);

  // header
  header.flags             = GTPU_FLAGS_VERSION_V1 | GTPU_FLAGS_GTP_PROTOCOL | GTPU_FLAGS_SEQUENCE;
  header.message_type      = GTPU_MSG_ECHO_RESPONSE;
  header.teid              = 0;
  header.length            = 4;
  header.seq_number        = seq;
  header.n_pdu             = 0;
  header.next_ext_hdr_type = 0;

  gtpu_write_header(&header, pdu.get(), gtpu_log);

  struct sockaddr_in servaddr;
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = addr;
  servaddr.sin_port        = port;

  sendto(fd, pdu->msg, 12, MSG_EOR, (struct sockaddr*)&servaddr, sizeof(struct sockaddr_in));
}

/****************************************************************************
 * TEID to RNIT/LCID helper functions
 ***************************************************************************/
void gtpu::teidin_to_rntilcid(uint32_t teidin, uint16_t* rnti, uint16_t* lcid)
{
  *lcid = teidin & 0xFFFF;
  *rnti = (teidin >> 16) & 0xFFFF;
}

void gtpu::rntilcid_to_teidin(uint16_t rnti, uint16_t lcid, uint32_t* teidin)
{
  *teidin = (rnti << 16) | lcid;
}

/****************************************************************************
 * Class to handle MCH packet handling
 ***************************************************************************/

gtpu::m1u_handler::~m1u_handler()
{
  if (initiated) {
    close(m1u_sd);
    initiated = false;
  }
}

bool gtpu::m1u_handler::init(std::string m1u_multiaddr_, std::string m1u_if_addr_, int sync_sequence_duration_, int sync_sequence_packets_)
{
  m1u_multiaddr = std::move(m1u_multiaddr_);
  m1u_if_addr   = std::move(m1u_if_addr_);
  pdcp          = parent->pdcp;
  gtpu_log      = parent->gtpu_log;
  queue = sync_queue<srslte::sync_packet_t, srslte::sync_header_type0_t>(pdcp, 4, sync_sequence_duration_, sync_sequence_packets_);

  // Set up sink socket
  struct sockaddr_in bindaddr = {};
  m1u_sd                      = socket(AF_INET, SOCK_DGRAM, 0);
  if (m1u_sd < 0) {
    gtpu_log->error("Failed to create M1-U sink socket\n");
    return false;
  }

  /* Bind socket */
  bindaddr.sin_family      = AF_INET;
  bindaddr.sin_addr.s_addr = htonl(INADDR_ANY); // Multicast sockets require bind to INADDR_ANY
  bindaddr.sin_port        = htons(GTPU_PORT + 1);

  if (bind(m1u_sd, (struct sockaddr*)&bindaddr, sizeof(bindaddr)) < 0) {
    gtpu_log->error("Failed to bind multicast socket\n");
    return false;
  }

  /* Send an ADD MEMBERSHIP message via setsockopt */
  struct ip_mreq mreq {
  };
  mreq.imr_multiaddr.s_addr = inet_addr(m1u_multiaddr.c_str()); // Multicast address of the service
  mreq.imr_interface.s_addr = inet_addr(m1u_if_addr.c_str());   // Address of the IF the socket will listen to.
  if (setsockopt(m1u_sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
    gtpu_log->error("Register musticast group for M1-U\n");
    gtpu_log->error("M1-U infterface IP: %s, M1-U Multicast Address %s\n", m1u_if_addr.c_str(), m1u_multiaddr.c_str());
    return false;
  }
  gtpu_log->info("M1-U initialized\n");

  initiated    = true;
  lcid_counter = 1;

  // Register socket in stack rx sockets thread
  parent->stack->add_gtpu_m1u_socket_handler(m1u_sd);
  
  // Init MBMS SYNC protocol consumer
  if(!init_mbms_sync()){
    gtpu_log->error("Failed to init MBMS synchronisation protocol consumer\n");
  }

  return true;
}

void gtpu::m1u_handler::handle_rx_packet(srslte::unique_byte_buffer_t pdu, const sockaddr_in& addr)
{
  gtpu_log->debug("Received %d bytes from M1-U interface\n", pdu->N_bytes);

  // For hex dumps set gtpu_hex_limit = 1600
  //gtpu_log->debug_hex(pdu->msg, pdu->N_bytes, "GTPU hex packet");

  gtpu_header_t header;
  gtpu_read_header(pdu.get(), &header, gtpu_log);

  sync_common_header_type_t sync_common_header;

  sync_header_type0_t sync_header_type0;
  sync_header_type1_t sync_header_type1;
  sync_header_type3_t sync_header_type3;

  // Avoiding switch variable initialization problems
  uint32_t period_sec, period_nsec;
  uint8_t* ptr;
  //sync_data_packets[counter] = {};
  switch(sync_read_header(pdu.get(), &sync_common_header, gtpu_log)){

    case SYNC_PDU_TYPE_0:
      //sync_read_header_type0(pdu.get(), &sync_header0, gtpu_log);
      gtpu_log->console("Received SYNC PDU TYPE 0\n");
      sync_read_header_type0(pdu.get(), &sync_header_type0, gtpu_log);
      sync_info_packets[counter_info] = sync_header_type0;
      queue.push_info(sync_info_packets[counter_info]);
      counter_info++;
      break;
    
    case SYNC_PDU_TYPE_1:
      //sync_read_header_type1(pdu.get(), &sync_header1, gtpu_log);
      sync_read_header_type1(pdu.get(), &sync_header_type1, gtpu_log);
      sync_data_packets[counter].header = sync_header_type1;
      sync_data_packets[counter].payload = std::move(pdu);
      queue.push_data(sync_data_packets[counter]);
      counter++;
      break;
    
    case SYNC_PDU_TYPE_3:
      // Currently used to carry sync_period
      gtpu_log->console("Received SYNC PDU TYPE 3 SYNC period setter\n");
      sync_read_header_type3(pdu.get(), &sync_header_type3, gtpu_log);
      ptr = (uint8_t*) pdu.get()->msg;
      // fix index of ptr
      period_sec = (uint32_t)ptr[19] << 24 | (uint32_t)ptr[20] << 16 | (uint32_t)ptr[21] << 8 | (uint32_t)ptr[22];
      period_nsec = (uint32_t)ptr[23] << 24 | (uint32_t)ptr[24] << 16 | (uint32_t)ptr[25] << 8 | (uint32_t)ptr[26];
      //gtpu_log->console("Sync period seconds: %" PRIu32 " and nanoseconds: %" PRIu32 "\n", period_sec, period_nsec);
      queue.set_sync_period(period_sec, period_nsec);
      break;
    
    default:
      gtpu_log->debug("Unsupported SYNC PDU TYPE\n");
      break;
  }

  if(counter==339999){ // Last packet in the buffer
    counter = 0;
  } else if(counter_info==169999){ // Last packet in the buffer
    counter_info = 0;
  }

  //pdcp->write_sdu(SRSLTE_MRNTI, lcid_counter, std::move(pdu));
}

bool gtpu::m1u_handler::init_mbms_sync(){

  // Effort to give priorities to this thread
  pthread_t sync_consumer;
  pthread_attr_t     attr;
  struct sched_param param;

  int prio_offset = 0; //?
  param.sched_priority = 50 - prio_offset;

  if(pthread_attr_init(&attr)){
    gtpu_log->error("pthread_attr_init at init_mbms_sync\n");
  }
  if(pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)){
    gtpu_log->error("pthread_attr_setinheritsched at init_mbms_sync\n");
  }
  if(pthread_attr_setschedpolicy(&attr, SCHED_FIFO)){
    gtpu_log->error("pthread_attr_setschedpolicy at init_mbms_sync\n");
  }
  if(pthread_attr_setschedparam(&attr, &param)){
    gtpu_log->error("pthread_attr_setschedparam, not enough privileges to set Scheduling priority at init_mbms_sync\n");
  }

  int err = pthread_create(&sync_consumer, &attr, &sync_queue<srslte::sync_packet_t,srslte::sync_header_type0_t>::sync_consumer, (void*) &queue);

  if(err){
    // Join failed thread to avoid memory leak
    pthread_join(sync_consumer, NULL);
    return false;
  } else {
    return true;
  }
}
} // namespace srsenb
