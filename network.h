/*
 * lionfs, The Link Over Network File System
 * Copyright (C) 2016  Ricardo Biehl Pasquali <rbpoficial@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

size_t
network_file_get_data(char*, size_t, long long, void*);

int
network_file_get_valid(char*);

int
network_file_get_info(char*, lionfile_info_t*);

/* General network functions */

int
network_open_module(const char*);

void
network_open_all_modules(void);

int
network_close_module(const char*);

void
network_close_all_modules(void);

void
network_init(void);
