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
	TI89/TI92+/V200/TI89 Titanium support.
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

#include "cmd89.h"
#include "rom89.h"
#include "keys89.h"

// Screen coordinates of the TI89
#define TI89_ROWS          128
#define TI89_COLS          240
#define TI89_ROWS_VISIBLE  100
#define TI89_COLS_VISIBLE  160

#define DUMP_ROM89_FILE "dumprom.89z"
//#define ROMSIZE (2*1024)	// 2MB or 4MB (Titanium)

static int		is_ready	(CalcHandle* handle)
{
	uint16_t status;

	TRYF(ti89_send_RDY());
	TRYF(ti89_recv_ACK(&status));

	return (status & 0x01) ? ERR_NOT_READY : 0;
}

static int		send_key	(CalcHandle* handle, uint16_t key)
{
	TRYF(ti89_send_KEY(key));
	TRYF(ti89_recv_ACK(NULL));

	return 0;
}

static int		recv_screen	(CalcHandle* handle, CalcScreenCoord* sc, uint8_t** bitmap)
{
	uint32_t max_cnt;
	int err;

	sc->width = TI89_COLS;
	sc->height = TI89_ROWS;
	switch (handle->model) 
	{
	case CALC_TI89:
	case CALC_TI89T:
		sc->clipped_width = TI89_COLS_VISIBLE;
		sc->clipped_height = TI89_ROWS_VISIBLE;
    break;
	case CALC_TI92P:
	case CALC_V200:
		sc->clipped_width = TI89_COLS;
		sc->clipped_height = TI89_ROWS;
    break;
	default:
	    sc->clipped_width = TI89_COLS;
	    sc->clipped_height = TI89_ROWS;
	    break;
	}

	*bitmap = (uint8_t *) malloc(TI89_COLS * TI89_ROWS * sizeof(uint8_t) / 8);
	if(*bitmap == NULL) 
		return ERR_MALLOC;

	TRYF(ti89_send_SCR());
	TRYF(ti89_recv_ACK(NULL));

	err = ti89_recv_XDP(&max_cnt, *bitmap);	// pb with checksum
	if(err != ERR_CHECKSUM) { TRYF(err) };
	TRYF(ti89_send_ACK());

	// Clip the unused part of the screen (nethertheless useable witha asm prog)
	if(((handle->model == CALC_TI89) || (handle->model == CALC_TI89T))
      && (sc->format == SCREEN_CLIPPED)) 
	{
		int i, j, k;

		for(i = 0, j = 0; j < TI89_ROWS_VISIBLE; j++)
			for(k = 0; k < (TI89_COLS_VISIBLE >> 3); k++)
				(*bitmap)[i++] = (*bitmap)[j * (TI89_COLS >> 3) + k];
	}

	return 0;
}

static int		get_dirlist	(CalcHandle* handle, TNode** vars, TNode** apps)
{
	TreeInfo *ti;
	VarEntry info;
	uint32_t block_size;
	uint8_t buffer[65536];
    int i, j;
    uint8_t extra = (handle->model == CALC_V200) ? 8 : 0;

	// get list of folders & FLASH apps
    (*vars) = t_node_new(NULL);
	ti = (TreeInfo *)malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = VAR_NODE_NAME;
	(*vars)->data = ti;

	(*apps) = t_node_new(NULL);
	ti = (TreeInfo *)malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = APP_NODE_NAME;
	(*apps)->data = ti;

    TRYF(ti89_send_REQ(TI89_FDIR << 24, TI89_RDIR, ""));
    TRYF(ti89_recv_ACK(NULL));

    TRYF(ti89_recv_VAR(&info.size, &info.type, info.name));
    TRYF(ti89_send_ACK());

    TRYF(ti89_send_CTS());
    TRYF(ti89_recv_ACK(NULL));

    TRYF(ti89_recv_XDP(&block_size, buffer));
    TRYF(ti89_send_ACK());

    TRYF(ti89_recv_EOT());
    TRYF(ti89_send_ACK());

	for(j = 4; j < (int)block_size;) 
	{
		VarEntry *fe = calloc(1, sizeof(VarEntry));
        TNode *node;

        memcpy(fe->name, buffer + j, 8);
        fe->name[8] = '\0';
        fe->type = buffer[j + 8];
        fe->attr = buffer[j + 9];
        fe->size = buffer[j + 10] | (buffer[j + 11] << 8) | (buffer[j + 12] << 16);	// | (buffer[j+13] << 24);
        j += 14 + extra;
        strcpy(fe->folder, "");

		ticalcs_info(_("Name: %8s | Type: %8s | Attr: %i  | Size: %08X"), 
			fe->name, 
			tifiles_vartype2string(handle->model, fe->type),
			fe->attr,
			fe->size);

        if(fe->type == TI89_DIR)
		{
			node = t_node_new(fe);
			t_node_append(*vars, node);
		}
        else if(fe->type == TI89_APPL)
		{
			// AMS<2.08 returns FLASH apps
			continue;
		}
	}

	// get list of variables into each folder
	for(i = 0; i < (int)t_node_n_children(*vars); i++) 
	{
		TNode *folder = t_node_nth_child(*vars, i);
		char *folder_name = ((VarEntry *) (folder->data))->name;

		ticalcs_info(_("Directory listing in %8s...\n"), folder_name);

		TRYF(ti89_send_REQ(TI89_LDIR << 24, TI89_RDIR, folder_name));
		TRYF(ti89_recv_ACK(NULL));

		TRYF(ti89_recv_VAR(&info.size, &info.type, info.name));
		TRYF(ti89_send_ACK());

		TRYF(ti89_send_CTS());
		TRYF(ti89_recv_ACK(NULL));

		TRYF(ti89_recv_XDP(&block_size, buffer));
		TRYF(ti89_send_ACK());

		TRYF(ti89_recv_EOT());
		TRYF(ti89_send_ACK());

		for(j = 4 + 14 + extra; j < (int)block_size;) 
		{
			VarEntry *ve = calloc(1, sizeof(VarEntry));
			TNode *node;

			memcpy(ve->name, buffer + j, 8);
			ve->name[8] = '\0';
			ve->type = buffer[j + 8];
			ve->attr = buffer[j + 9];
			ve->size = buffer[j + 10] | (buffer[j + 11] << 8) | (buffer[j + 12] << 16);	// | (buffer[j+13] << 24);
			j += 14 + extra;
			strcpy(ve->folder, folder_name);

			ticalcs_info(_("Name: %8s | Type: %8s | Attr: %i  | Size: %08X"), 
			ve->name, 
			tifiles_vartype2string(handle->model, ve->type),
			ve->attr,
			ve->size);

			sprintf(update->text, _("Reading of '%s/%s'"),
			  ((VarEntry *) (folder->data))->name, ve->name);
			update_label();
			if(update->cancel)
				return -1;

			if(ve->type == TI89_APPL) 
			{
				if(!ticalcs_dirlist_app_exist(*apps, ve->name))
				{
					node = t_node_new(ve);
					t_node_append(*apps, node);
				}
			} 
			else
			{
				node = t_node_new(ve);
				t_node_append(folder, node);
			}
		}
		
		ticalcs_info("\n");
	}

	return 0;
}

static int		get_memfree	(CalcHandle* handle, uint32_t* mem)
{
	return ERR_UNSUPPORTED;
}

static int		send_var	(CalcHandle* handle, CalcMode mode, FileContent* content)
{
	int i;
	uint16_t status;

	update->max2 = content->num_entries;
	for(i = 0; i < content->num_entries; i++) 
	{
		VarEntry *entry = &(content->entries[i]);
		uint8_t buffer[65536 + 4] = { 0 };
		uint8_t vartype = entry->type;
		uint8_t varname[18], utf8[35];

		if((mode & MODE_LOCAL_PATH) && !(mode & MODE_BACKUP)) 
		{	
			// local & not backup
			strcpy(varname, entry->name);
		} 
		else 
		{
			// full or backup
			tifiles_build_fullname(handle->model, varname, entry->folder, entry->name);
		}

		tifiles_transcode_varname(handle->model, utf8, varname, entry->type);
		sprintf(update->text, _("Sending '%s'"), utf8);
		update_label();

		if(mode & MODE_BACKUP) 
		{	
			// backup: keep attributes
			switch (entry->attr) 
			{
			case TI89_VNONE:
				vartype = TI89_BKUP;
			break;
			case TI89_VLOCK:
				vartype = 0x26;
			break;
			case TI89_VARCH:
				vartype = 0x27;
			break;
			}

			TRYF(ti89_send_RTS(entry->size, vartype, varname));
		} 
		else 
		{
		  TRYF(ti89_send_VAR(entry->size, vartype, varname));
		}

		TRYF(ti89_recv_ACK(NULL));

		TRYF(ti89_recv_CTS());
		TRYF(ti89_send_ACK());

		memcpy(buffer + 4, entry->data, entry->size);
		TRYF(ti89_send_XDP(entry->size + 4, buffer));
		TRYF(ti89_recv_ACK(&status));

		TRYF(ti89_send_EOT());
		TRYF(ti89_recv_ACK(NULL));

		if(mode & MODE_BACKUP) 
		{
			update->cnt2 = i;		  
			if(update->cancel)
				return ERR_ABORT;
		}
	}

	return 0;
}

static int		recv_var	(CalcHandle* handle, CalcMode mode, FileContent* content, VarRequest* vr)
{
	uint16_t status;
	VarEntry *ve;
	uint32_t unused;
	uint8_t varname[20], utf8[35];

	content->model = handle->model;
	content->num_entries = 1;
	content->entries = (VarEntry *) tifiles_calloc(1, sizeof(VarEntry));
	ve = &(content->entries[0]);
	memcpy(ve, vr, sizeof(VarEntry));

	tifiles_build_fullname(handle->model, varname, vr->folder, vr->name);

	tifiles_transcode_varname(handle->model, utf8, varname, vr->type);
	sprintf(update->text, _("Receiving '%s'"), utf8);
	update_label();

	TRYF(ti89_send_REQ(0, vr->type, varname));
	TRYF(ti89_recv_ACK(&status));
	if(status != 0)
		return ERR_MISSING_VAR;

	TRYF(ti89_recv_VAR(&ve->size, &ve->type, ve->name));
	TRYF(ti89_send_ACK());

	TRYF(ti89_send_CTS());
	TRYF(ti89_recv_ACK(NULL));

	ve->data = tifiles_calloc(ve->size + 4, 1);
	TRYF(ti89_recv_XDP(&unused, ve->data));
	memmove(ve->data, ve->data + 4, ve->size);
	TRYF(ti89_send_ACK());

	TRYF(ti89_recv_EOT());
	TRYF(ti89_send_ACK());

	PAUSE(250);

	return 0;
}

static int		send_backup	(CalcHandle* handle, BackupContent* content)
{
	TRYF(ti89_send_VAR(0, TI89_BKUP, "main"));
	TRYF(ti89_recv_ACK(NULL));

	TRYF(ti89_recv_CTS());
	TRYF(ti89_send_ACK());

	TRYF(ti89_send_EOT());
	TRYF(ti89_recv_ACK(NULL));

	TRYF(send_var(handle, MODE_BACKUP, (FileContent *)content));

	return 0;
}

static int		recv_backup	(CalcHandle* handle, BackupContent* content)
{
	int i, j;
	int i_max, j_max;
	int mask = MODE_BACKUP;
	TNode *vars, *apps;
	int nvars, ivars = 0;
	int b = 0;

	// Do a directory list and check for something to backup
	TRYF(get_dirlist(handle, &vars, &apps));
	nvars = ticalcs_dirlist_num_vars(vars);
	if(!nvars)
		return ERR_NO_VARS;

	// Check whether the last folder is empty
	b = t_node_n_children(t_node_nth_child(vars, t_node_n_children(vars) - 1));

	// Receive all variables, except FLASH apps
	update->max2 = nvars;
	i_max = t_node_n_children(vars);

	for(i = 0; i < i_max; i++) 
	{
		TNode *parent = t_node_nth_child(vars, i);

		j_max = t_node_n_children(parent);
		for(j = 0; j < j_max; j++) 
		{
			TNode *node = t_node_nth_child(parent, j);

			VarEntry *ve = (VarEntry *) (node->data);

			/*
			if(!i && !j)
				mask = MODE_RECEIVE_FIRST_VAR;
			else if((i == i_max - 1) && (j == j_max - 1) && b)
				mask = MODE_RECEIVE_LAST_VAR;
			else if((i == i_max - 2) && (j == j_max - 1) && !b)
				mask = MODE_RECEIVE_LAST_VAR;
			else
				mask = 0;
*/
			// we need to group files !
		  TRYF(is_ready(handle));
		  TRYF(recv_var(handle, 0, (FileContent *)content, ve));
		  //TRYF(ti89_recv_var((char *) filename, mask, ve));

		  update->cnt2 = ivars++;
		  if(update->cancel)
				return ERR_ABORT;
		}
	}

	ticalcs_dirlist_destroy(&vars);
	ticalcs_dirlist_destroy(&apps);

	return 0;
}

static int		del_var		(CalcHandle* handle, VarRequest* vr)
{
	return ERR_UNSUPPORTED;
}

static int		send_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content)
{
	return send_var(handle, mode, content);
}

static int		recv_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content, VarEntry* ve)
{
	uint32_t unused;
	int nvar, err;
    char tipath[18];
    char *tiname;

	content->model = handle->model;

	// receive packets
	for(nvar = 1;; nvar++)
	{
		VarEntry *ve;

		content->entries = (VarEntry *) tifiles_realloc(content->entries, nvar * sizeof(VarEntry));
		ve = &(content->entries[nvar-1]);
		strcpy(ve->folder, "main");	

		err = ti89_recv_VAR(&ve->size, &ve->type, tipath);
		TRYF(ti89_send_ACK());

		if(err == ERR_EOT)	// end of transmission
			goto exit;
		else
			content->num_entries = nvar;

		// from Christian (TI can send varname or fldname/varname)
        if((tiname = strchr(tipath, '\\')) != NULL) 
		{
			*tiname = '\0';
            strcpy(ve->folder, tipath);
            strcpy(ve->name, tiname + 1);
        }
        else 
		{
            strcpy(ve->folder, "main");
            strcpy(ve->name, tipath);
        }

		sprintf(update->text, _("Receiving '%s'"), ve->name);
		update_label();

		TRYF(ti89_send_CTS());
		TRYF(ti89_recv_ACK(NULL));

		ve->data = tifiles_calloc(ve->size + 4, 1);
		TRYF(ti89_recv_XDP(&unused, ve->data));
		memmove(ve->data, ve->data + 4, ve->size);
		TRYF(ti89_send_ACK());
	}

exit:
	// write file content
	nvar--;

	return 0;
}

static int		send_flash	(CalcHandle* handle, FlashContent* content)
{
	FlashContent *ptr;
	int i, nblocks;
	int nheaders = 0;

	// count headers
	for(ptr = content; ptr != NULL; ptr = ptr->next)
		nheaders++;

	// use the last one (data)
	for(i = 0, ptr = content; i < nheaders - 1; i++)
		ptr = ptr->next;

	ticalcs_info(_("FLASH app/os name: \"%s\"\n"), ptr->name);
	ticalcs_info(_("FLASH app/os size: %i bytes.\n"), ptr->data_length);

	if(ptr->data_type == TI89_AMS) 
	{
	  if(handle->model == CALC_TI89T)
	  {
		TRYF(ti89_send_RTS2(ptr->data_length, ptr->data_type, ""));
	  }
	  else
	  {
		TRYF(ti89_send_RTS(ptr->data_length, ptr->data_type, ""));
	  }
	} 
	else 
	{
		TRYF(ti89_send_RTS(ptr->data_length, ptr->data_type, ptr->name));
	}

	nblocks = ptr->data_length / 65536;
	update->max2 = nblocks;

	for(i = 0; i <= nblocks; i++) 
	{
		uint32_t length = (i != nblocks) ? 65536 : ptr->data_length % 65536;

		TRYF(ti89_recv_ACK(NULL));

		TRYF(ti89_recv_CTS());
		TRYF(ti89_send_ACK());

		TRYF(ti89_send_XDP(length, (ptr->data_part) + 65536 * i));
		TRYF(ti89_recv_ACK(NULL));

		if(i != nblocks) 
		{
		  TRYF(ti89_send_CNT());
		} 
		else 
		{
		  TRYF(ti89_send_EOT());
		}

		update->cnt2 = i;
		if(update->cancel)
			return ERR_ABORT;
	}

	if(ptr->data_type == TI89_AMS)
		TRYF(ti89_recv_ACK(NULL));

	ticalcs_info(_("Flash application/os sent completely.\n"));

	return 0;
}

static int		recv_flash	(CalcHandle* handle, FlashContent* content, VarRequest* vr)
{
	int i;

	content->model = handle->model;
	content->data_part = (uint8_t *) tifiles_calloc(2 * 1024 * 1024, 1);	// 2MB max

	sprintf(update->text, _("Receiving '%s'"), vr->name);
	update_label();

	TRYF(ti89_send_REQ(0x00, TI89_APPL, vr->name));
	TRYF(ti89_recv_ACK(NULL));

	TRYF(ti89_recv_VAR(&content->data_length, &content->data_type, content->name));

	update->cnt2 = vr->size;
	for(i = 0, content->data_length = 0;; i++) 
	{
		int err;
		uint32_t block_size;

		TRYF(ti89_send_ACK());

		TRYF(ti89_send_CTS());
		TRYF(ti89_recv_ACK(NULL));

		TRYF(ti89_recv_XDP(&block_size, content->data_part + content->data_length));
		TRYF(ti89_send_ACK());

		content->data_length += block_size;

		err = ti89_recv_CNT();
		if(err == ERR_EOT)
			break;
		TRYF(err);

		update->cnt2 = content->data_length;
		if(update->cancel)
		  return ERR_ABORT;
	}

	TRYF(ti89_send_ACK());

	return 0;
}

static int		recv_idlist	(CalcHandle* handle, uint8_t* idlist)
{
	uint32_t varsize;
	uint8_t vartype;
	uint8_t varname[9];

	sprintf(update->text, _("Getting variable..."));
	update_label();

	TRYF(ti89_send_REQ(0x0000, TI89_IDLIST, ""));
	TRYF(ti89_recv_ACK(NULL));

	TRYF(ti89_recv_VAR(&varsize, &vartype, varname));
	TRYF(ti89_send_ACK());

	TRYF(ti89_send_CTS());
	TRYF(ti89_recv_ACK(NULL));

	TRYF(ti89_recv_XDP(&varsize, idlist));
	memcpy(idlist, idlist+8, varsize - 8);
	idlist[varsize - 8] = '\0';
	TRYF(ti89_send_ACK());

	TRYF(ti89_recv_EOT());
	TRYF(ti89_send_ACK());

	return 0;
}

static int		dump_rom	(CalcHandle* handle, CalcDumpSize size, const char *filename)
{
	int i, j;
	uint8_t data;
	time_t start, elapsed, estimated, remaining;
	char buffer[257];
	char tmp[257];
	int pad;
	FILE *f, *file;
	uint16_t checksum, sum;
	int ROMSIZE = (handle->model == CALC_TI89T) || 
  					(handle->model == CALC_V200) ? 4*1024 : 2*1024;
	FileContent content;

	// Copies ROM dump program into a file
	f = fopen(DUMP_ROM89_FILE, "wb");
	if(f == NULL)
		return ERR_FILE_OPEN;

	fwrite(romDump89, sizeof(unsigned char), romDumpSize89, f);
	fclose(f);

	// Transfer program to calc
	tifiles_file_read_regular(DUMP_ROM89_FILE, &content);
	TRYF(send_var(handle, MODE_SEND_ONE_VAR, &content));
	tifiles_content_free_regular(&content);
	unlink(DUMP_ROM89_FILE);

	// Launch calculator program by remote control
	sprintf(handle->updat->text, _("Launching..."));
	handle->updat->label();

	TRYF(send_key(handle, KEY89_CLEAR));
	PAUSE(50);
    TRYF(send_key(handle, KEY89_CLEAR));
    PAUSE(50);
    TRYF(send_key(handle, 'm'));
    TRYF(send_key(handle, 'a'));
    TRYF(send_key(handle, 'i'));
    TRYF(send_key(handle, 'n'));
    TRYF(send_key(handle, '\\'));
    TRYF(send_key(handle, 'r'));
    TRYF(send_key(handle, 'o'));
    TRYF(send_key(handle, 'm'));
    TRYF(send_key(handle, 'd'));
    TRYF(send_key(handle, 'u'));
    TRYF(send_key(handle, 'm'));
    TRYF(send_key(handle, 'p'));
    TRYF(send_key(handle, KEY89_LP));
    TRYF(send_key(handle, KEY89_RP));
    TRYF(send_key(handle, KEY89_ENTER));

	// Open file
	file = fopen(filename, "wb");
	if(file == NULL)
		return ERR_OPEN_FILE;

	// Receive it now blocks per blocks (1024 + CHK)
	sprintf(handle->updat->text, _("Receiving..."));
	handle->updat->label();

	start = time(NULL);
	handle->updat->max1 = 1024;
	handle->updat->max2 = 1024 * size;

	for(i = 0; i < ROMSIZE; i++) 
	{
		sum = 0;
		for(j = 0; j < 1024; j++) 
		{
			TRYF(ticables_cable_get(handle->cable, &data));
			fprintf(file, "%c", data);
			sum += data;

			handle->updat->cnt1 = j;
			handle->updat->pbar();
			if(handle->updat->cancel)
				return -1;
		}

		TRYF(ticables_cable_get(handle->cable, &data));
		checksum = data << 8;
		TRYF(ticables_cable_get(handle->cable, &data));
		checksum |= data;
		if(sum != checksum)
		return ERR_CHECKSUM;
		TRYF(ticables_cable_put(handle->cable, 0xDA));

		handle->updat->cnt2 = i;
		if(handle->updat->cancel)
			return -1;

		elapsed = (long) difftime(time(NULL), start);
		estimated = (long) (elapsed * (float) (ROMSIZE) / i);
		remaining = (long) difftime(estimated, elapsed);
		sprintf(buffer, "%s", ctime(&remaining));
		sscanf(buffer, "%3s %3s %i %s %i", tmp, tmp, &pad, tmp, &pad);
		sprintf(handle->updat->text, _("Remaining (mm:ss): %s"), tmp + 3);
		handle->updat->label();
	}

	// make ROM dumping program exit.
	TRYF(ticables_cable_put(handle->cable, 0xCC));
	fclose(file);

	return 0;
}

static int		set_clock	(CalcHandle* handle, CalcClock* clock)
{
    uint8_t buffer[16] = { 0 };
    uint16_t status;

    buffer[6] = clock->year >> 8;
    buffer[7] = clock->year & 0x00ff;
    buffer[8] = clock->month;
    buffer[9] = clock->day;
    buffer[10] = clock->hours;
    buffer[11] = clock->minutes;
    buffer[12] = clock->seconds;
    buffer[13] = clock->date_format;
    buffer[14] = clock->time_format;
    buffer[15] = 0xff;

    sprintf(update->text, _("Setting clock..."));
    update_label();

    TRYF(ti89_send_RTS(0x10, TI89_CLK, "Clock"));
    TRYF(ti89_recv_ACK(NULL));

    TRYF(ti89_recv_CTS());
    TRYF(ti89_send_ACK());

    TRYF(ti89_send_XDP(0x10, buffer));
    TRYF(ti89_recv_ACK(NULL));

    TRYF(ti89_send_EOT());
    TRYF(ti89_recv_ACK(&status));

	return 0;
}

static int		get_clock	(CalcHandle* handle, CalcClock* clock)
{
	uint32_t varsize;
    uint8_t vartype;
    uint8_t varname[9];
    uint8_t buffer[32];

    sprintf(update->text, _("Getting clock..."));
    update_label();

    TRYF(ti89_send_REQ(0x0000, TI89_CLK, "Clock"));
    TRYF(ti89_recv_ACK(NULL));

    TRYF(ti89_recv_VAR(&varsize, &vartype, varname));
    TRYF(ti89_send_ACK());

    TRYF(ti89_send_CTS());
    TRYF(ti89_recv_ACK(NULL));

    TRYF(ti89_recv_XDP(&varsize, buffer));
    TRYF(ti89_send_ACK());

    TRYF(ti89_recv_EOT());
    TRYF(ti89_send_ACK());

    clock->year = (buffer[6] << 8) | buffer[7];
    clock->month = buffer[8];
    clock->day = buffer[9];
    clock->hours = buffer[10];
    clock->minutes = buffer[11];
    clock->seconds = buffer[12];
    clock->date_format = buffer[13];
    clock->time_format = buffer[14];

	return 0;
}

const CalcFncts calc_89 = 
{
	CALC_TI89,
	"TI89",
	N_("TI-89"),
	N_("TI-89"),
	OPS_ISREADY | OPS_KEYS | OPS_SCREEN | OPS_DIRLIST | OPS_BACKUP | OPS_VARS | 
	OPS_FLASH | OPS_IDLIST | OPS_CLOCK | OPS_ROMDUMP |
	FTS_SILENT | FTS_FOLDER | FTS_FLASH,
	&is_ready,
	&send_key,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&recv_backup,
	&send_var,
	&recv_var,
	&del_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&recv_idlist,
	&dump_rom,
	&set_clock,
	&get_clock,
};

const CalcFncts calc_92p = 
{
	CALC_TI92P,
	"TI92+",
	N_("TI-92 Plus"),
	N_("TI-92 Plus"),
	OPS_ISREADY | OPS_KEYS | OPS_SCREEN | OPS_DIRLIST | OPS_BACKUP | OPS_VARS | 
	OPS_FLASH | OPS_IDLIST | OPS_CLOCK | OPS_ROMDUMP |
	FTS_SILENT | FTS_FOLDER | FTS_FLASH,
	&is_ready,
	&send_key,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&recv_backup,
	&send_var,
	&recv_var,
	&del_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&recv_idlist,
	&dump_rom,
	&set_clock,
	&get_clock,
};

const CalcFncts calc_89t = 
{
	CALC_TI89T,
	"TI89t",
	N_("TI-89 Titanium"),
	N_("TI-89 Titanium"),
	OPS_ISREADY | OPS_KEYS | OPS_SCREEN | OPS_DIRLIST | OPS_BACKUP | OPS_VARS | 
	OPS_FLASH | OPS_IDLIST | OPS_CLOCK | OPS_ROMDUMP |
	FTS_SILENT | FTS_FOLDER | FTS_FLASH,
	&is_ready,
	&send_key,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&recv_backup,
	&send_var,
	&recv_var,
	&del_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&recv_idlist,
	&dump_rom,
	&set_clock,
	&get_clock,
};

const CalcFncts calc_v2 = 
{
	CALC_V200,
	"V200",
	N_("V200PLT"),
	N_("V200 Portable Learning Tool"),
	OPS_ISREADY | OPS_KEYS | OPS_SCREEN | OPS_DIRLIST | OPS_BACKUP | OPS_VARS | 
	OPS_FLASH | OPS_IDLIST | OPS_CLOCK | OPS_ROMDUMP |
	FTS_SILENT | FTS_FOLDER | FTS_FLASH,
	&is_ready,
	&send_key,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&recv_backup,
	&send_var,
	&recv_var,
	&del_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&recv_idlist,
	&dump_rom,
	&set_clock,
	&get_clock,
};
