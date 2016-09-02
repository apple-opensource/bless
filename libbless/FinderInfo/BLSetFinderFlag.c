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
 *  getFinderFlag.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Thu Jul 05 2001.
 *  Copyright (c) 2001-2005 Apple Computer, Inc. All rights reserved.
 *
 *  $Id: BLSetFinderFlag.c,v 1.16 2005/08/22 20:49:22 ssen Exp $
 *
 *  $Log: BLSetFinderFlag.c,v $
 *  Revision 1.16  2005/08/22 20:49:22  ssen
 *  Change functions to take "char *foo" instead of "char foo[]".
 *  It should be semantically identical, and be more consistent with
 *  other system APIs
 *
 *  Revision 1.15  2005/06/24 16:39:49  ssen
 *  Don't use "unsigned char[]" for paths. If regular char*s are
 *  good enough for the BSD system calls, they're good enough for
 *  bless.
 *
 *  Revision 1.14  2005/02/03 00:42:25  ssen
 *  Update copyrights to 2005
 *
 *  Revision 1.13  2004/04/20 21:40:41  ssen
 *  Update copyrights to 2004
 *
 *  Revision 1.12  2003/10/16 23:50:05  ssen
 *  Partially finish cleanup of headers to add "const" to char[] arguments
 *  that won't be modified.
 *
 *  Revision 1.11  2003/07/22 15:58:30  ssen
 *  APSL 2.0
 *
 *  Revision 1.10  2003/04/19 00:11:05  ssen
 *  Update to APSL 1.2
 *
 *  Revision 1.9  2003/04/16 23:57:30  ssen
 *  Update Copyrights
 *
 *  Revision 1.8  2003/03/20 03:40:53  ssen
 *  Merge in from PR-3202649
 *
 *  Revision 1.7.2.1  2003/03/20 02:10:49  ssen
 *  swap integers to BE for on-disk representation
 *
 *  Revision 1.7  2003/03/19 22:56:58  ssen
 *  C99 types
 *
 *  Revision 1.6  2002/06/11 00:50:40  ssen
 *  All function prototypes need to use BLContextPtr. This is really
 *  a minor change in all of the files.
 *
 *  Revision 1.5  2002/04/27 17:54:58  ssen
 *  Rewrite output logic to format the string before sending of to logger
 *
 *  Revision 1.4  2002/04/25 07:27:26  ssen
 *  Go back to using errorprint and verboseprint inside library
 *
 *  Revision 1.3  2002/02/23 04:13:05  ssen
 *  Update to context-based API
 *
 *  Revision 1.2  2001/11/17 05:44:02  ssen
 *  fileinfobuf is 8 words
 *
 *  Revision 1.1  2001/11/16 05:36:46  ssen
 *  Add libbless files
 *
 *  Revision 1.6  2001/11/11 06:20:59  ssen
 *  readding files
 *
 *  Revision 1.4  2001/10/26 04:19:41  ssen
 *  Add dollar Id and dollar Log
 *
 *
 */

#include <CoreFoundation/CoreFoundation.h>

#include <sys/types.h>
#include <sys/attr.h>
#include <unistd.h>

#include "bless.h"
#include "bless_private.h"

struct TwoUInt16 {
    uint16_t first;
    uint16_t second;
};

struct fileinfobuf {
  uint32_t info_length;
  uint32_t finderinfo[8];
}; 


int BLSetFinderFlag(BLContextPtr context, const char * path, uint16_t flag, int setval) {
    struct attrlist		alist;
    struct fileinfobuf finfo;
    struct TwoUInt16 *twoUint = (struct TwoUInt16 *)&finfo.finderinfo[2];
    int err;
	
    alist.bitmapcount = 5;
    alist.reserved = 0;
    alist.commonattr = ATTR_CMN_FNDRINFO;
    alist.volattr = 0;
    alist.dirattr = 0;
    alist.fileattr = 0;
    alist.forkattr = 0;

	err = getattrlist(path, &alist, &finfo, sizeof(finfo), 0);
    if(err) {
        contextprintf(context, kBLLogLevelError,  "Can't file information for %s\n", path );
        return 1;
    }

    if(setval) {
        /* we want to set the bit. so OR with the flag  */
        twoUint->first |= CFSwapInt16HostToBig(flag);
    } else {
        /* AND with a mask  that excludes flag*/
        uint16_t mask = CFSwapInt16HostToBig(flag);;
	mask = ~mask;
        twoUint->first &= mask;
    }
    
	err = setattrlist(path, &alist, &finfo.finderinfo, sizeof(finfo.finderinfo), 0);
    if(err) {
        contextprintf(context, kBLLogLevelError,  "Error while setting file information for %s\n", path );
        return 2;
    }

    return 0;
}
