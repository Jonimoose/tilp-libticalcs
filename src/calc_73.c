/* Hey EMACS -*- linux-c -*- */
/* $Id: link_nul.c 1059 2005-05-14 09:45:42Z roms $ */

/*  libCables - Ti Link Cable library, a part of the TiLP project
 *  Copyright (C) 1999-2005  Romain Lievin
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
	TI73/TI83+/TI84+ support.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ticalcs.h"
#include "gettext.h"
#include "logging.h"
#include "error.h"
#include "pause.h"
#include "macros.h"

#include "packets.h"
#include "cmd73.h"
#include "rom83p.h"

// Screen coordinates of the TI83+
#define TI73_ROWS  64
#define TI73_COLS  96

#define DUMP_ROM73_FILE "dumprom.8Xp"
//#define ROMSIZE 512		// 512KB (TI83+) or 1MB (TI84+) or 2MB (SilverEdition)

static int		is_ready	(CalcHandle* handle)
{
	uint16_t status;

	TRYF(ti73_send_RDY());
	TRYF(ti73_recv_ACK(&status));

	return (status & 0x01) ? ERR_NOT_READY : 0;
}

static int		send_key	(CalcHandle* handle, uint16_t key)
{
	TRYF(ti73_send_KEY(key));
	TRYF(ti73_recv_ACK(NULL));

	return 0;
}

static int		recv_screen	(CalcHandle* handle, CalcScreenCoord* sc, uint8_t** bitmap)
{
	uint16_t max_cnt;
	int err;

	sc->width = TI73_COLS;
	sc->height = TI73_ROWS;
	sc->clipped_width = TI73_COLS;
	sc->clipped_height = TI73_ROWS;

	*bitmap = (uint8_t *) malloc(TI73_COLS * TI73_ROWS * sizeof(uint8_t) / 8);
	if(*bitmap == NULL)
		return ERR_MALLOC;

	TRYF(ti73_send_SCR());
	TRYF(ti73_recv_ACK(NULL));

	err = ti73_recv_XDP(&max_cnt, *bitmap);	// pb with checksum
	if (err != ERR_CHECKSUM) { TRYF(err) };
	TRYF(ti73_send_ACK());

	return 0;
}

static int		get_dirlist	(CalcHandle* handle, TNode** vars, TNode** apps)
{
	TreeInfo *ti;
	uint16_t unused;
	uint32_t memory;
	TNode *folder;	
	char utf8[10];

	(*apps) = t_node_new(NULL);
	ti = (TreeInfo *)malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = APP_NODE_NAME;
	(*apps)->data = ti;

	(*vars) = t_node_new(NULL);
	ti = (TreeInfo *)malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = VAR_NODE_NAME;
	(*vars)->data = ti;

	TRYF(ti73_send_REQ(0x0000, TI73_DIR, "", 0x00));
	TRYF(ti73_recv_ACK(NULL));

	TRYF(ti73_recv_XDP(&unused, (uint8_t *)&memory));
	fixup(memory);
	TRYF(ti73_send_ACK());
	ti->mem_free = (uint32_t)GUINT_TO_POINTER(memory);

	folder = t_node_new(NULL);
	t_node_append(*vars, folder);

	for (;;) 
	{
		VarEntry *ve = calloc(1, sizeof(VarEntry));
		TNode *node;
		int err;

		err = ti73_recv_VAR((uint16_t *) & ve->size, &ve->type, ve->name, &ve->attr);
		fixup(ve->size);
		TRYF(ti73_send_ACK());
		if (err == ERR_EOT)
			break;
		else if (err != 0)
			return err;

		node = t_node_new(ve);
		if (ve->type != TI73_APPL)
			t_node_append(folder, node);
		else
			t_node_append(*apps, node);

		tifiles_transcode_varname(handle->model, utf8, ve->name, ve->type);
		sprintf(update->text, _("Reading of '%s'"), utf8);
		update_label();
		if (update->cancel)
		  return ERR_ABORT;
  }

	return 0;
}

static int		get_memfree	(CalcHandle* handle, uint32_t* mem)
{
	return 0;
}

static int		send_backup	(CalcHandle* handle, BackupContent* content)
{
	uint16_t length;
	uint8_t varname[9];
	uint8_t rej_code;

	length = content->data_length1;
	varname[0] = LSB(content->data_length2);
	varname[1] = MSB(content->data_length2);
	varname[2] = LSB(content->data_length3);
	varname[3] = MSB(content->data_length3);
	varname[4] = LSB(content->mem_address);
	varname[5] = MSB(content->mem_address);

	TRYF(ti73_send_RTS(content->data_length1, TI73_BKUP, varname, 0x00));
	TRYF(ti73_recv_ACK(NULL));

	TRYF(ti73_recv_SKP(&rej_code))
    TRYF(ti73_send_ACK());
	switch (rej_code) 
	{
	case REJ_EXIT:
	case REJ_SKIP:
		return ERR_ABORT;
    break;
	case REJ_MEMORY:
		return ERR_OUT_OF_MEMORY;
    break;
	default:			// RTS
    break;
	}

	update->max2 = 3;
	update->cnt2 = 0;
	update->pbar();

	TRYF(ti73_send_XDP(content->data_length1, content->data_part1));
	TRYF(ti73_recv_ACK(NULL));
	update->cnt2++;
	update->pbar();

	TRYF(ti73_send_XDP(content->data_length2, content->data_part2));
	TRYF(ti73_recv_ACK(NULL));
	update->cnt2++;
	update->pbar();

	TRYF(ti73_send_XDP(content->data_length3, content->data_part3));
	TRYF(ti73_recv_ACK(NULL));
	update->cnt2++;
	update->pbar();

	TRYF(ti73_send_ACK());

	return 0;
}

static int		recv_backup	(CalcHandle* handle, BackupContent* content)
{
	uint8_t varname[9] = { 0 };
	uint8_t attr;

	content->model = handle->model;
	strcpy(content->comment, tifiles_comment_set_backup());

	TRYF(ti73_send_REQ(0x0000, TI73_BKUP, "", 0x00));
	TRYF(ti73_recv_ACK(NULL));

	TRYF(ti73_recv_VAR(&content->data_length1, &content->type, varname, &attr));
	content->data_length2 = varname[0] | (varname[1] << 8);
	content->data_length3 = varname[2] | (varname[3] << 8);
	content->mem_address = varname[4] | (varname[5] << 8);
	TRYF(ti73_send_ACK());

	TRYF(ti73_send_CTS());
	TRYF(ti73_recv_ACK(NULL));

	update->max2 = 3;
	update->cnt2 = 0;
	update->pbar();

	content->data_part1 = calloc(65536, 1);
	TRYF(ti73_recv_XDP(&content->data_length1, content->data_part1));
	TRYF(ti73_send_ACK());
	update->cnt2++;
	update->pbar();

	content->data_part2 = calloc(65536, 1);
	TRYF(ti73_recv_XDP(&content->data_length2, content->data_part2));
	TRYF(ti73_send_ACK());
	update->cnt2++;
	update->pbar();

	content->data_part3 = calloc(65536, 1);
	TRYF(ti73_recv_XDP(&content->data_length3, content->data_part3));
	TRYF(ti73_send_ACK());
	update->cnt2++;
	update->pbar();
  
	content->data_part4 = NULL;
  
	return 0;
}

static int		send_var	(CalcHandle* handle, CalcMode mode, FileContent* content)
{
	int i;
	uint8_t rej_code;

	for (i = 0; i < content->num_entries; i++) 
	{
		VarEntry *entry = &(content->entries[i]);
		
		if(entry->action == ACT_SKIP)
			continue;

		TRYF(ti73_send_RTS((uint16_t)entry->size, entry->type, entry->name, entry->attr));
		TRYF(ti73_recv_ACK(NULL));

		TRYF(ti73_recv_SKP(&rej_code));
		TRYF(ti73_send_ACK());

		switch (rej_code) 
		{
		case REJ_EXIT:
		  return ERR_ABORT;
		  break;
		case REJ_SKIP:
		  continue;
		  break;
		case REJ_MEMORY:
		  return ERR_OUT_OF_MEMORY;
		  break;
		default:			// RTS
		  break;
		}
		sprintf(update->text, _("Sending '%s'"),
			tifiles_transcode_varname_static(handle->model, entry->name, entry->type));
		update_label();

		TRYF(ti73_send_XDP(entry->size, entry->data));
		TRYF(ti73_recv_ACK(NULL));

		ticalcs_info("");
  }

	TRYF(ti73_send_EOT());

	return 0;
}

static int		recv_var	(CalcHandle* handle, CalcMode mode, FileContent* content, VarRequest* vr)
{
    VarEntry *ve;

    content->model = handle->model;
	strcpy(content->comment, tifiles_comment_set_single());
    content->num_entries = 1;
    content->entries = (VarEntry *) calloc(1, sizeof(VarEntry));
    ve = &(content->entries[0]);
    memcpy(ve, vr, sizeof(VarEntry));

    sprintf(update->text, _("Receiving '%s'"),
	  tifiles_transcode_varname_static(handle->model, vr->name, vr->type));
    update_label();

    // silent request
    TRYF(ti73_send_REQ((uint16_t)vr->size, vr->type, vr->name, vr->attr));
    TRYF(ti73_recv_ACK(NULL));

    TRYF(ti73_recv_VAR((uint16_t *)&ve->size, &ve->type, ve->name, &vr->attr));
    fixup(ve->size);
    TRYF(ti73_send_ACK());

    TRYF(ti73_send_CTS());
    TRYF(ti73_recv_ACK(NULL));

    ve->data = calloc(ve->size, 1);
    TRYF(ti73_recv_XDP((uint16_t *)&ve->size, ve->data));
    TRYF(ti73_send_ACK());

	return 0;
}

static int		send_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content)
{
	return 0;
}

static int		recv_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content, VarEntry* ve)
{
	return 0;
}

static int		send_flash	(CalcHandle* handle, FlashContent* content)
{
	int i, j, k;
	int size;

	if(content->data_type == TI83p_AMS)
		size = 0x100;
	else if(content->data_type == TI83p_APPL)
		size = 0x80;
	else
		return -1;
/*
	printf("#pages: %i\n", content->num_pages);
	printf("type: %02x\n", content->data_type);
	for (i = 0; i < content->num_pages; i++) 
	{
		FlashPage *fp = &(content->pages[i]);

		printf("page #%i: %04x %02x %02x %04x\n", i,
		content->pages[i].addr,
		content->pages[i].page,
		content->pages[i].flag,
		content->pages[i].size);		
	}

	return 0;
*/
	update->max2 = content->num_pages * FLASH_PAGE_SIZE / size;
	for (k= i = 0; i < content->num_pages; i++) 
	{
		FlashPage *fp = &(content->pages[i]);

		for(j = 0; j < FLASH_PAGE_SIZE; j += size)
		{
			uint16_t addr = fp->addr + j;
			uint8_t* data = fp->data + j;

			TRYF(ti73_send_VAR2(size, content->data_type, fp->flag, addr, fp->page));
			TRYF(ti73_recv_ACK(NULL));

			TRYF(ti73_recv_CTS(10));
			TRYF(ti73_send_ACK());

			TRYF(ti73_send_XDP(size, data));
			TRYF(ti73_recv_ACK(NULL));

			update->cnt2 = ++k;
			update->pbar();
			if (update->cancel)
				return ERR_ABORT;
		}

		if(handle->model != CALC_TI84P)
		{
			if (i == 1)
			  PAUSE(1000);		// This pause is NEEDED !
			if (i == content->num_pages - 2)
			  PAUSE(2500);		// This pause is NEEDED !
		}
	}

	TRYF(ti73_send_EOT());
	TRYF(ti73_recv_ACK(NULL));

	return 0;
}

static int		recv_flash	(CalcHandle* handle, FlashContent* content, VarRequest* vr)
{
	FlashPage *fp;
	uint16_t data_addr;
	uint16_t old_page = 0;
	uint16_t data_page;
	uint16_t data_length;
	uint8_t data_type;
	uint32_t size;
	int first_block;
	int page;
	int offset;
	uint8_t buf[FLASH_PAGE_SIZE + 4];

	sprintf(update->text, _("Receiving '%s'"), vr->name);
	update_label();

	content->model = handle->model;
	strcpy(content->name, vr->name);
	content->data_type = vr->type;
	content->device_type = 0x73;
	content->num_pages = 2048;	// TI83+ has 512 KB of FLASH max
	content->pages = (FlashPage *)calloc(content->num_pages, sizeof(FlashPage));

	TRYF(ti73_send_REQ2(0x00, TI73_APPL, vr->name, 0x00));
	TRYF(ti73_recv_ACK(NULL));

	update->max2 = vr->size;
	for(page = 0, size = 0, first_block = 1, offset = 0;;)
	{
		int err;
		char name[9];

		fp = &(content->pages[page]);

		err = ti73_recv_VAR2(&data_length, &data_type, name, &data_addr, &data_page);
		TRYF(ti73_send_ACK());
		if (err == ERR_EOT)
			goto exit;
		TRYF(err);

		if(first_block)
		{
			old_page = data_page;
			first_block = 0;
			printf("first init !\n");

			fp->addr = data_addr & 0x4000;
			fp->page = data_page;
		}
		if(old_page != data_page)
		{
			fp->addr = data_addr & 0x4000;
			fp->page = data_page;
			fp->flag = 0x80;
			fp->size = offset;			
			fp->data = calloc(FLASH_PAGE_SIZE, 1);
			memcpy(fp->data, buf, fp->size);

			page++;
			offset = 0;
			old_page = data_page;
		}

		TRYF(ti73_send_CTS());
		TRYF(ti73_recv_ACK(NULL));

		TRYF(ti73_recv_XDP((uint16_t *)&data_length, &buf[offset]));
		TRYF(ti73_send_ACK());

		size += data_length;
		offset += data_length;

		update->cnt2 = size;
		update->pbar();
		if (update->cancel)
			return ERR_ABORT;
	}

exit:
	{
		fp->addr = data_addr & 0x4000;
		fp->page = data_page;
		fp->flag = 0x80;
		fp->size = offset;			
		fp->data = calloc(FLASH_PAGE_SIZE, 1);
		memcpy(fp->data, buf, fp->size);
		page++;
	}

	content->num_pages = page;

	return 0;
}

static int		recv_idlist	(CalcHandle* handle, uint8_t* id)
{
	uint16_t unused;
	uint16_t varsize;
	uint8_t vartype;
	uint8_t varname[9];
	uint8_t varattr;
	uint8_t data[16];
	int i;

	sprintf(update->text, _("Getting variable..."));
	update_label();

	TRYF(ti73_send_REQ(0x0000, TI73_IDLIST, "", 0x00));
	TRYF(ti73_recv_ACK(&unused));

	TRYF(ti73_recv_VAR((uint16_t *) & varsize, &vartype, varname, &varattr));
	fixup(varsize);
	TRYF(ti73_send_ACK());

	TRYF(ti73_send_CTS());
	TRYF(ti73_recv_ACK(NULL));

	TRYF(ti73_recv_XDP((uint16_t *) & varsize, data));
	TRYF(ti73_send_ACK());

	i = data[9];
	data[9] = data[10];
	data[10] = i;

	for(i = 4; i < varsize; i++)
		sprintf(&id[2 * (i-4)], "%02x", data[i]);
	id[7*2] = '\0';

	return 0;
}

static int		dump_rom	(CalcHandle* handle, CalcDumpSize size, const char *filename)
{
	int i, j;
	uint8_t data;
	time_t start, elapsed, estimated, remaining;
	char buffer[256];
	char tmp[256];
	int pad;
	FILE *f, *file;
	uint16_t checksum, sum;
	int err;
	int b = 0;
	int ROMSIZE = (size == ROMSIZE_2MB) ? 2048 : (handle->model == CALC_TI84P) ? 1024 : 512;
	FileContent content;

	// Copies ROM dump program into a file
	f = fopen(DUMP_ROM73_FILE, "wb");
	if (f == NULL)
		return ERR_FILE_OPEN;
	fwrite(romDump83p, sizeof(unsigned char), romDumpSize83p, f);
	fclose(f);

	// Transfer program to calc
	tifiles_file_read_regular(DUMP_ROM73_FILE, &content);
	TRYF(send_var(handle, MODE_SEND_ONE_VAR, &content));
	tifiles_content_free_regular(&content);
	unlink(DUMP_ROM73_FILE);

	// Open file
	file = fopen(filename, "wb");
	if (file == NULL)
		return ERR_OPEN_FILE;

	// Wait for user's action (execing program)
	sprintf(handle->updat->text, _("Waiting user's action..."));
	handle->updat->label();
	do 
	{
		handle->updat->refresh();
		if (handle->updat->cancel)
			return ERR_ABORT;
		err = ticables_cable_get(handle->cable, &data);
		sum = data;
	}
	while (err == ERROR_READ_TIMEOUT);
	fprintf(file, "%c", data);

	// Receive it now blocks per blocks (1024 + CHK)
	sprintf(handle->updat->text, _("Receiving..."));
	handle->updat->label();

	start = time(NULL);
	handle->updat->max1 = 1024;
	handle->updat->max2 = ROMSIZE;

	for (i = 0; i < ROMSIZE; i++) 
	{
		if (b)
			sum = 0;

		for (j = 0; j < 1023 + b; j++) 
		{
			TRYF(ticables_cable_get(handle->cable, &data));
			fprintf(file, "%c", data);
			sum += data;

			handle->updat->cnt1 = j;
			handle->updat->pbar();
			if (handle->updat->cancel)
				return -1;
		}
		b = 1;

		TRYF(ticables_cable_get(handle->cable, &data));
		checksum = data << 8;
		TRYF(ticables_cable_get(handle->cable, &data));
		checksum |= data;
		if (sum != checksum)
		  return ERR_CHECKSUM;
		TRYF(ticables_cable_put(handle->cable, 0xDA));

		handle->updat->cnt2 = i;
		update->pbar();
		if (handle->updat->cancel)
		  return -1;

		elapsed = (long) difftime(time(NULL), start);
		estimated = (long) (elapsed * (float) (ROMSIZE) / i);
		remaining = (long) difftime(estimated, elapsed);
		sprintf(buffer, "%s", ctime(&remaining));
		sscanf(buffer, "%3s %3s %i %s %i", tmp, tmp, &pad, tmp, &pad);
		sprintf(handle->updat->text, _("Remaining (mm:ss): %s"), tmp + 3);
		handle->updat->label();
	}

	fclose(file);	

	return 0;
}

static int days_in_months[12] = 
{
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};

static int days_elapsed(int month)
{
	int i;
	int days;

	if(!month || month > 11)
		return 0;

	for(i = 0, days = 0; i < month-1; i++)
		days += days_in_months[i];

	printf("days = %i\n", days);

	return days;
}

static int		set_clock	(CalcHandle* handle, CalcClock* clock)
{
	uint8_t buffer[16] = { 0 };
	uint32_t time;

	struct tm ref, cur;
	time_t r, c;

	ref.tm_year = 1997 - 1900;
	ref.tm_mon = 0;
	ref.tm_yday = 0;
	ref.tm_mday = 1;
	ref.tm_wday = 3;
	ref.tm_hour = 0;
	ref.tm_min = 0;
	ref.tm_sec = 0;
	ref.tm_isdst = 1;
	r = mktime(&ref);
	//printf("<%s>\n", asctime(&ref));

	cur.tm_year = clock->year - 1900;
	cur.tm_mon = clock->month - 1;
	cur.tm_mday = clock->day;	
	cur.tm_hour = clock->hours;
	cur.tm_min = clock->minutes;
	cur.tm_sec = clock->seconds;
	cur.tm_isdst = 1;
	c = mktime(&cur);
	//printf("<%s>\n", asctime(&cur));
	
	time = c - r;

    buffer[2] = MSB(MSW(time));
    buffer[3] = LSB(MSW(time));
    buffer[4] = MSB(LSW(time));
    buffer[5] = LSB(LSW(time));
    buffer[6] = clock->date_format;
    buffer[7] = clock->time_format;
    buffer[8] = 0xff;

    sprintf(update->text, _("Setting clock..."));
    update_label();

	TRYF(ti73_send_RTS(13, TI73_CLK, "\0x08", 0x00));
    TRYF(ti73_recv_ACK(NULL));

    TRYF(ti73_recv_CTS(13));
    TRYF(ti73_send_ACK());

    TRYF(ti73_send_XDP(9, buffer));
    TRYF(ti73_recv_ACK(NULL));

    TRYF(ti73_send_EOT());

	return 0;
}

static int		get_clock	(CalcHandle* handle, CalcClock* clock)
{
	uint16_t varsize;
    uint8_t vartype;
	uint8_t varattr;
    uint8_t varname[9];
    uint8_t buffer[32];
	uint32_t time;

	struct tm ref, *cur;
	time_t r, c;

    sprintf(update->text, _("Getting clock..."));
    update_label();

	TRYF(ti73_send_REQ(0x0000, TI73_CLK, "\0x08", 0x00));
    TRYF(ti73_recv_ACK(NULL));

    TRYF(ti73_recv_VAR(&varsize, &vartype, varname, &varattr));
    TRYF(ti73_send_ACK());

    TRYF(ti73_send_CTS());
    TRYF(ti73_recv_ACK(NULL));

    TRYF(ti73_recv_XDP(&varsize, buffer));
    TRYF(ti73_send_ACK());

	time = (buffer[2] << 24) | (buffer[3] << 16) | (buffer[4] << 8) | buffer[5];
	//printf("<%08x>\n", time);

	ref.tm_year = 1997 - 1900;
	ref.tm_mon = 0;
	ref.tm_yday = 0;
	ref.tm_mday = 1;
	ref.tm_wday = 3;
	ref.tm_hour = 0;
	ref.tm_min = 0;
	ref.tm_sec = 0;
	ref.tm_isdst = 1;
	r = mktime(&ref);
	//printf("<%s>\n", asctime(&ref));

	c = r + time;
	cur = localtime(&c);
	//printf("<%s>\n", asctime(cur));

	clock->year = cur->tm_year + 1900;
	clock->month = cur->tm_mon + 1;
	clock->day = cur->tm_mday;
	clock->hours = cur->tm_hour;
	clock->minutes = cur->tm_min;
	clock->seconds = cur->tm_sec;

    clock->date_format = buffer[6];
    clock->time_format = buffer[7];

	return 0;
}

static int		del_var		(CalcHandle* handle, VarRequest* vr)
{
	TRYF(ti73_send_DEL((uint16_t)vr->size, vr->type, vr->name, vr->attr));
	TRYF(ti73_recv_ACK(NULL));

	PAUSE(500);

	return 0;
}

static int		new_folder  (CalcHandle* handle, VarRequest* vr)
{
	return 0;
}

static int		get_version	(CalcHandle* handle, CalcInfos* infos)
{
	uint16_t length;
	uint8_t buf[32];

	TRYF(ti73_send_VER());
	TRYF(ti73_recv_ACK(NULL));

	TRYF(ti73_send_CTS());
    TRYF(ti73_recv_ACK(NULL));

	TRYF(ti73_recv_XDP(&length, buf));
    TRYF(ti73_send_ACK());

	ticalcs_info(_("  OS: %i.%2i"), buf[0], buf[1]);
	ticalcs_info(_("  BIOS: %i.%2i"), buf[2], buf[3]);

	memset(infos, 0, sizeof(CalcInfos));
	infos->os[0] = buf[0] + '0';
	infos->os[1] = '.';
	infos->os[2] = MSN(buf[1]) + '0';
	infos->os[3] = LSN(buf[1]) + '0';
	infos->os[4] = '\0';

	infos->bios[0] = buf[2] + '0';
	infos->bios[1] = '.';
	infos->bios[2] = MSN(buf[3]) + '0';
	infos->bios[3] = LSN(buf[3]) + '0';
	infos->bios[4] = '\0';

	return 0;
}

const CalcFncts calc_73 = 
{
	CALC_TI73,
	"TI73",
	N_("TI-73"),
	N_("TI-73"),
	OPS_ISREADY | OPS_KEYS | OPS_SCREEN | OPS_DIRLIST | /*OPS_BACKUP |*/ OPS_VARS | 
	OPS_FLASH | OPS_IDLIST | OPS_ROMDUMP |
	OPS_DELVAR | OPS_NEWFLD | OPS_VERSION |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	&is_ready,
	&send_key,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&recv_idlist,
	&dump_rom,
	&set_clock,
	&get_clock,
};

const CalcFncts calc_83p = 
{
	CALC_TI83P,
	"TI83+",
	N_("TI-83 Plus"),
	N_("TI-83 Plus"),
	OPS_ISREADY | OPS_KEYS | OPS_SCREEN | OPS_DIRLIST | OPS_BACKUP | OPS_VARS | 
	OPS_FLASH | OPS_IDLIST | OPS_ROMDUMP | OPS_CLOCK |
	OPS_DELVAR | OPS_NEWFLD | OPS_VERSION |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	&is_ready,
	&send_key,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&recv_idlist,
	&dump_rom,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
};

const CalcFncts calc_84p = 
{
	CALC_TI84P,
	"TI84+",
	N_("TI-84 Plus"),
	N_("TI-84 Plus"),
	OPS_ISREADY | OPS_KEYS | OPS_SCREEN | OPS_DIRLIST | OPS_BACKUP | OPS_VARS | 
	OPS_FLASH | OPS_IDLIST | OPS_ROMDUMP | OPS_CLOCK |
	OPS_DELVAR | OPS_NEWFLD | OPS_VERSION |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	&is_ready,
	&send_key,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&recv_idlist,
	&dump_rom,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
};
