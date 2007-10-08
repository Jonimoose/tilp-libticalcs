/* Hey EMACS -*- linux-c -*- */
/* $Id: cmd84p.c 2077 2006-03-31 21:16:19Z roms $ */

/*  libticalcs - Ti Calculator library, a part of the TiLP project
 *  Copyright (C) 1999-2005  Romain Li�vin
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	This unit manages virtual packets from/to NSPire (DirectLink).
	Virtual packets are fragmented into one or more raw packets.
*/

#include <stdlib.h>
#include <string.h>

#include "ticalcs.h"
#include "logging.h"
#include "error.h"
#include "macros.h"

#include "nsp_rpkt.h"
#include "nsp_vpkt.h"

#define VPKT_DBG	1	// 1 = verbose, 2 = more verbose

// Type to string

static const ServiceName sid_types[] = 
{
	{ 0x00FE, "Reception Acknowledgment" },
	{ 0x00FF, "Reception Ack" },
	{ 0x4001, "Null" },
	{ 0x4002, "Echo" },
	{ 0x4003, "Device Address Request/Assignment" },
	{ 0x4020, "Device Information" },
	{ 0x4021, "Screen Capture" },
	{ 0x4024, "Screen Capture w/ RLE" },
	{ 0x4050, "Login" },
	{ 0x4060, "File Management" },
	{ 0x4080, "OS Installation" },
	{ 0x40DE, "Service Disconnect" },
	{ -1, NULL},
};

const char* nsp_sid2name(uint16_t id)
{
	const ServiceName *p;

	for(p = sid_types; p->name != NULL; p++)
		if(p->id == id)
			return p->name;

	return "unknown: not listed";
}

// Buffer allocation

static GList *vtl_pkt_list = NULL;

VirtualPacket*  vtl_pkt_new(uint32_t size, uint16_t src_addr, uint16_t src_id, uint16_t dst_addr, uint16_t dst_id)
{
	VirtualPacket* vtl = g_malloc0(sizeof(VirtualPacket));

	vtl->src_addr = src_addr;
	vtl->src_id = src_id;
	vtl->dst_addr = dst_addr;
	vtl->dst_id = dst_id;
	vtl->size = size;
	vtl->data = g_malloc0(size);

	vtl_pkt_list = g_list_append(vtl_pkt_list, vtl);

	return vtl;
}

void			vtl_pkt_del(VirtualPacket* vtl)
{
	vtl_pkt_list = g_list_remove(vtl_pkt_list, vtl);

	g_free(vtl->data);
	g_free(vtl);
}

void			vtl_pkt_purge(void)
{
	g_list_foreach(vtl_pkt_list, (GFunc)vtl_pkt_del, NULL);
	g_list_free(vtl_pkt_list);
	vtl_pkt_list = NULL;
}

// Fragmenting of packets

int dusb_send_data(CalcHandle *h, VirtualPacket *vtl)
{
	return 0;
}

// beware: data field may be re-allocated in size !
int dusb_recv_data(CalcHandle* h, VirtualPacket* vtl)
{
	return 0;
}
