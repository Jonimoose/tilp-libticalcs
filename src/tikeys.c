/* Hey EMACS -*- linux-c -*- */
/* $Id$ */

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

#include "calc_def.h"
#include "headers.h"

#include "keys73.h"
#include "keys83p.h"
#include "keys89.h"
#include "keys92p.h"

extern const TicalcKey TI73_KEYS[];
extern const TicalcKey TI83P_KEYS[];
extern const TicalcKey TI89_KEYS[];
extern const TicalcKey TI92P_KEYS[];


TIEXPORT const TicalcKey TICALL ticalc_73_keys(unsigned char ascii_code)
{
  return TI73_KEYS[ascii_code];
}

TIEXPORT const TicalcKey TICALL ticalc_83p_keys(unsigned char ascii_code)
{
  return TI83P_KEYS[ascii_code];
}

TIEXPORT const TicalcKey TICALL ticalc_89_keys(unsigned char ascii_code)
{
  return TI89_KEYS[ascii_code];
}

TIEXPORT const TicalcKey TICALL ticalc_92p_keys(unsigned char ascii_code)
{
  return TI92P_KEYS[ascii_code];
}
