/* 
 * This file is part of libprotoident
 *
 * Copyright (c) 2011 The University of Waikato, Hamilton, New Zealand.
 * Author: Shane Alcock
 *
 * With contributions from:
 *      Aaron Murrihy
 *      Donald Neal
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
 * $Id: lpi_wechat.cc 160 2013-11-04 03:47:06Z salcock $
 */

#include <string.h>

#include "libprotoident.h"
#include "proto_manager.h"
#include "proto_common.h"

static inline bool match_wc_first(uint32_t payload, uint32_t len) {
	
	/* The initial request is always 16 bytes */
	if (len != 16)
		return false;

	if (!MATCH(payload, 0x00, 0x00, 0x00, 0x10))
		return false;
	
	return true;

}

static inline bool match_wc_second(uint32_t payload, uint32_t len) {
	if (len == 0)
		return true;
	
	/* The response is either 16 or 18 bytes in size.
	 * Technically, more values could be possible but until we see
	 * them in the wild I'll just match on 16 and 18.
	 */

	if (len == 16 && MATCH(payload, 0x00, 0x00, 0x00, 0x10))
		return true;
	if (len == 18 && MATCH(payload, 0x00, 0x00, 0x00, 0x12))
		return true;

	return false;
}

static inline bool match_wechat(lpi_data_t *data, lpi_module_t *mod UNUSED) {
	bool valid_port = false;

	/* WeChat begins with a very simple 4 byte length field.
	 * This is not unique to WeChat though, so we need to be careful.
	 */

	/* Only observed on port 80, 443 or 8080. Because the payload 
	 * signature is not entirely unique to WeChat, let's restrict matches
	 * to flows using those ports unless it shows up on other ports.
	 */
	if (data->server_port == 80 || data->client_port == 80)
		valid_port = true;
	if (data->server_port == 8080 || data->client_port == 8080)
		valid_port = true;
	if (data->server_port == 443 || data->client_port == 443)
		valid_port = true;

	if (!valid_port)
		return false;

	if (match_wc_first(data->payload[0], data->payload_len[0])) {
		if (match_wc_second(data->payload[1], data->payload_len[1]))
			return true;
	}
	
	if (match_wc_first(data->payload[1], data->payload_len[1])) {
		if (match_wc_second(data->payload[0], data->payload_len[0]))
			return true;
	}

	return false;

}

static lpi_module_t lpi_wechat = {
	LPI_PROTO_WECHAT,
	LPI_CATEGORY_CHAT,
	"WeChat",
	10, 
	match_wechat
};

void register_wechat(LPIModuleMap *mod_map) {
	register_protocol(&lpi_wechat, mod_map);
}

