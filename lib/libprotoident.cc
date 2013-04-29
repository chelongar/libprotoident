/* 
 * This file is part of libprotoident
 *
 * Copyright (c) 2011 The University of Waikato, Hamilton, New Zealand.
 * Author: Shane Alcock
 *
 * With contributions from:
 * 	Aaron Murrihy
 * 	Donald Neal
 *
 * All rights reserved.
 *
 * This code has been developed by the University of Waikato WAND 
 * research group. For further information please see http://www.wand.net.nz/
 *
 * libprotoident is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * libprotoident is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libprotoident; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: libprotoident.cc 68 2011-03-10 22:35:25Z salcock $
 */

#define __STDC_FORMAT_MACROS
#define __STDC_LIMIT_MACROS

#include <stdio.h>
#include <assert.h>
#include <libtrace.h>
#include <inttypes.h>
#include <sys/types.h>
#include <stdint.h>

#include "libprotoident.h"
#include "proto_manager.h"


bool init_called = false;
LPIModuleMap TCP_protocols;
LPIModuleMap UDP_protocols;

lpi_module_t *lpi_icmp = NULL;
lpi_module_t *lpi_unsupported = NULL;
lpi_module_t *lpi_unknown_tcp = NULL;
lpi_module_t *lpi_unknown_udp = NULL;

static LPINameMap lpi_names;

static int seq_cmp (uint32_t seq_a, uint32_t seq_b) {

        if (seq_a == seq_b) return 0;


        if (seq_a > seq_b)
                return (int)(seq_a - seq_b);
        else
                /* WRAPPING */
                return (int)(UINT32_MAX - ((seq_b - seq_a) - 1));

}

int lpi_init_library() {

	if (register_tcp_protocols(&TCP_protocols) == -1) 
		return -1;
	
	if (register_udp_protocols(&UDP_protocols) == -1) 
		return -1;

	init_other_protocols(&lpi_names);

	register_names(&TCP_protocols, &lpi_names);
	register_names(&UDP_protocols, &lpi_names);
	
	init_called = true;

	if (TCP_protocols.empty() && UDP_protocols.empty()) {
		fprintf(stderr, "WARNING: No protocol modules loaded\n");
		return -1;
	}


	return 0;

}

void lpi_free_library() {

	free_protocols(&TCP_protocols);
	free_protocols(&UDP_protocols);

	init_called = false;
}

void lpi_init_data(lpi_data_t *data) {

	data->payload[0] = 0;
	data->payload[1] = 0;
	data->seqno[0] = 0;
	data->seqno[1] = 0;
	data->observed[0] = 0;
	data->observed[1] = 0;
	data->server_port = 0;
	data->client_port = 0;
	data->trans_proto = 0;
	data->payload_len[0] = 0;
	data->payload_len[1] = 0;
	data->ips[0] = 0;
	data->ips[1] = 0;

}

static int update_tcp_flow(lpi_data_t *data, libtrace_tcp_t *tcp, uint8_t dir,
		uint32_t rem) {
	uint32_t seq = 0;

	if (rem < sizeof(libtrace_tcp_t))
		return 0;
	if (tcp->rst)
		return 0;
	
	if (data->server_port == 0) {
		data->server_port = ntohs(tcp->dest);
		data->client_port = ntohs(tcp->source);
	}

	seq = ntohl(tcp->seq);

	if (tcp->syn && data->payload_len[dir] == 0) {
		data->seqno[dir] = seq + 1;
	}

	if (seq_cmp(seq, data->seqno[dir]) > 0)
		return 0;
	//data->seqno[dir] = seq;

	return 1;
}

static int update_udp_flow(lpi_data_t *data, libtrace_udp_t *udp,
		uint32_t rem) {

	if (rem < sizeof(libtrace_udp_t))
		return 0;
	
	if (data->server_port == 0) {
		data->server_port = ntohs(udp->dest);
		data->client_port = ntohs(udp->source);
	}

	return 1;
}

int lpi_update_data(libtrace_packet_t *packet, lpi_data_t *data, uint8_t dir) {

	char *payload = NULL;
	uint32_t psize = 0;
	uint32_t rem = 0;
	uint8_t proto = 0;
	void *transport;
	uint32_t four_bytes;
	libtrace_ip_t *ip = NULL;

	//tcp = trace_get_tcp(packet);
	psize = trace_get_payload_length(packet);

	/* Don't bother if we've observed 32k of data - the first packet must
	 * surely been within that. This helps us avoid issues with sequence
	 * number wrapping when doing the reordering check below */
	if (data->observed[dir] > 32 * 1024)
		return 0;
	
	data->observed[dir] += psize;
	
	/* If we're TCP, we have to wait to check that we haven't been
	 * reordered */
	if (data->trans_proto != 6 && data->payload_len[dir] != 0)
		return 0;
	
	transport = trace_get_transport(packet, &proto, &rem);
	if (data->trans_proto == 0)
		data->trans_proto = proto;
	
	if (transport == NULL || rem == 0)
		return 0;		

	if (proto == 6) {
		if (update_tcp_flow(data, (libtrace_tcp_t *)transport, dir, rem) == 0) 
			return 0;
		payload = (char *)trace_get_payload_from_tcp(
				(libtrace_tcp_t *)transport, &rem);
	} 

	if (proto == 17) {
		if (update_udp_flow(data, (libtrace_udp_t *)transport, rem) == 0)
			return 0;
		payload = (char *)trace_get_payload_from_udp(
				(libtrace_udp_t *)transport, &rem);
	}

	ip = trace_get_ip(packet);
	
	if (payload == NULL)
		return 0;
	if (psize <= 0)
		return 0;

	four_bytes = ntohl((*(uint32_t *)payload));
	
	if (psize < 4) {
		four_bytes = four_bytes >> (8 * (4 - psize));		
		four_bytes = four_bytes << (8 * (4 - psize));		
	}

	data->payload[dir] = htonl(four_bytes);
	data->payload_len[dir] = psize;

	if (ip != NULL && data->ips[0] == 0) {
		if (dir == 0) {
			data->ips[0] = ip->ip_src.s_addr;
			data->ips[1] = ip->ip_dst.s_addr;
		} else {
			data->ips[1] = ip->ip_src.s_addr;
			data->ips[0] = ip->ip_dst.s_addr;
		}
	}

	return 1;

}

static lpi_module_t *test_protocol_list(LPIModuleList *ml, lpi_data_t *data) {

	LPIModuleList::iterator l_it;
	
	/*
	 * Ultimately, I want to spawn up to MAXTHREADS threads for rule
	 * matching so we can parallelise this process. For now, though, I'll
	 * just naively loop through the list.
	 */

	for (l_it = ml->begin(); l_it != ml->end(); l_it ++) {
		lpi_module_t *module = *l_it;

		/* To save time, I'm going to break on the first successful
		 * match. A threaded version would wait for all the modules
		 * to run, storing all successful results in a list of some
		 * sort and selecting an appropriate result from there.
		 */

		if (module->lpi_callback(data, module)) 
			return module;
		
	}

	return NULL;
}

static lpi_module_t *guess_protocol(LPIModuleMap *modmap, lpi_data_t *data) {

	lpi_module_t *proto = NULL;

	LPIModuleMap::iterator m_it;

	/* Deal with each priority in turn - want to match higher priority
	 * rules first. 
	 */
	
	for (m_it = modmap->begin(); m_it != modmap->end(); m_it ++) {
		LPIModuleList *ml = m_it->second;
		//fprintf(stderr, "Testing protocols at priority level %u\n",
		//		m_it->first);
		
		proto = test_protocol_list(ml, data);

		if (proto != NULL)
			break;
	}

	return proto;

}

lpi_module_t *lpi_guess_protocol(lpi_data_t *data) {

	lpi_module_t *p = NULL;

	if (!init_called) {
		fprintf(stderr, "lpi_init_library was never called - cannot guess the protocol\n");
		return NULL;
	}

	switch(data->trans_proto) {
		case TRACE_IPPROTO_ICMP:
			return lpi_icmp;
		case TRACE_IPPROTO_TCP:
			p = guess_protocol(&TCP_protocols, data);
			if (p == NULL)
				p = lpi_unknown_tcp;
			return p;

		case TRACE_IPPROTO_UDP:
			p = guess_protocol(&UDP_protocols, data);
			if (p == NULL)
				p = lpi_unknown_udp;
			return p;
		default:
			return lpi_unsupported;
	}


	return p;
}
	
lpi_category_t lpi_categorise(lpi_module_t *module) {

	if (module == NULL)
		return LPI_CATEGORY_NO_CATEGORY;

	return module->category;

	switch(module->protocol) {
		case LPI_PROTO_UNSUPPORTED:
			return LPI_CATEGORY_UNSUPPORTED;
		
		case LPI_PROTO_INVALID:
		case LPI_PROTO_INVALID_BT:
		case LPI_PROTO_WEB_JUNK:
			return LPI_CATEGORY_MIXED;

		case LPI_PROTO_UNKNOWN:
		case LPI_PROTO_UDP:
			return LPI_CATEGORY_UNKNOWN;
		
		case LPI_PROTO_NO_PAYLOAD:
			return LPI_CATEGORY_NOPAYLOAD;

		case LPI_PROTO_ICMP:
			return LPI_CATEGORY_ICMP;

		case LPI_PROTO_HTTP:
		case LPI_PROTO_HTTPS:
		case LPI_PROTO_HTTP_BADPORT:
		case LPI_PROTO_INVALID_HTTP:
			return LPI_CATEGORY_WEB;

		case LPI_PROTO_SMTP:
		case LPI_PROTO_POP3:
		case LPI_PROTO_IMAP:
		case LPI_PROTO_IMAPS:
		case LPI_PROTO_INVALID_SMTP:
			return LPI_CATEGORY_MAIL;

		case LPI_PROTO_SSL:
			return LPI_CATEGORY_ENCRYPT;
		
		case LPI_PROTO_UDP_ISAKMP:
                case LPI_PROTO_KMS:
		case LPI_PROTO_UDP_CP_RDP:
			return LPI_CATEGORY_KEY_EXCHANGE;
		
		case LPI_PROTO_UDP_TRACEROUTE:
		case LPI_PROTO_UDP_SNMP:
		case LPI_PROTO_UDP_LINKPROOF:
               		return LPI_CATEGORY_MONITORING;

                case LPI_PROTO_RPC_SCAN:
		case LPI_PROTO_UDP_OPASERV:
		case LPI_PROTO_UDP_WORM_22105:
		case LPI_PROTO_UDP_SQLEXP:
                case LPI_PROTO_MITGLIEDER:
                case LPI_PROTO_UDP_WIN_MESSAGE:
		case LPI_PROTO_UDP_STORM_WORM:
	      		return LPI_CATEGORY_MALWARE;
	
		case LPI_PROTO_ETRUST:
		case LPI_PROTO_UDP_BACKWEB:
		case LPI_PROTO_UDP_NORTON:
		case LPI_PROTO_UDP_FORTINET:
			return LPI_CATEGORY_SECURITY;

		case LPI_PROTO_SVN:
			return LPI_CATEGORY_RCS;

		case LPI_PROTO_NNTP:
                        return LPI_CATEGORY_NEWS;

                case LPI_PROTO_SIP:
                case LPI_PROTO_UDP_SIP:
                case LPI_PROTO_UDP_RTP:
		case LPI_PROTO_UDP_SKYPE:
		case LPI_PROTO_UDP_VENTRILO:
		case LPI_PROTO_UDP_VIVOX:
		case LPI_PROTO_UDP_TEAMSPEAK:
		case LPI_PROTO_UDP_RTCP:
                        return LPI_CATEGORY_VOIP;
                
		case LPI_PROTO_HAMACHI:
                case LPI_PROTO_TOR:
                case LPI_PROTO_HTTP_TUNNEL:
		case LPI_PROTO_OPENVPN:
		case LPI_PROTO_UDP_IPV6:
		case LPI_PROTO_UDP_ESP:
		case LPI_PROTO_UDP_TEREDO:
		case LPI_PROTO_PPTP:
		case LPI_PROTO_UDP_CISCO_VPN:
		case LPI_PROTO_SOCKS5:
		case LPI_PROTO_SOCKS4:
			return LPI_CATEGORY_TUNNELLING;

		case LPI_PROTO_UDP_PYZOR:
		case LPI_PROTO_RAZOR:
		case LPI_PROTO_RBLS:
		case LPI_PROTO_UDP_SPAMFIGHTER:
			return LPI_CATEGORY_ANTISPAM;
		
		case LPI_PROTO_UDP_STUN:
			return LPI_CATEGORY_NAT;
		
                case LPI_PROTO_RTSP:
		case LPI_PROTO_RTMP:
		case LPI_PROTO_FLASH:
		case LPI_PROTO_SHOUTCAST:
		case LPI_PROTO_UDP_REAL:
		case LPI_PROTO_MMS:
			return LPI_CATEGORY_STREAMING;	
		
		case LPI_PROTO_DNS:
                case LPI_PROTO_ID:
                case LPI_PROTO_NETBIOS:
                case LPI_PROTO_UDP_DNS:
                case LPI_PROTO_UDP_DHCP:
                case LPI_PROTO_UDP_NTP:
		case LPI_PROTO_UDP_SLP:
		case LPI_PROTO_UDP_SSDP:
		case LPI_PROTO_UDP_NETBIOS:
		case LPI_PROTO_UDP_SERIALNUMBERD:	
			return LPI_CATEGORY_SERVICES;	
                
		case LPI_PROTO_TDS:
		case LPI_PROTO_POSTGRESQL:
                case LPI_PROTO_DXP:
                case LPI_PROTO_MYSQL:
		case LPI_PROTO_WEBLOGIC:
			return LPI_CATEGORY_DATABASES;
	
                case LPI_PROTO_SMB:
                case LPI_PROTO_FTP_CONTROL:
		case LPI_PROTO_FTP_DATA:
                case LPI_PROTO_MS_DS:
                case LPI_PROTO_RSYNC:
		case LPI_PROTO_HARVEYS:
		case LPI_PROTO_UDP_ORBIT:
		case LPI_PROTO_MSNC:
		case LPI_PROTO_AFP:
		case LPI_PROTO_UDP_TFTP:
			return LPI_CATEGORY_FILES;

		case LPI_PROTO_BITTORRENT:
		case LPI_PROTO_EMULE:
		case LPI_PROTO_GNUTELLA:
                case LPI_PROTO_ARES:
                case LPI_PROTO_NAPSTER:
                case LPI_PROTO_XUNLEI:
                case LPI_PROTO_BITEXT:
                case LPI_PROTO_AZUREUS:
		case LPI_PROTO_PANDO:
		case LPI_PROTO_P2P_HTTP:
		case LPI_PROTO_IMESH:
		case LPI_PROTO_UDP_MP2P:
		case LPI_PROTO_UDP_XFIRE_P2P:
		case LPI_PROTO_DC:
		case LPI_PROTO_UDP_FREECHAL:
		case LPI_PROTO_CLUBBOX:
		case LPI_PROTO_UDP_XUNLEI:
		case LPI_PROTO_WINMX:
		case LPI_PROTO_MP2P:
			return LPI_CATEGORY_P2P;
		
		case LPI_PROTO_UDP_PPLIVE:
		case LPI_PROTO_PDBOX:
		case LPI_PROTO_UDP_PPSTREAM:
		case LPI_PROTO_UDP_TVANTS:
		case LPI_PROTO_UDP_SOPCAST:
			return LPI_CATEGORY_P2PTV;
		
		case LPI_PROTO_NCSOFT:
                case LPI_PROTO_WARCRAFT3:
                case LPI_PROTO_BNCS:
                case LPI_PROTO_EYE:
                case LPI_PROTO_BLIZZARD:
		case LPI_PROTO_STEAM:
		case LPI_PROTO_TRACKMANIA:
		case LPI_PROTO_CONQUER:
		case LPI_PROTO_WOW:
		case LPI_PROTO_UDP_QUAKE:
                case LPI_PROTO_UDP_STEAM:
                case LPI_PROTO_UDP_STEAM_FRIENDS:
                case LPI_PROTO_UDP_GAMESPY:
                case LPI_PROTO_UDP_EYE:
                case LPI_PROTO_UDP_COD:
		case LPI_PROTO_UDP_SECONDLIFE:
		case LPI_PROTO_UDP_HL:
		case LPI_PROTO_UDP_XLSP:
		case LPI_PROTO_UDP_DEMONWARE:
		case LPI_PROTO_UDP_DIABLO2:
		case LPI_PROTO_UDP_PSN:
		case LPI_PROTO_UDP_STARCRAFT:
		case LPI_PROTO_UDP_THQ:
		case LPI_PROTO_UDP_ESO:
		case LPI_PROTO_UDP_NEWERTH:
		case LPI_PROTO_EA_GAMES:
		case LPI_PROTO_UDP_MTA:
		case LPI_PROTO_UDP_JEDI:
		case LPI_PROTO_UDP_MOH:
		case LPI_PROTO_UDP_TREMULOUS:
		case LPI_PROTO_ZYNGA:
		case LPI_PROTO_UDP_UNREAL:
		case LPI_PROTO_UDP_GARENA:
		case LPI_PROTO_COD_WAW:
		case LPI_PROTO_UDP_BATTLEFIELD:
			return LPI_CATEGORY_GAMING;

		case LPI_PROTO_IRC:
		case LPI_PROTO_MSN:
                case LPI_PROTO_YAHOO:
                case LPI_PROTO_ICQ:
                case LPI_PROTO_YAHOO_WEBCAM:
                case LPI_PROTO_GOKUCHAT:
                case LPI_PROTO_MSNV:
		case LPI_PROTO_YAHOO_ERROR:
                case LPI_PROTO_UDP_MSN_VIDEO:
		case LPI_PROTO_UDP_MSN_CACHE:
                case LPI_PROTO_MZINGA:
		case LPI_PROTO_UDP_QQ:
		case LPI_PROTO_UDP_IPMSG:
			return LPI_CATEGORY_CHAT;

		case LPI_PROTO_SSH:
                case LPI_PROTO_TELNET:
                case LPI_PROTO_RFB:
                case LPI_PROTO_RDP:
                case LPI_PROTO_ICA:
                case LPI_PROTO_NOTES_RPC:
			return LPI_CATEGORY_REMOTE;
		
                case LPI_PROTO_UDP_BTDHT:
                case LPI_PROTO_UDP_GNUTELLA:
                case LPI_PROTO_UDP_EMULE:
		case LPI_PROTO_UDP_IMESH:
		case LPI_PROTO_UDP_KADEMLIA:
		case LPI_PROTO_UDP_PANDO:
		case LPI_PROTO_UDP_GNUTELLA2:
		case LPI_PROTO_UDP_DC:
		case LPI_PROTO_UDP_EMULE_MYSTERY:
		case LPI_PROTO_UDP_KAZAA:
			return LPI_CATEGORY_P2P_STRUCTURE;
                
		case LPI_PROTO_TELECOMKEY:
		case LPI_PROTO_M4U:
			return LPI_CATEGORY_TELCO;

		case LPI_PROTO_TIP:
			return LPI_CATEGORY_ECOMMERCE;
	}
	if (module == NULL)
		return LPI_CATEGORY_NO_CATEGORY;
}

const char *lpi_print_category(lpi_category_t category) {

	switch(category) {
		case LPI_CATEGORY_WEB:
			return "Web";
		case LPI_CATEGORY_MAIL:
			return "Mail";
		case LPI_CATEGORY_CHAT:
			return "Chat";
		case LPI_CATEGORY_P2P:
			return "P2P";
		case LPI_CATEGORY_P2P_STRUCTURE:
			return "P2P_Structure";
		case LPI_CATEGORY_KEY_EXCHANGE:
			return "Key_Exchange";
		case LPI_CATEGORY_ECOMMERCE:
			return "ECommerce";
		case LPI_CATEGORY_GAMING:
			return "Gaming";
		case LPI_CATEGORY_ENCRYPT:
			return "Encryption";
		case LPI_CATEGORY_MONITORING:
			return "Measurement";
		case LPI_CATEGORY_NEWS:
			return "News";
		case LPI_CATEGORY_MALWARE:
			return "Malware";
		case LPI_CATEGORY_SECURITY:
			return "Security";
		case LPI_CATEGORY_ANTISPAM:
			return "Antispam";
		case LPI_CATEGORY_VOIP:
			return "VOIP";
		case LPI_CATEGORY_TUNNELLING:
			return "Tunnelling";
		case LPI_CATEGORY_NAT:
			return "NAT_Traversal";
		case LPI_CATEGORY_STREAMING:
			return "Streaming";
		case LPI_CATEGORY_SERVICES:
			return "Services";
		case LPI_CATEGORY_DATABASES:
			return "Databases";
		case LPI_CATEGORY_FILES:
			return "File_Transfer";
		case LPI_CATEGORY_REMOTE:
			return "Remote_Access";
		case LPI_CATEGORY_TELCO:
			return "Telco_Services";
		case LPI_CATEGORY_P2PTV:
			return "P2PTV";
		case LPI_CATEGORY_RCS:
			return "Revision_Control";
		case LPI_CATEGORY_ICMP:
			return "ICMP";
		case LPI_CATEGORY_MIXED:
			return "Mixed";
		case LPI_CATEGORY_NOPAYLOAD:
			return "No_Payload";
		case LPI_CATEGORY_UNKNOWN:
			return "Unknown";
		case LPI_CATEGORY_UNSUPPORTED:
			return "Unsupported";
		case LPI_CATEGORY_NO_CATEGORY:
			return "Uncategorised";
	}

	return "Invalid_Category";

}
			
const char *lpi_print(lpi_protocol_t proto) {

	LPINameMap::iterator it;

	it = lpi_names.find(proto);

	if (it == lpi_names.end()) {
		return "NULL";
	}	
	return (it->second);
	
}

#if 0
const char *lpi_print(lpi_module_t *module) {

	if (module == NULL)
		return "NULL";

	return module->name;
	
	switch(module->protocol) {
		case LPI_PROTO_INVALID:
			return "Invalid";
		case LPI_PROTO_UNKNOWN:
			return "Unknown_TCP";
		case LPI_PROTO_UDP:
			return "Unknown_UDP";
		case LPI_PROTO_NO_PAYLOAD:
			return "No_Payload";
		case LPI_PROTO_UNSUPPORTED:
			return "Unsupported";
		case LPI_PROTO_ICMP:
			return "ICMP";

		/* TCP Protocols */
		case LPI_PROTO_HTTP:
			return "HTTP";
		case LPI_PROTO_SMTP:
			return "SMTP";
		case LPI_PROTO_DC:
			return "DirectConnect";
		case LPI_PROTO_BITTORRENT:
			return "BitTorrent";
		case LPI_PROTO_EMULE:
			return "eMule";
		case LPI_PROTO_NCSOFT:
			return "NCSoft";
		case LPI_PROTO_IRC:
			return "IRC";
		case LPI_PROTO_SSH:
			return "SSH";
		case LPI_PROTO_GNUTELLA:
			return "Gnutella";
		case LPI_PROTO_POP3:
			return "POP3";
		case LPI_PROTO_RAZOR:
			return "Razor";
		case LPI_PROTO_HTTPS:
			return "HTTPS";
		case LPI_PROTO_SSL:
			return "SSL/TLS";
		case LPI_PROTO_MSN:
			return "MSN";
		case LPI_PROTO_DNS:
			return "DNS";
		case LPI_PROTO_IMAP:
			return "IMAP";
                case LPI_PROTO_RTSP:
                        return "RTSP";
                case LPI_PROTO_ID:
                        return "ID_Protocol";
                case LPI_PROTO_YAHOO:
                        return "Yahoo";
                case LPI_PROTO_ICQ:
                        return "ICQ";
                case LPI_PROTO_TELNET:
                        return "Telnet";
                case LPI_PROTO_RTMP:
                        return "RTMP";
                case LPI_PROTO_RDP:
                        return "RDP";
                case LPI_PROTO_TDS:
                        return "TDS";
                 case LPI_PROTO_RPC_SCAN:
                        return "RPC_Exploit";
                case LPI_PROTO_SMB:
                        return "SMB";
                case LPI_PROTO_WARCRAFT3:
                        return "Warcraft3";
                case LPI_PROTO_ETRUST:
                        return "eTrust_Update";
                case LPI_PROTO_FTP_CONTROL:
                        return "FTP_Control";
                case LPI_PROTO_FTP_DATA:
                        return "FTP_Data";
                case LPI_PROTO_EYE:
                        return "AllSeeingEye";
                case LPI_PROTO_ARES:
                        return "Ares";
                case LPI_PROTO_NNTP:
                        return "NNTP";
                case LPI_PROTO_NAPSTER:
                        return "Napster";
                case LPI_PROTO_BNCS:
                        return "Battle.net_Chat";
                case LPI_PROTO_RFB:
                        return "RFB";
                case LPI_PROTO_YAHOO_WEBCAM:
                        return "Yahoo_Webcam";
                case LPI_PROTO_ICA:
                        return "CitrixICA";
                case LPI_PROTO_NETBIOS:
                        return "Netbios_Session";
                case LPI_PROTO_KMS:
                        return "KMS";
                case LPI_PROTO_MS_DS:
                        return "Microsoft_DS";
                case LPI_PROTO_SIP:
                        return "SIP";
                case LPI_PROTO_MZINGA:
                        return "Mzinga";
                case LPI_PROTO_XUNLEI:
                        return "Xunlei";
                case LPI_PROTO_GOKUCHAT:
                        return "GokuChat";
                case LPI_PROTO_DXP:
                        return "Silverplatter_DXP";
                case LPI_PROTO_HAMACHI:
                        return "Hamachi";
                case LPI_PROTO_BLIZZARD:
                        return "Blizzard";
                case LPI_PROTO_MSNV:
                        return "MSN_Voice";
                case LPI_PROTO_BITEXT:
                        return "BitTorrent_Extension";
                case LPI_PROTO_MITGLIEDER:
                        return "Mitglieder_Trojan";
                case LPI_PROTO_TOR:
                        return "TOR";
                case LPI_PROTO_MYSQL:
                        return "MySQL";
                case LPI_PROTO_HTTP_TUNNEL:
                        return "HTTP_Tunnel";
                case LPI_PROTO_RSYNC:
                        return "Rsync";
                case LPI_PROTO_NOTES_RPC:
                        return "Lotus_Notes_RPC";
                case LPI_PROTO_AZUREUS:
                        return "Azureus";
		case LPI_PROTO_PANDO:
			return "Pando";
		case LPI_PROTO_FLASH:
			return "Flash_Player";
		case LPI_PROTO_STEAM:
			return "Steam_TCP";
		case LPI_PROTO_TRACKMANIA:
			return "Trackmania";
		case LPI_PROTO_CONQUER:
			return "ConquerOnline";
		case LPI_PROTO_TIP:
			return "TIP";
		case LPI_PROTO_P2P_HTTP:
			return "HTTP_P2P";
		case LPI_PROTO_HARVEYS:
			return "Harveys";
		case LPI_PROTO_SHOUTCAST:
			return "Shoutcast";
		case LPI_PROTO_HTTP_BADPORT:
			return "HTTP_443";
		case LPI_PROTO_POSTGRESQL:
			return "Postgresql";
		case LPI_PROTO_WOW:
			return "WorldOfWarcraft";
		case LPI_PROTO_M4U:
			return "Message4U";
		case LPI_PROTO_RBLS:
			return "RBL";
		case LPI_PROTO_OPENVPN:
			return "OpenVPN";
		case LPI_PROTO_TELECOMKEY:
			return "TelecomKey";
		case LPI_PROTO_IMAPS:
			return "IMAPS";
		case LPI_PROTO_MSNC:
			return "MSNC";
		case LPI_PROTO_YAHOO_ERROR:
			return "YahooError";
		case LPI_PROTO_IMESH:
			return "iMesh_TCP";
		case LPI_PROTO_PPTP:
			return "PPTP";
		case LPI_PROTO_AFP:
			return "AFP";
		case LPI_PROTO_PDBOX:
			return "PDBOX";
		case LPI_PROTO_EA_GAMES:
			return "EA_Games";
		case LPI_PROTO_ZYNGA:
			return "Zynga";
		case LPI_PROTO_CLUBBOX:
			return "Clubbox";
		case LPI_PROTO_WINMX:
			return "WinMX";
		case LPI_PROTO_INVALID_BT:
			return "Invalid_Bittorrent";
		case LPI_PROTO_WEBLOGIC:
			return "Weblogic";
		case LPI_PROTO_INVALID_HTTP:
			return "Invalid_HTTP";
		case LPI_PROTO_COD_WAW:
			return "Call_of_Duty";
		case LPI_PROTO_MP2P:
			return "MP2P_TCP";
		case LPI_PROTO_SVN:
			return "SVN";
		case LPI_PROTO_SOCKS5:
			return "SOCKS5";
		case LPI_PROTO_SOCKS4:
			return "SOCKS4";
		case LPI_PROTO_INVALID_SMTP:
			return "Invalid_SMTP";
		case LPI_PROTO_MMS:
			return "MMS";
		case LPI_PROTO_WEB_JUNK:
			return "Web_Junk";

                /* UDP Protocols */
                case LPI_PROTO_UDP_SIP:
                        return "SIP_UDP";
                case LPI_PROTO_UDP_BTDHT:
                        return "BitTorrent_UDP";
                case LPI_PROTO_UDP_GNUTELLA:
                        return "Gnutella_UDP";
                case LPI_PROTO_UDP_DNS:
                        return "DNS";
                case LPI_PROTO_UDP_DHCP:
                        return "DHCP";
                case LPI_PROTO_UDP_QUAKE:
                        return "Quake";
                case LPI_PROTO_UDP_STEAM:
                        return "Steam_UDP";
                case LPI_PROTO_UDP_STEAM_FRIENDS:
                        return "Steam_Friends";
                case LPI_PROTO_UDP_WIN_MESSAGE:
                        return "WindowsMessenger";
                case LPI_PROTO_UDP_GAMESPY:
                        return "Gamespy";
                case LPI_PROTO_UDP_EMULE:
                        return "eMule_UDP";
                case LPI_PROTO_UDP_EYE:
                        return "AllSeeingEye";
                case LPI_PROTO_UDP_RTP:
                        return "RTP";
                case LPI_PROTO_UDP_MSN_VIDEO:
                        return "MSN_Video";
                case LPI_PROTO_UDP_COD:
                        return "Call_of_Duty";
                case LPI_PROTO_UDP_NTP:
                        return "NTP";
		case LPI_PROTO_UDP_MP2P:
			return "MP2P_UDP";
		case LPI_PROTO_UDP_SPAMFIGHTER:
			return "SpamFighter";
		case LPI_PROTO_UDP_TRACEROUTE:
			return "Traceroute_UDP";
		case LPI_PROTO_UDP_SECONDLIFE:
			return "SecondLife";
		case LPI_PROTO_UDP_HL:
			return "HalfLife";
		case LPI_PROTO_UDP_XLSP:
			return "XboxLive_UDP";
		case LPI_PROTO_UDP_DEMONWARE:
			return "Demonware";
		case LPI_PROTO_UDP_IMESH:
			return "iMesh_UDP";
		case LPI_PROTO_UDP_OPASERV:
			return "Opaserv";
		case LPI_PROTO_UDP_STUN:
			return "STUN";
		case LPI_PROTO_UDP_SQLEXP:
			return "SQLExp";
		case LPI_PROTO_UDP_MSN_CACHE:
			return "MSN_Cache";
		case LPI_PROTO_UDP_DIABLO2:
			return "Diablo2";
		case LPI_PROTO_UDP_IPV6:
			return "UDP_IPv6";
		case LPI_PROTO_UDP_ORBIT:
			return "Orbit_UDP";
		case LPI_PROTO_UDP_TEREDO:
			return "Teredo";
		case LPI_PROTO_UDP_KADEMLIA:
			return "Kademlia";
		case LPI_PROTO_UDP_PANDO:
			return "Pando_UDP";
		case LPI_PROTO_UDP_ESP:
			return "ESP_UDP";
		case LPI_PROTO_UDP_PSN:
			return "PSN";
		case LPI_PROTO_UDP_REAL:
			return "RealPlayer";
		case LPI_PROTO_UDP_GNUTELLA2:
			return "Gnutella2_UDP";
		case LPI_PROTO_UDP_PYZOR:
			return "Pyzor_UDP";
		case LPI_PROTO_UDP_SKYPE:
			return "Skype_UDP";
		case LPI_PROTO_UDP_ISAKMP:
			return "ISAKMP";
		case LPI_PROTO_UDP_SNMP:
			return "SNMP";
		case LPI_PROTO_UDP_BACKWEB:
			return "BackWeb";
		case LPI_PROTO_UDP_STARCRAFT:
			return "Starcraft";
		case LPI_PROTO_UDP_XFIRE_P2P:
			return "Xfire_P2P";
		case LPI_PROTO_UDP_THQ:
			return "THQ";
		case LPI_PROTO_UDP_NEWERTH:
			return "HeroesOfNewerth";
		case LPI_PROTO_UDP_LINKPROOF:
			return "Linkproof";
		case LPI_PROTO_UDP_WORM_22105:
			return "Worm_22105";
		case LPI_PROTO_UDP_QQ:
			return "QQ";
		case LPI_PROTO_UDP_SLP:
			return "SLP";
		case LPI_PROTO_UDP_ESO:
			return "Ensemble";
		case LPI_PROTO_UDP_SSDP:
			return "SSDP";
		case LPI_PROTO_UDP_NETBIOS:
			return "Netbios_UDP";
		case LPI_PROTO_UDP_CP_RDP:
			return "Checkpoint_RDP";
		case LPI_PROTO_UDP_VENTRILO:
			return "Ventrilo_UDP";
		case LPI_PROTO_UDP_MTA:
			return "MultiTheftAuto";
		case LPI_PROTO_UDP_PPLIVE:
			return "PPLive";
		case LPI_PROTO_UDP_JEDI:
			return "JediAcademy";
		case LPI_PROTO_UDP_MOH:
			return "MedalOfHonor";
		case LPI_PROTO_UDP_TREMULOUS:
			return "Tremulous";
		case LPI_PROTO_UDP_VIVOX:
			return "Vivox";
		case LPI_PROTO_UDP_IPMSG:
			return "IPMsg";
		case LPI_PROTO_UDP_TEAMSPEAK:
			return "TeamSpeak";
		case LPI_PROTO_UDP_DC:
			return "DirectConnect_UDP";
		case LPI_PROTO_UDP_FREECHAL:
			return "FreeChal_UDP";
		case LPI_PROTO_UDP_XUNLEI:
			return "Xunlei_UDP";
		case LPI_PROTO_UDP_KAZAA:
			return "Kazaa_UDP";
		case LPI_PROTO_UDP_NORTON:
			return "Norton_UDP";
		case LPI_PROTO_UDP_CISCO_VPN:
			return "Cisco_VPN_UDP";
		case LPI_PROTO_UDP_RTCP:
			return "RTCP";
		case LPI_PROTO_UDP_UNREAL:
			return "Unreal";
		case LPI_PROTO_UDP_TFTP:
			return "TFTP";
		case LPI_PROTO_UDP_GARENA:
			return "Garena_UDP";
		case LPI_PROTO_UDP_PPSTREAM:
			return "PPStream";
		case LPI_PROTO_UDP_FORTINET:
			return "Fortinet";
		case LPI_PROTO_UDP_STORM_WORM:
			return "StormWorm";
		case LPI_PROTO_UDP_TVANTS:
			return "TVants";
		case LPI_PROTO_UDP_BATTLEFIELD:
			return "Battlefield";
		case LPI_PROTO_UDP_SOPCAST:
			return "Sopcast";
		case LPI_PROTO_UDP_SERIALNUMBERD:
			return "Serialnumberd";

		
		case LPI_PROTO_REJECTION:
			return "Rejection";
		case LPI_PROTO_MYSTERY_9000:
			return "Mystery_9000";
		case LPI_PROTO_MYSTERY_PSPR:
			return "Mystery_PSPR";
		case LPI_PROTO_MYSTERY_8000:
			return "Mystery_8000";
		case LPI_PROTO_MYSTERY_IG:
			return "Mystery_iG";
		case LPI_PROTO_MYSTERY_CONN:
			return "Mystery_conn";
		case LPI_PROTO_UDP_EMULE_MYSTERY:
			return "eMule_UDP_Mystery";
		case LPI_PROTO_UDP_MYSTERY_0D:
			return "Mystery_0D";
		case LPI_PROTO_UDP_MYSTERY_02_36:
			return "Mystery_02_36";
		case LPI_PROTO_UDP_MYSTERY_FE:
			return "Mystery_FE";
		case LPI_PROTO_UDP_MYSTERY_99:
			return "Mystery_99";
		case LPI_PROTO_UDP_MYSTERY_8000:
			return "Mystery_8000";
		case LPI_PROTO_UDP_MYSTERY_45:
			return "Mystery_45";
		case LPI_PROTO_UDP_MYSTERY_0660:
			return "Mystery_0660";
		case LPI_PROTO_UDP_MYSTERY_E9:
			return "Mystery_E9";
        }

	return "Invalid_Protocol";
}
#endif
