/*
 * Copyright (C) 2015 Scality SA - http://www.scality.com
 *
 * This file is part of ScalityRestBlock.
 *
 * ScalityRestBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ScalityRestBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __SRB_H__
#define __SRB_H__

#define SRB_DEV_SECTORSIZE (1 * 1024 * 1024)
/* This defines the max_hw_sectors_kb value
 * Unit is a sector (512B)
 */
#define SRB_DEV_NB_PHYS_SEGS (512 * 4 * 32)

#endif
