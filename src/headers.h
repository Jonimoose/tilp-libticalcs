/*  libticalcs - calculator library, a part of the TiLP project
 *  Copyright (C) 1999-2003  Romain Lievin
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

#ifndef __CALC_HEADERS__
#define __CALC_HEADERS__

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

//#define HAVE_TILP_TICABLES_H

#if defined(HAVE_TILP_TICABLES_H) && !defined(__MACOSX__)
# include <tilp/ticables.h>
#elif defined(HAVE_TILP_TICABLES_H) && defined(__MACOSX__)
# include <libticables/ticables.h>
#else
# include "ticables.h"
# include "intl.h"
#endif

//#define HAVE_TILP_TIFILES_H

#if defined(HAVE_TILP_TIFILES_H) && !defined(__MACOSX__)
# include <tilp/tifiles.h>
# include <tilp/macros.h>
# include <tilp/typesxx.h>
#elif defined(HAVE_TILP_TIFILES_H) && defined(__MACOSX__)
# include <libtifiles/tifiles.h>
# include <libtifiles/macros.h>
#else
# include "tifiles.h"
# include "macros.h"
#endif

#endif
