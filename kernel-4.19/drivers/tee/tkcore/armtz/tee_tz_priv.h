/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2015-2019 TrustKernel Incorporated
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __TEE_TZ_PRIV__
#define __TEE_TZ_PRIV__

#include <linux/tee_kernel_lowlevel_api.h>

struct tee;
struct shm_pool;
struct tee_rpc_bf;

struct tee_tz {
	bool started;
	struct tee *tee;

	unsigned long shm_paddr;
	void *shm_vaddr;
	bool shm_cached;
	struct shm_pool *shm_pool;

	void *log_buffer;
	size_t log_buffer_size;

	struct tee_rpc_bf *rpc_buffers;

	struct tee_wait_queue_private wait_queue;
};

#endif /* __TEE_TZ_PRIV__ */
