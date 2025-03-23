/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 MediaTek Inc.
 */

// to avoid to inlcude more unnecessary module in,
// copy the follow lines form fs/f2fs.h.
// Those defines is used in trace/event/f2fs.h
#ifndef _FAKE_F2FS_H
#define _FAKE_F2FS_H
typedef u32 block_t;
typedef u32 nid_t;

#define	CP_UMOUNT   0x00000001
#define	CP_FASTBOOT 0x00000002
#define	CP_SYNC     0x00000004
#define	CP_RECOVERY 0x00000008
#define	CP_DISCARD  0x00000010
#define CP_TRIMMED  0x00000020
#define CP_PAUSE    0x00000040
#define CP_RESIZE   0x00000080

#define PAGE_TYPE_OF_BIO(type) ((type) > META ? META : (type))
enum page_type {
	DATA,
	NODE,
	META,
	NR_PAGE_TYPE,
	META_FLUSH,
	INMEM,		/* the below types are used by tracepoints only. */
	INMEM_DROP,
	INMEM_INVALIDATE,
	INMEM_REVOKE,
	IPU,
	OPU,
};

struct victim_sel_policy {
	int alloc_mode;			/* LFS or SSR */
	int gc_mode;			/* GC_CB or GC_GREEDY */
	unsigned long *dirty_segmap;	/* dirty segment bitmap */
	unsigned int max_search;	/* maximum # of segments to search */
	unsigned int offset;		/* last scanned bitmap offset */
	unsigned int ofs_unit;		/* bitmap search unit */
	unsigned int min_cost;		/* minimum cost */
	unsigned int min_segno;		/* segment # having min. cost */
};

#endif //_FAKE_F2FS_H
