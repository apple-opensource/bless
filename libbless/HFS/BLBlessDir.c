/*
 * Copyright (c) 2001-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
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
 *  BLBlessDir.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Tue Apr 17 2001.
 *  Copyright (c) 2001-2003 Apple Computer, Inc. All rights reserved.
 *
 *  $Id: BLBlessDir.c,v 1.9 2003/07/22 15:58:31 ssen Exp $
 *
 *  $Log: BLBlessDir.c,v $
 *  Revision 1.9  2003/07/22 15:58:31  ssen
 *  APSL 2.0
 *
 *  Revision 1.8  2003/04/19 00:11:08  ssen
 *  Update to APSL 1.2
 *
 *  Revision 1.7  2003/04/16 23:57:31  ssen
 *  Update Copyrights
 *
 *  Revision 1.6  2003/03/19 22:57:02  ssen
 *  C99 types
 *
 *  Revision 1.5  2002/06/11 00:50:42  ssen
 *  All function prototypes need to use BLContextPtr. This is really
 *  a minor change in all of the files.
 *
 *  Revision 1.4  2002/04/27 17:54:59  ssen
 *  Rewrite output logic to format the string before sending of to logger
 *
 *  Revision 1.3  2002/04/25 07:27:27  ssen
 *  Go back to using errorprint and verboseprint inside library
 *
 *  Revision 1.2  2002/02/23 04:13:05  ssen
 *  Update to context-based API
 *
 *  Revision 1.1  2001/11/16 05:36:46  ssen
 *  Add libbless files
 *
 *  Revision 1.8  2001/10/26 04:19:40  ssen
 *  Add dollar Id and dollar Log
 *
 *
 */

#include <sys/types.h>

#include "bless.h"
#include "bless_private.h"


int BLBlessDir(BLContextPtr context, unsigned char mountpoint[], uint32_t dirX, uint32_t dir9, int useX) {

    int err;
    uint32_t finderinfo[8];
    
    err = BLGetVolumeFinderInfo(context, mountpoint, finderinfo);
    if(err) {
        contextprintf(context, kBLLogLevelError,  "Can't get Finder info fields for volume mounted at %s\n", mountpoint );
        return 1;
    }

    /* If either directory was not specified, the dirID
     * variables will be 0, so we can use that to initialize
     * the FI fields */

    /* Set Finder info words 3 & 5 */
    finderinfo[3] = dir9;
    finderinfo[5] = dirX;

    if(!dirX || !useX) {
      /* The 9 folder is what we really want */
      finderinfo[0] = dir9;
    } else {
      /* X */
      finderinfo[0] = dirX;
    }

    contextprintf(context, kBLLogLevelVerbose,  "finderinfo[0] = %d\n", finderinfo[0] );
    contextprintf(context, kBLLogLevelVerbose,  "finderinfo[3] = %d\n", finderinfo[3] );
    contextprintf(context, kBLLogLevelVerbose,  "finderinfo[5] = %d\n", finderinfo[5] );
    
    err = BLSetVolumeFinderInfo(context, mountpoint, finderinfo);
    if(err) {
      contextprintf(context, kBLLogLevelError,  "Can't set Finder info fields for volume mounted at %s\n", mountpoint );
      return 2;
    }

    return 0;
}

