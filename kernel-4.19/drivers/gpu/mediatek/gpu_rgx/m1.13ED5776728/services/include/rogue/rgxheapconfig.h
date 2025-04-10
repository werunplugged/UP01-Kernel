/*************************************************************************/ /*!
@File
@Title          device configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Memory heaps device specific configuration
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef RGXHEAPCONFIG_H
#define RGXHEAPCONFIG_H

#include "rgxdefs_km.h"

/*
	RGX Device Virtual Address Space Definitions

		RGX_PDSCODEDATA_HEAP_BASE and RGX_USCCODE_HEAP_BASE will be programmed,
		on a global basis, into RGX_CR_PDS_EXEC_BASE and RGX_CR_USC_CODE_BASE_*
		respectively. Therefore if clients use multiple configs they must still
		be consistent with their definitions for these heaps.

		Shared virtual memory (GENERAL_SVM) support requires half of the address
		space be reserved for SVM allocations unless BRN fixes are required in
		which case the SVM heap is disabled. This is reflected in the device
		connection capability bits returned to userspace.

		Variable page-size heap (GENERAL_NON4K) support reserves 64GiB from the
		available 4K page-size heap (GENERAL) space. The actual heap page-size
		defaults to 16K; AppHint PVRSRV_APPHINT_GENERALNON4KHEAPPAGESIZE
		can be used to forced it to these values: 4K,64K,256K,1M,2M.

		Heaps must not start at 0x0000000000, as this is reserved for internal
		use within device memory layer.
*/

	/* Start at 32 KiB Size of 512 GiB less 32 KiB (managed by OS/Services) */
	#define RGX_GENERAL_SVM_HEAP_BASE           IMG_UINT64_C(0x0000008000)
	#define RGX_GENERAL_SVM_HEAP_SIZE			IMG_UINT64_C(0x7FFFFF8000)

	/* Start at 512GiB. Size of 255 GiB */
	#define RGX_GENERAL_HEAP_BASE				IMG_UINT64_C(0x8000000000)
	#define RGX_GENERAL_HEAP_SIZE				IMG_UINT64_C(0x3FC0000000)

	/* Start at 767GiB. Size of 1 GiB */
	#define RGX_VK_CAPT_REPLAY_BUF_HEAP_BASE	IMG_UINT64_C(0xBFC0000000)
	#define RGX_VK_CAPT_REPLAY_BUF_HEAP_SIZE	IMG_UINT64_C(0x0040000000)

	/* HWBRN65273 workaround requires General Heap to use a unique single 1GB PCE entry. */
	#define RGX_GENERAL_BRN_65273_HEAP_BASE		IMG_UINT64_C(0x65C0000000)
	#define RGX_GENERAL_BRN_65273_HEAP_SIZE		IMG_UINT64_C(0x0080000000)

	/* Start at 768GiB. Size of 64 GiB */
	#define RGX_GENERAL_NON4K_HEAP_BASE			IMG_UINT64_C(0xC000000000)
	#define RGX_GENERAL_NON4K_HEAP_SIZE			IMG_UINT64_C(0x1000000000)

	/* HWBRN65273 workaround requires Non4K memory to use a unique single 1GB PCE entry. */
	#define RGX_GENERAL_NON4K_BRN_65273_HEAP_BASE	IMG_UINT64_C(0x73C0000000)
	#define RGX_GENERAL_NON4K_BRN_65273_HEAP_SIZE	IMG_UINT64_C(0x0080000000)

	/* Start at 832 GiB. Size of 32 GiB */
	#define RGX_BIF_TILING_NUM_HEAPS			4
	#define RGX_BIF_TILING_HEAP_SIZE			IMG_UINT64_C(0x0200000000)
	#define RGX_BIF_TILING_HEAP_1_BASE			IMG_UINT64_C(0xD000000000)
	#define RGX_BIF_TILING_HEAP_2_BASE			(RGX_BIF_TILING_HEAP_1_BASE + RGX_BIF_TILING_HEAP_SIZE)
	#define RGX_BIF_TILING_HEAP_3_BASE			(RGX_BIF_TILING_HEAP_2_BASE + RGX_BIF_TILING_HEAP_SIZE)
	#define RGX_BIF_TILING_HEAP_4_BASE			(RGX_BIF_TILING_HEAP_3_BASE + RGX_BIF_TILING_HEAP_SIZE)

	/* Start at 872 GiB. Size of 4 GiB */
	#define RGX_PDSCODEDATA_HEAP_BASE			IMG_UINT64_C(0xDA00000000)
	#define RGX_PDSCODEDATA_HEAP_SIZE			IMG_UINT64_C(0x0100000000)

	/* HWBRN65273 workaround requires PDS memory to use a unique single 1GB PCE entry. */
	#define RGX_PDSCODEDATA_BRN_65273_HEAP_BASE	IMG_UINT64_C(0xA800000000)
	#define RGX_PDSCODEDATA_BRN_65273_HEAP_SIZE	IMG_UINT64_C(0x0040000000)

	/* HWBRN63142 workaround requires Region Header memory to be at the top
	   of a 16GB aligned range. This is so when masked with 0x03FFFFFFFF the
	   address will avoid aliasing PB addresses. Start at 879.75GB. Size of 256MB. */
	#define RGX_RGNHDR_BRN_63142_HEAP_BASE		IMG_UINT64_C(0xDBF0000000)
	#define RGX_RGNHDR_BRN_63142_HEAP_SIZE		IMG_UINT64_C(0x0010000000)

	/* Start at 880 GiB, Size of 1 MiB */
	#define RGX_VISTEST_HEAP_BASE				IMG_UINT64_C(0xDC00000000)
	#define RGX_VISTEST_HEAP_SIZE				IMG_UINT64_C(0x0000100000)

	/* HWBRN65273 workaround requires VisTest memory to use a unique single 1GB PCE entry. */
	#define RGX_VISTEST_BRN_65273_HEAP_BASE		IMG_UINT64_C(0xE400000000)
	#define RGX_VISTEST_BRN_65273_HEAP_SIZE		IMG_UINT64_C(0x0000100000)

	/* Start at 896 GiB Size of 4 GiB */
	#define RGX_USCCODE_HEAP_BASE				IMG_UINT64_C(0xE000000000)
	#define RGX_USCCODE_HEAP_SIZE				IMG_UINT64_C(0x0100000000)

	/* HWBRN65273 workaround requires USC memory to use a unique single 1GB PCE entry. */
	#define RGX_USCCODE_BRN_65273_HEAP_BASE		IMG_UINT64_C(0xBA00000000)
	#define RGX_USCCODE_BRN_65273_HEAP_SIZE		IMG_UINT64_C(0x0040000000)

	/* Start at 903GiB. Firmware heaps defined in rgxdefs_km.h
		RGX_FIRMWARE_RAW_HEAP_BASE
		RGX_FIRMWARE_HOST_MAIN_HEAP_BASE
		RGX_FIRMWARE_GUEST_MAIN_HEAP_BASE
		RGX_FIRMWARE_MAIN_HEAP_SIZE
		RGX_FIRMWARE_CONFIG_HEAP_SIZE
		RGX_FIRMWARE_RAW_HEAP_SIZE */

	/* HWBRN65273 workaround requires TQ memory to start at 64kB and use a unique single 0.99GB PCE entry. */
	#define RGX_TQ3DPARAMETERS_BRN_65273_HEAP_BASE		IMG_UINT64_C(0x0000010000)
	#define RGX_TQ3DPARAMETERS_BRN_65273_HEAP_SIZE		IMG_UINT64_C(0x003FFF0000)

	/* Start at 912GiB. Size of 16 GiB. 16GB aligned to match RGX_CR_ISP_PIXEL_BASE */
	#define RGX_TQ3DPARAMETERS_HEAP_BASE		IMG_UINT64_C(0xE400000000)
	#define RGX_TQ3DPARAMETERS_HEAP_SIZE		IMG_UINT64_C(0x0400000000)

	/* Start at 928GiB. Size of 4 GiB */
	#define RGX_DOPPLER_HEAP_BASE				IMG_UINT64_C(0xE800000000)
	#define RGX_DOPPLER_HEAP_SIZE				IMG_UINT64_C(0x0100000000)

	/* Start at 932GiB. Size of 4 GiB */
	#define RGX_DOPPLER_OVERFLOW_HEAP_BASE		IMG_UINT64_C(0xE900000000)
	#define RGX_DOPPLER_OVERFLOW_HEAP_SIZE		IMG_UINT64_C(0x0100000000)

	/* CDM Signals heap (31 signals less one reserved for Services). Start at 936GiB, 960bytes rounded up to 4K */
	#define RGX_SIGNALS_HEAP_BASE				IMG_UINT64_C(0xEA00000000)
	#define RGX_SIGNALS_HEAP_SIZE				IMG_UINT64_C(0x0000001000)

	/* TDM TPU YUV coeffs - can be reduced to a single page */
	#define RGX_TDM_TPU_YUV_COEFFS_HEAP_BASE	IMG_UINT64_C(0xEA00080000)
	#define RGX_TDM_TPU_YUV_COEFFS_HEAP_SIZE	IMG_UINT64_C(0x0000040000)

	/* HWBRN65273 workaround requires two Region Header buffers 4GB apart. */
	#define RGX_MMU_INIA_BRN_65273_HEAP_BASE	IMG_UINT64_C(0xF800000000)
	#define RGX_MMU_INIA_BRN_65273_HEAP_SIZE	IMG_UINT64_C(0x0040000000)
	#define RGX_MMU_INIB_BRN_65273_HEAP_BASE	IMG_UINT64_C(0xF900000000)
	#define RGX_MMU_INIB_BRN_65273_HEAP_SIZE	IMG_UINT64_C(0x0040000000)

	/* Heaps which are barred from using the reserved-region feature (intended for clients
	   of Services), but need the macro definitions are buried here */
	#define RGX_GENERAL_SVM_HEAP_RESERVED_SIZE        0  /* SVM heap is exclusively managed by USER or KERNEL */
	#define RGX_GENERAL_NON4K_HEAP_RESERVED_SIZE      0  /* Non-4K can have page sizes up to 2MB, which is currently
	                                                        not supported in reserved-heap implementation */
	/* ... and heaps which are not used outside of Services */
	#define RGX_RGNHDR_BRN_63142_HEAP_RESERVED_SIZE   0
	#define RGX_MMU_INIA_BRN_65273_HEAP_RESERVED_SIZE 0
	#define RGX_MMU_INIB_BRN_65273_HEAP_RESERVED_SIZE 0
	#define RGX_TQ3DPARAMETERS_HEAP_RESERVED_SIZE     0
	#define RGX_DOPPLER_HEAP_RESERVED_SIZE            0
	#define RGX_DOPPLER_OVERFLOW_HEAP_RESERVED_SIZE   0
	#define RGX_SERVICES_SIGNALS_HEAP_RESERVED_SIZE   0
	#define RGX_TDM_TPU_YUV_COEFFS_HEAP_RESERVED_SIZE 0

#endif /* RGXHEAPCONFIG_H */

/******************************************************************************
 End of file (rgxheapconfig.h)
******************************************************************************/
