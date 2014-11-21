/*
 * Copyright (C) 2014 SCALITY SA - http://www.scality.com
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

#ifndef __SRBLOCK_LOG_H__
# define __SRBLOCK_LOG_H__

/*
 * Standard Kernel value for log level
 */
#define SRB_DEBUG		7
#define SRB_INFO		6
#define SRB_NOTICE		5
#define SRB_WARNING		4
#define SRB_ERR			3
#define SRB_CRIT		2
#define SRB_ALERT		1
#define SRB_EMERG		0

#define SRB_LVLSTR_DEBUG	"DEBUG"
#define SRB_LVLSTR_INFO		"INFO"
#define SRB_LVLSTR_NOTICE	"NOTICE"
#define SRB_LVLSTR_WARNING	"WARNING"
#define SRB_LVLSTR_ERR		"ERROR"
#define SRB_LVLSTR_CRIT		"CRITICAL"
#define SRB_LVLSTR_ALERT	"ALERT"
#define SRB_LVLSTR_EMERG	"EMERGENCY"

/* 
 * LOGGING macros.
 */

/*
 * Defines a harmonized LOG format (might seem complex but it should help
 * changing the formats in an easier fashion).
 */
#define SRB_LOGFMT_LVL(lvl)	"[" SRB_LVLSTR_##lvl "]"
#define SRB_LOGFMT_NAME		DEV_NAME ": "
#define SRB_LOGFMT_DEVICE	"device %i: "
#define SRB_LOGARG_DEVICE(dev)  (dev)->major
#define SRB_LOGFMT_MODULE	"%s: "
#define SRB_LOGARG_MODULE(dbg)  dbg ? (dbg)->name : NULL
#define SRB_LOGFMT_DBG		"@%s(l.%i): "
#define SRB_LOGARG_DBG		__func__, __LINE__

#define SRB_FMT_BASE(lvl)		SRB_LOGFMT_LVL(lvl) " " SRB_LOGFMT_NAME
#define SRB_FMT_END(fmt)		fmt "\n"
#define SRB_FMT_MINIMAL(lvl, fmt)	SRB_FMT_BASE(lvl)                                                    SRB_FMT_END(fmt)
#define SRB_FMT_DEV(lvl, fmt)		SRB_FMT_BASE(lvl) SRB_LOGFMT_DEVICE                                  SRB_FMT_END(fmt)
#define SRB_FMT_NODEV(lvl, fmt)		SRB_FMT_BASE(lvl)                   SRB_LOGFMT_MODULE SRB_LOGFMT_DBG SRB_FMT_END(fmt)
#define SRB_FMT_MOD(lvl, fmt)		SRB_FMT_BASE(lvl)                   SRB_LOGFMT_MODULE                SRB_FMT_END(fmt)
#define SRB_FMT_NOMOD(lvl, fmt)		SRB_FMT_BASE(lvl) SRB_LOGFMT_DEVICE                   SRB_LOGFMT_DBG SRB_FMT_END(fmt)
#define SRB_FMT_DBG(lvl, fmt)		SRB_FMT_BASE(lvl)				      SRB_LOGFMT_DBG SRB_FMT_END(fmt)
#define SRB_FMT_NODBG(lvl, fmt)		SRB_FMT_BASE(lvl) SRB_LOGFMT_DEVICE SRB_LOGFMT_MODULE                SRB_FMT_END(fmt)
#define SRB_FMT_FULL(lvl, fmt)		SRB_FMT_BASE(lvl) SRB_LOGFMT_DEVICE SRB_LOGFMT_MODULE SRB_LOGFMT_DBG SRB_FMT_END(fmt)

#define SRB_ARGS_DEV(dev)		SRB_LOGARG_DEVICE(dev)
#define SRB_ARGS_NODEV(dbg)		                        SRB_LOGARG_MODULE(dbg), SRB_LOGARG_DBG
#define SRB_ARGS_MOD(dbg)		                        SRB_LOGARG_MODULE(dbg)
#define SRB_ARGS_NOMOD(dev)		SRB_LOGARG_DEVICE(dev),                         SRB_LOGARG_DBG
#define SRB_ARGS_DBG			                                                SRB_LOGARG_DBG
#define SRB_ARGS_NODBG(dev, dbg)	SRB_LOGARG_DEVICE(dev), SRB_LOGARG_MODULE(dbg)
#define SRB_ARGS_FULL(dev, dbg)		SRB_LOGARG_DEVICE(dev), SRB_LOGARG_MODULE(dbg), SRB_LOGARG_DBG

/*
 * This macro might seem quite complex at first,
 * but the aim is to ease the writing of all the upper layers of logging macros;
 * So this one ends up managing every single case of not-loggable components.
 */
#define SRBDEV_INTERNAL_DBG(lvl, dev, dbg, fmt, args...)\
	do {\
		if (dev == NULL && dbg == NULL && SRB_##lvl != SRB_DEBUG)\
			printk(KERN_##lvl SRB_FMT_MINIMAL(lvl, fmt), ##args);\
		else if (dev == NULL && dbg == NULL)\
			printk(KERN_##lvl SRB_FMT_DBG(lvl, fmt), SRB_ARGS_DBG, ##args);\
		else if (dev == NULL && SRB_##lvl != SRB_DEBUG)\
			printk(KERN_##lvl SRB_FMT_MOD(lvl, fmt), SRB_ARGS_MOD(dbg), ##args);\
		else if (dbg == NULL && SRB_##lvl != SRB_DEBUG)\
			printk(KERN_##lvl SRB_FMT_DEV(lvl, fmt), SRB_ARGS_DEV(dev), ##args);\
		else if (dev == NULL)\
			printk(KERN_##lvl SRB_FMT_NODEV(lvl, fmt), SRB_ARGS_NODEV(dbg), ##args);\
		else if (dbg == NULL)\
			printk(KERN_##lvl SRB_FMT_NOMOD(lvl, fmt), SRB_ARGS_NOMOD(dev), ##args);\
		else if (SRB_##lvl != SRB_DEBUG)\
			printk(KERN_##lvl SRB_FMT_NODBG(lvl, fmt), SRB_ARGS_NODBG(dev, dbg), ##args);\
		else\
			printk(KERN_##lvl SRB_FMT_FULL(lvl, fmt), SRB_ARGS_FULL(dev, dbg), ##args);\
	} while (0);

#define SRB_INTERNAL_DBG(lvl, fmt, args...) \
	do {\
		srb_device_t *dev = NULL;\
		srb_debug_t *dbg = NULL;\
		(void)dbg;\
		(void)dev;\
		SRBDEV_INTERNAL_DBG(lvl, dev, dbg, fmt, ##args);\
	} while (0)

#define SRB_LOG_DEBUG(level, fmt, args...) \
	if (level >= SRB_DEBUG) SRB_INTERNAL_DBG(DEBUG, fmt, ##args)
#define SRB_LOG_INFO(level, fmt, args...) \
	if (level >= SRB_INFO) SRB_INTERNAL_DBG(INFO, fmt, ##args)
#define SRB_LOG_NOTICE(level, fmt, args...) \
	if (level >= SRB_NOTICE) SRB_INTERNAL_DBG(NOTICE, fmt, ##args)
#define SRB_LOG_WARN(level, fmt, args...) \
	if (level >= SRB_WARNING) SRB_INTERNAL_DBG(WARNING, fmt, ##args)
#define SRB_LOG_ERR(level, fmt, args...) \
	if (level >= SRB_ERR) SRB_INTERNAL_DBG(ERR, fmt, ##args)
#define SRB_LOG_CRIT(level, fmt, args...) \
	if (level >= SRB_CRIT) SRB_INTERNAL_DBG(CRIT, fmt, ##args)
#define SRB_LOG_ALERT(level, fmt, args...) \
	if (level >= SRB_ALERT) SRB_INTERNAL_DBG(ALERT, fmt, ##args)
#define SRB_LOG_EMERG(level, fmt, args...) \
	if (level >= SRB_EMERG) SRB_INTERNAL_DBG(EMERG, fmt, ##args)

#define SRBDEV_LOG_DEBUG(level, fmt, a...) \
	if (level >= SRB_DEBUG) SRBDEV_INTERNAL_DBG(DEBUG, dbg, fmt, ##a)
#define SRBDEV_LOG_INFO(level, fmt, a...) \
	if (level >= SRB_INFO) SRBDEV_INTERNAL_DBG(INFO, dbg, fmt, ##a)
#define SRBDEV_LOG_NOTICE(level, fmt, a...) \
	if (level >= SRB_NOTICE) SRBDEV_INTERNAL_DBG(NOTICE, dbg, fmt, ##a)
#define SRBDEV_LOG_WARN(level, fmt, a...) \
	if (level >= SRB_WARNING) SRBDEV_INTERNAL_DBG(WARNING, dbg, fmt, ##a)
#define SRBDEV_LOG_ERR(level, fmt, a...) \
	if (level >= SRB_ERR) SRBDEV_INTERNAL_DBG(ERR, dbg, fmt, ##a)
#define SRBDEV_LOG_CRIT(level, fmt, a...) \
	if (level >= SRB_CRIT) SRBDEV_INTERNAL_DBG(CRIT, dbg, fmt, ##a)
#define SRBDEV_LOG_ALERT(level, fmt, a...) \
	if (level >= SRB_ALERT) SRBDEV_INTERNAL_DBG(ALERT, dbg, fmt, ##a)
#define SRBDEV_LOG_EMERG(level, fmt, a...) \
	if (level >= SRB_EMERG) SRBDEV_INTERNAL_DBG(EMERG, dbg, fmt, ##a)


/*
#define SRB_INFO(fmt, a...) \
	printk(KERN_INFO "srb: " fmt "\n" , ##a)

#define SRB_ERROR(fmt, a...) \
	printk(KERN_ERR "srb: " fmt "\n" , ##a)
*/

#endif /* ! __SRBLOCK_LOG_H__ */
