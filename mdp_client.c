/*
 Copyright (C) 2010-2012 Paul Gardner-Stephen, Serval Project.
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <sys/stat.h>
#include "serval.h"
#include "conf.h"
#include "log.h"
#include "str.h"
#include "strbuf.h"
#include "strbuf_helpers.h"
#include "overlay_buffer.h"
#include "overlay_address.h"
#include "overlay_packet.h"
#include "mdp_client.h"

int mdp_client_socket = -1;

int overlay_mdp_send(overlay_mdp_frame *mdp, int flags, int timeout_ms)
{
  if (mdp_client_socket == -1 && overlay_mdp_client_init() == -1)
    return -1;
  // Minimise frame length to save work and prevent accidental disclosure of memory contents.
  int len = overlay_mdp_relevant_bytes(mdp);
  if (len < 0)
    return WHY("MDP frame invalid (could not compute length)");
  /* Construct name of socket to send to. */
  struct sockaddr_un addr;
  socklen_t addrlen;
  if (socket_setname(&addr, &addrlen, "mdp.socket") == -1)
    return -1;
  // Send to that socket
  set_nonblock(mdp_client_socket);
  int result = sendto(mdp_client_socket, mdp, len, 0, (struct sockaddr *)&addr, addrlen);
  set_block(mdp_client_socket);
  if (result == -1) {
    mdp->packetTypeAndFlags=MDP_ERROR;
    mdp->error.error=1;
    snprintf(mdp->error.message,128,"Error sending frame to MDP server.");
    return WHY_perror("sendto(f)");
  } else {
    if (!(flags&MDP_AWAITREPLY)) {       
      return 0;
    }
  }
  
  int port=0;
  if ((mdp->packetTypeAndFlags&MDP_TYPE_MASK) == MDP_TX)
      port = mdp->out.src.port;
      
  time_ms_t started = gettime_ms();
  while(timeout_ms>=0 && overlay_mdp_client_poll(timeout_ms)>0){
    int ttl=-1;
    if (!overlay_mdp_recv(mdp, port, &ttl)) {
      /* If all is well, examine result and return error code provided */
      if ((mdp->packetTypeAndFlags&MDP_TYPE_MASK)==MDP_ERROR)
	return mdp->error.error;
      else
      /* Something other than an error has been returned */
	return 0;
    }
    
    // work out how much longer we can wait for a valid response
    time_ms_t now = gettime_ms();
    timeout_ms -= (now - started);
  }
  
  /* Timeout */
  mdp->packetTypeAndFlags=MDP_ERROR;
  mdp->error.error=1;
  snprintf(mdp->error.message,128,"Timeout waiting for reply to MDP packet (packet was successfully sent).");    
  return -1; /* WHY("Timeout waiting for server response"); */
}

int overlay_mdp_client_init()
{
  if (mdp_client_socket == -1) {
    /* Create local per-client socket to MDP server (connection is always local) */
    struct sockaddr_un addr;
    socklen_t addrlen;
    uint32_t random_value;
    if (urandombytes((unsigned char *)&random_value, sizeof random_value) == -1)
      return WHY("urandombytes() failed");
    if (socket_setname(&addr, &addrlen, "mdp.client.%u.%08lx.socket", getpid(), (unsigned long)random_value) == -1)
      return -1;
    if ((mdp_client_socket = esocket(AF_UNIX, SOCK_DGRAM, 0)) == -1)
      return -1;
    if (socket_bind(mdp_client_socket, (struct sockaddr *)&addr, addrlen) == -1) {
      close(mdp_client_socket);
      mdp_client_socket = -1;
      return -1;
    }
    socket_set_rcvbufsize(mdp_client_socket, 128 * 1024);
  }
  return 0;
}

int overlay_mdp_client_done()
{
  if (mdp_client_socket != -1) {
    /* Tell MDP server to release all our bindings */
    overlay_mdp_frame mdp;
    mdp.packetTypeAndFlags = MDP_GOODBYE;
    overlay_mdp_send(&mdp, 0, 0);
    // get the socket name and unlink it from the filesystem if not abstract
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof addr;
    if (getsockname(mdp_client_socket, (struct sockaddr *)&addr, &addrlen))
      WHYF_perror("getsockname(%d)", mdp_client_socket);
    else if (addrlen > sizeof addr.sun_family && addrlen <= sizeof addr && addr.sun_path[0] != '\0') {
      if (unlink(addr.sun_path) == -1)
	WARNF_perror("unlink(%s)", alloca_str_toprint(addr.sun_path));
    }
    close(mdp_client_socket);
    mdp_client_socket = -1;
  }
  return 0;
}

int overlay_mdp_client_poll(time_ms_t timeout_ms)
{
  fd_set r;
  int ret;
  FD_ZERO(&r);
  FD_SET(mdp_client_socket,&r);
  if (timeout_ms<0) timeout_ms=0;
  
  struct timeval tv;
  
  if (timeout_ms>=0) {
    tv.tv_sec=timeout_ms/1000;
    tv.tv_usec=(timeout_ms%1000)*1000;
    ret=select(mdp_client_socket+1,&r,NULL,&r,&tv);
  }
  else
    ret=select(mdp_client_socket+1,&r,NULL,&r,NULL);
  return ret;
}

int overlay_mdp_recv(overlay_mdp_frame *mdp, int port, int *ttl) 
{
  /* Construct name of socket to receive from. */
  struct sockaddr_un mdp_addr;
  socklen_t mdp_addrlen;
  if (socket_setname(&mdp_addr, &mdp_addrlen, "mdp.socket") == -1)
    return -1;
  
  /* Check if reply available */
  struct sockaddr_un recvaddr;
  socklen_t recvaddrlen = sizeof recvaddr;
  ssize_t len;
  mdp->packetTypeAndFlags = 0;
  set_nonblock(mdp_client_socket);
  len = recvwithttl(mdp_client_socket, (unsigned char *)mdp, sizeof(overlay_mdp_frame), ttl, (struct sockaddr *)&recvaddr, &recvaddrlen);
  set_block(mdp_client_socket);
  if (len <= 0)
    return -1; // no packet received

  // If the received address overflowed the buffer, then it cannot have come from the server, whose
  // address always fits within a struct sockaddr_un.
  if (recvaddrlen > sizeof recvaddr)
    return WHY("reply did not come from server: address overrun");

  if (cmp_sockaddr((struct sockaddr *)&recvaddr, recvaddrlen, (struct sockaddr *)&mdp_addr, mdp_addrlen) != 0)
    return WHYF("reply did not come from server: %s", alloca_sockaddr(&recvaddr, recvaddrlen));
  
  // silently drop incoming packets for the wrong port number
  if (port>0 && port != mdp->in.dst.port){
    WARNF("Ignoring packet for port %d",mdp->in.dst.port);
    return -1;
  }

  int expected_len = overlay_mdp_relevant_bytes(mdp);
  if (len < expected_len)
    return WHYF("Expected packet length of %d, received only %lld bytes", expected_len, (long long) len);
  
  /* Valid packet received */
  return 0;
}

// send a request to servald deamon to add a port binding
int overlay_mdp_bind(const sid_t *localaddr, int port) 
{
  overlay_mdp_frame mdp;
  mdp.packetTypeAndFlags=MDP_BIND|MDP_FORCE;
  bcopy(localaddr->binary, mdp.bind.sid, SID_SIZE);
  mdp.bind.port=port;
  int result=overlay_mdp_send(&mdp,MDP_AWAITREPLY,5000);
  if (result) {
    if (mdp.packetTypeAndFlags==MDP_ERROR)
      WHYF("Could not bind to MDP port %d: error=%d, message='%s'",
	   port,mdp.error.error,mdp.error.message);
    else
      WHYF("Could not bind to MDP port %d (no reason given)",port);
    return -1;
  }
  return 0;
}

int overlay_mdp_getmyaddr(unsigned index, sid_t *sid)
{
  overlay_mdp_frame a;
  memset(&a, 0, sizeof(a));
  
  a.packetTypeAndFlags=MDP_GETADDRS;
  a.addrlist.mode = MDP_ADDRLIST_MODE_SELF;
  a.addrlist.first_sid=index;
  a.addrlist.last_sid=OVERLAY_MDP_ADDRLIST_MAX_SID_COUNT;
  a.addrlist.frame_sid_count=MDP_MAX_SID_REQUEST;
  int result=overlay_mdp_send(&a,MDP_AWAITREPLY,5000);
  if (result) {
    if (a.packetTypeAndFlags == MDP_ERROR)
      DEBUGF("MDP Server error #%d: '%s'", a.error.error, a.error.message);
    return WHY("Failed to get local address list");
  }
  if ((a.packetTypeAndFlags&MDP_TYPE_MASK)!=MDP_ADDRLIST)
    return WHY("MDP Server returned something other than an address list");
  if (0) DEBUGF("local addr 0 = %s",alloca_tohex_sid(a.addrlist.sids[0]));
  bcopy(&a.addrlist.sids[0][0], sid->binary, sizeof sid->binary);
  return 0;
}

int overlay_mdp_relevant_bytes(overlay_mdp_frame *mdp) 
{
  int len;
  switch(mdp->packetTypeAndFlags&MDP_TYPE_MASK)
  {
    case MDP_ROUTING_TABLE:
    case MDP_GOODBYE:
      /* no arguments for saying goodbye */
      len=&mdp->raw[0]-(char *)mdp;
      break;
    case MDP_ADDRLIST: 
      len=(&mdp->addrlist.sids[0][0]-(unsigned char *)mdp) + mdp->addrlist.frame_sid_count*SID_SIZE;
      break;
    case MDP_GETADDRS: 
      len=&mdp->addrlist.sids[0][0]-(unsigned char *)mdp;
      break;
    case MDP_TX: 
      len=(&mdp->out.payload[0]-(unsigned char *)mdp) + mdp->out.payload_length; 
      break;
    case MDP_BIND:
      len=(&mdp->raw[0] - (char *)mdp) + sizeof(sockaddr_mdp);
      break;
    case MDP_SCAN:
      len=(&mdp->raw[0] - (char *)mdp) + sizeof(struct overlay_mdp_scan);
      break;
    case MDP_ERROR: 
      /* This formulation is used so that we don't copy any bytes after the
       end of the string, to avoid information leaks */
      len=(&mdp->error.message[0]-(char *)mdp) + strlen(mdp->error.message)+1;      
      if (mdp->error.error) INFOF("mdp return/error code: %d:%s",mdp->error.error,mdp->error.message);
      break;
    default:
      return WHY("Illegal MDP frame type.");
  }
  return len;
}
