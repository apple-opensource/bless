/*
 * Copyright (c) 2001-2005 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 *  BLSetOpenFirmwareBootDeviceForMountPoint.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Tue Apr 17 2001.
 *  Copyright (c) 2001-2005 Apple Computer, Inc. All rights reserved.
 *
 *  $Id: BLSetOpenFirmwareBootDeviceForMountPoint.c,v 1.14 2006/02/20 22:49:57 ssen Exp $
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "bless.h"
#include "bless_private.h"

#define NVRAM "/usr/sbin/nvram"

int BLSetOpenFirmwareBootDeviceForMountPoint(BLContextPtr context, const char * mountpoint) {
    char           mntfrm[MAXPATHLEN];
    int err;
    struct stat sb;

    err = stat(mountpoint, &sb);
    if(err) {
      contextprintf(context, kBLLogLevelError,  "Can't stat mount point %s\n", mountpoint );
      return 1;
    }

    if(devname(sb.st_dev, S_IFBLK) == NULL) {
            return 2;
    }

    snprintf(mntfrm, MAXPATHLEN, "/dev/%s", devname(sb.st_dev, S_IFBLK));
    return BLSetOpenFirmwareBootDevice(context, mntfrm);
}
    
