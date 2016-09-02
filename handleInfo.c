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
 *  handleInfo.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Thu Dec 6 2001.
 *  Copyright (c) 2001-2003 Apple Computer, Inc. All rights reserved.
 *
 *  $Id: handleInfo.c,v 1.20 2003/07/22 15:58:25 ssen Exp $
 *
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "enums.h"
#include "structs.h"

#include "bless.h"

#include <CoreFoundation/CoreFoundation.h>

extern int blesscontextprintf(BLContextPtr context, int loglevel, char const *fmt, ...);


/* 8 words of "finder info" in volume
 * 0 & 1 often set to blessed system folder
 * boot blocks contain name of system file, name of shell app, and startup app
 * starting w/sys8 added file OpenFolderListDF ... which wins open at mount
 * there are per-file/folder "finder info" fields such as invis, location, etc
 * "next-folder in "open folder list" chain ... first item came from word 2
 * 3 & 5 co-opted in the X timeframe to store dirID's of dual-install sysF's 
 * 6 & 7 is the vsdb stuff (64 bits) to see if sysA has seen diskB
 *
 * 0 is blessed system folder
 * 1 is folder which contains startup app (reserved for Finder these days)
 * 2 is first link in linked list of folders to open at mount (deprecated)
 *   (9 and X are supposed to honor this if set and ignore OpenFolderListDF)
 * 3 OS 9 blessed system folder (maybe OS X?)
 * 4 thought to be unused (reserved for Finder, once was PowerTalk Inbox)
 * 5 OS X blessed system folder (maybe OS 9?)
 * 6 & 7 are 64 bit volume identifier (high 32 bits in 6; low in 7)
 */

static const char *messages[7][2] = {

       { "No Blessed System Folder", "Blessed System Folder is " },    /* 0 */
       { "No Startup App folder (ignored anyway)", "Startup App folder is " },
       { "Open-folder linked list empty", "1st dir in open-folder list is " },
       { "No OS 9 + X blessed 9 folder", "OS 9 blessed folder is " },  /* 3 */
       { "Unused field unset", "Thought-to-be-unused field points to " },
       { "No OS 9 + X blessed X folder", "OS X blessed folder is " },  /* 5 */
       { "64-bit VSDB volume id not present", "64-bit VSDB volume id: " }
       
};

int modeInfo(BLContextPtr context, struct clopt commandlineopts[klast], struct clarg actargs[klast]) {
    int err;
    CFDictionaryRef dict;
    extern double blessVersionNumber;

    if(actargs[kversion].present) {
      printf("%.1f\n", blessVersionNumber);
      return 0;
    }

    if(!actargs[kinfo].hasArg ||  actargs[kgetboot].present) {
            char currentString[1024];
            char currentDev[MNAMELEN];
            struct statfs *mnts;
            int vols;

            FILE *pop;
            // we didn't get any volumes, so add them all

            pop = popen("/usr/sbin/nvram boot-device", "r");
            if(pop == NULL) {
                    blesscontextprintf(context, kBLLogLevelError,  "Could not determine current boot device\n" );
                    return 1;
            }

            if(1 != fscanf(pop, "%*s\t%s\n", &(currentString[0]))) {
                    blesscontextprintf(context, kBLLogLevelError,  "Could not parse output from /usr/sbin/nvram\n" );
                    return 1;
            }

            pclose(pop);

            blesscontextprintf(context, kBLLogLevelVerbose,  "Current OF: %s\n", currentString );

            err = BLGetDeviceForOpenFirmwarePath(context, currentString,
                                                currentDev);
            if(err) {
                blesscontextprintf(context, kBLLogLevelError,  "Can't get device for %s: %d\n", currentString, err );
                return 1;

            }

	    if( actargs[kgetboot].present) {
		printf("%s\n", currentDev);
		return 0;
	    }
	    
            vols = getmntinfo(&mnts, MNT_NOWAIT);
            if(vols == -1) {
                    blesscontextprintf(context, kBLLogLevelError,  "Error gettings mounts\n" );
                    return 1;
            }

            while(--vols >= 0) {
                if(strncmp(mnts[vols].f_mntfromname, currentDev, strlen(currentDev)+1) == 0) {
                    blesscontextprintf(context, kBLLogLevelVerbose,  "mount: %s\n", mnts[vols].f_mntonname );
                    strcpy(actargs[kinfo].argument, mnts[vols].f_mntonname);
                    break;
                }

            }

	    if(strlen(actargs[kinfo].argument) == 0) {
	      blesscontextprintf(context, kBLLogLevelError,
			    "Volume for OpenFirmware path %s is not available\n",
			    currentString);
	      return 2;
	    }
    }


    err = BLGetCommonMountPoint(context, actargs[kinfo].argument, "", actargs[kmount].argument);
    if(err) {
            blesscontextprintf(context, kBLLogLevelError,  "Can't get mount point for %s\n", actargs[kinfo].argument );
            return 1;
    }

    err = BLCreateVolumeInformationDictionary(context, actargs[kmount].argument,
                                            &dict);
    if(err) {
            blesscontextprintf(context, kBLLogLevelError,  "Can't print Finder information\n" );
		return 1;
	}

    if(actargs[kplist].present) {
	CFDataRef		tempData = NULL;

	tempData = CFPropertyListCreateXMLData(kCFAllocatorDefault, dict);

        write(fileno(stdout), CFDataGetBytePtr(tempData), CFDataGetLength(tempData));

	CFRelease(tempData);

    } else {
        CFArrayRef finfo = CFDictionaryGetValue(dict, CFSTR("Finder Info"));
	CFDictionaryRef bootblocks = CFDictionaryGetValue(dict, CFSTR("BootBlocks"));
        int j;
        CFNumberRef vsdbref;
        uint64_t vsdb;

        for(j = 0; j < (8-2); j++) {
            CFDictionaryRef word = CFArrayGetValueAtIndex(finfo, j);
            CFNumberRef dirID = CFDictionaryGetValue(word, CFSTR("Directory ID"));
            CFStringRef path = CFDictionaryGetValue(word, CFSTR("Path"));
            uint32_t dirint;
            unsigned char cpath[MAXPATHLEN];
            
            if(!CFNumberGetValue(dirID, kCFNumberLongType, &dirint)) {
                continue;
            }

            if(!CFStringGetCString(path, cpath, MAXPATHLEN, kCFStringEncodingUTF8)) {
                continue;
            }

            blesscontextprintf(context, kBLLogLevelNormal,
                        "finderinfo[%i]: %6lu => %s%s\n", j, dirint,
                        messages[j][dirint > 0], cpath);

        }


        vsdbref = CFDictionaryGetValue(dict, CFSTR("VSDB ID"));
        CFNumberGetValue(vsdbref, kCFNumberSInt64Type, &vsdb);
        
        
    	blesscontextprintf(context, kBLLogLevelNormal, "%s 0x%016qX\n", messages[6][1],
		      vsdb);

	if(actargs[kbootblocks].present && bootblocks) {
	    // print out strings for the bootblocks dictionary
	    // in order to be deterministic, sort the keys first
	    CFIndex i, keycount = CFDictionaryGetCount(bootblocks);
	    const void *keys[keycount];
	    CFArrayRef keyarray;
	    CFMutableArrayRef keymutarray;
	    
	    CFDictionaryGetKeysAndValues(bootblocks, keys, NULL);

	    keyarray = CFArrayCreate(kCFAllocatorDefault, keys, keycount, &kCFTypeArrayCallBacks);
	    keymutarray = CFArrayCreateMutableCopy(kCFAllocatorDefault, keycount,
					    keyarray);
	    CFRelease(keyarray);
	    
	    CFArraySortValues(keymutarray, CFRangeMake(0,keycount-1), (CFComparatorFunction)CFStringCompare, 0);

	    // iterate over keys, printing string values
	    for(i=0; i < keycount; i++) {
		CFStringRef dkey = CFArrayGetValueAtIndex(keymutarray, i);
		CFStringRef str = CFDictionaryGetValue(bootblocks, dkey);

		if(CFGetTypeID(str) == CFStringGetTypeID()) {
		    CFStringRef line = CFStringCreateWithFormat(kCFAllocatorDefault,
						  NULL, CFSTR("%@: %@"), dkey, str);
		    char linecstr[100];

		    CFStringGetCString(line, linecstr, sizeof(linecstr),
			 kCFStringEncodingUTF8);
		    
		    blesscontextprintf(context, kBLLogLevelNormal,
			 "%s\n", linecstr);
		    
		    CFRelease(line);
		}
	    }

	    CFRelease(keymutarray);
	}

    }
    
    
    CFRelease(dict);
    return 0;
}
