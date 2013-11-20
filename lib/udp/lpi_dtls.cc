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
 * $Id: lpi_dtls.cc 155 2013-10-21 03:21:00Z salcock $
 */

#include <string.h>

#include "libprotoident.h"
#include "proto_manager.h"
#include "proto_common.h"

/* This is basically SSL for UDP. Used by AnyConnect.
 * It is possible that we can subclassify this traffic, e.g. maybe AnyConnect
 * is the only DTLS app that uses port 443 for instance...
 * 
 * Thanks to Remy Mudingay for helping to identify this protocol
 */

static inline bool match_dtls(lpi_data_t *data, lpi_module_t *mod UNUSED) {

	/* DTLS uses entirely different versioning to conventional TLS */

	if (MATCH(data->payload[0], 0x17, 0x01, 0x00, 0x00)) {
		if (data->payload_len[1] == 0)
			return true;
		if (MATCH(data->payload[1], 0x17, 0x01, 0x00, 0x00))
			return true;
	}

	if (MATCH(data->payload[1], 0x17, 0x01, 0x00, 0x00)) {
		if (data->payload_len[0] == 0)
			return true;
	}
	return false;
}

static lpi_module_t lpi_dtls = {
	LPI_PROTO_UDP_DTLS,
	LPI_CATEGORY_ENCRYPT,
	"DTLS",
	100,
	match_dtls
};

void register_dtls(LPIModuleMap *mod_map) {
	register_protocol(&lpi_dtls, mod_map);
}

