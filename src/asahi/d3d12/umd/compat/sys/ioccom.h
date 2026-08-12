/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Mesa's vendored drm.h selects the BSD ioctl header on non-Linux targets.
 * The native Windows AGX driver only consumes drm_fourcc format and modifier
 * definitions; it never issues a DRM ioctl.  Provide the encoding macros so
 * that those UAPI declarations remain parseable without adding a Unix ioctl
 * dependency to the Windows UMD.
 */
#ifndef D12_UMD_COMPAT_SYS_IOCCOM_H
#define D12_UMD_COMPAT_SYS_IOCCOM_H

#define D12_UMD_IOC_GROUP_SHIFT 8U
#define D12_UMD_IOC_ENCODE(group, number)                                     \
   ((((unsigned long)(unsigned char)(group)) << D12_UMD_IOC_GROUP_SHIFT) |    \
    (unsigned long)(unsigned char)(number))

#define _IO(group, number) D12_UMD_IOC_ENCODE(group, number)
#define _IOR(group, number, type) D12_UMD_IOC_ENCODE(group, number)
#define _IOW(group, number, type) D12_UMD_IOC_ENCODE(group, number)
#define _IOWR(group, number, type) D12_UMD_IOC_ENCODE(group, number)

#endif
