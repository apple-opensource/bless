/*
 * Copyright (c) 2001 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 *  BLLookupFileIDOnMount.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Thu Apr 19 2001.
 *  Copyright (c) 2001 Apple Computer, Int. All rights reserved.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/attr.h>

#include "bless.h"

struct cataloginfo {
  attrreference_t name;
  fsid_t volid;
  fsobj_id_t objectid;
  fsobj_id_t parentid;
  char namebuffer[NAME_MAX];
};

struct cataloginforeturn {
  u_int32_t length;
  struct cataloginfo c;
};

static int lookupIDOnVolID(u_int32_t volid, u_int32_t fileID, unsigned char out[]);

int BLLookupFileIDOnMount(BLContext context, unsigned char mount[], u_int32_t fileID, unsigned char out[]) {
    struct attrlist alist;
    struct cataloginforeturn catinfo;
    int err;

    u_int32_t volid;
    unsigned char relpath[MAXPATHLEN];

    if(fileID < 2) {
        out[0] = '\0';
        return 0;
    }

    alist.bitmapcount = 5;
    alist.commonattr = ATTR_CMN_NAME | ATTR_CMN_FSID | ATTR_CMN_OBJID | ATTR_CMN_PAROBJID;
    alist.volattr = 0;
    alist.dirattr = 0;
    alist.fileattr = 0;
    alist.forkattr = 0;
    
	err = getattrlist(mount, &alist, &catinfo, sizeof(catinfo), 0);
    if(err) {
        return 1;
    }

    volid = (u_int32_t)catinfo.c.volid.val[0];

    err = lookupIDOnVolID(volid, fileID, relpath);
    if(err) {
        return 2;
    }

    if(strcmp(mount, "/")) {
        /* If the mount point is not '/', prefix by mount */
        snprintf(out, MAXPATHLEN, "%s/%s", mount, relpath);
    } else {
        snprintf(out, MAXPATHLEN, "/%s", relpath);
    }

    return 0;
}

static int lookupIDOnVolID(u_int32_t volid, u_int32_t fileID, unsigned char out[]) {

    unsigned char *bp;

    u_int32_t dirID = fileID; /* to initialize loop */
    unsigned char volpath[MAXPATHLEN];

    struct attrlist alist;
    struct cataloginforeturn catinfo;
    int err;

    out[0] = '\0';

    if(fileID <= 2) {
        return 0;
    }

    /* Now for the recursive step
     * 1. getattr on /.vol/volid/dirID
     * 2. get the name.
     * 3. set dirID = parentID
     * 4. go to 1)
     * 5. exit when dirID == 2
    */


    /* bp will hold our current position. Work from the end
     * of the buffer until the beginning */
    bp = &(out[MAXPATHLEN-1]);
    *bp = '\0';

    while(dirID != 2) {
        char *nameptr;
        int namelen;
        sprintf(volpath, "/.vol/%u/%u", volid, dirID);
        alist.bitmapcount = 5;
        alist.commonattr = ATTR_CMN_NAME | ATTR_CMN_FSID | ATTR_CMN_OBJID | ATTR_CMN_PAROBJID;
        alist.volattr = 0;
        alist.dirattr = 0;
        alist.fileattr = 0;
        alist.forkattr = 0;
        
        err = getattrlist(volpath, &alist, &catinfo, sizeof(catinfo), 0);
        if (err) {
            return 3;
        }

        dirID = (u_int32_t)catinfo.c.parentid.fid_objno;
        nameptr = (char *)(&catinfo.c.name) + catinfo.c.name.attr_dataoffset;
        namelen = strlen(nameptr); /* move bp by this many and copy */
        bp -= namelen;
        strncpy(bp, nameptr, namelen); /* ignore trailing \0 */
        bp--;
        *bp = '/';
    }

    bp++; /* Don't want a '/' prefix, relative path! */
    memmove(out, bp, strlen(bp)+1);
    return 0;
}






