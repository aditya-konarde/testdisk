/*

    File: phcli_auto.h

    Copyright (C) 2025 Modified for non-interactive operation

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */

#ifndef _PHCLI_AUTO_H
#define _PHCLI_AUTO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Non-interactive mode for photorec */
int photorec_auto_mode(int argc, char **argv, struct ph_options *options);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _PHCLI_AUTO_H */