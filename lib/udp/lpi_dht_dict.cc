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
 * $Id: lpi_dht_dict.cc 66 2011-02-10 04:16:18Z salcock $
 */

#include <string.h>

#include "libprotoident.h"
#include "proto_manager.h"
#include "proto_common.h"

/* Separate modules for dictionary-style DHT (which has a much stronger rule)
 * and Vuze DHTs (which are not so strong) */

static inline bool match_dht_dict(lpi_data_t *data, lpi_module_t *mod UNUSED) {
	
	if (match_chars_either(data, 'd', '1', ':', ANY))
		return true;
	if (match_chars_either(data, 'd', '1', ANY, ':'))
		return true;
	
	return false;
}

static lpi_module_t lpi_dht_dict = {
	LPI_PROTO_UDP_BTDHT,
	LPI_CATEGORY_P2P_STRUCTURE,
	"BitTorrent_UDP",
	2,
	match_dht_dict
};

void register_dht_dict(LPIModuleMap *mod_map) {
	register_protocol(&lpi_dht_dict, mod_map);
}

