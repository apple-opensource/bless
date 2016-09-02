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
 *  BLGetOpenFirmwareBootDevice.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Thu Apr 19 2001.
 *  Copyright (c) 2001-2005 Apple Computer, Inc. All rights reserved.
 *
 *  $Id: BLGetOpenFirmwareBootDevice.c,v 1.26 2005/02/08 00:18:47 ssen Exp $
 *
 *  $Log: BLGetOpenFirmwareBootDevice.c,v $
 *  Revision 1.26  2005/02/08 00:18:47  ssen
 *  Implement support for offline updating of BootX and OF labels
 *  in Apple_Boot partitions, and also for RAIDs. Only works
 *  in --device mode so far
 *
 *  Revision 1.25  2005/02/07 21:22:38  ssen
 *  Refact lookupServiceForName and code for BLDeviceNeedsBooter
 *
 *  Revision 1.24  2005/02/03 00:42:29  ssen
 *  Update copyrights to 2005
 *
 *  Revision 1.23  2005/01/26 01:25:30  ssen
 *  Finish v1 booting support. Also prepare for the day that
 *  unbootable RAIDs will not publish IOBoot entries.
 *
 *  Revision 1.22  2005/01/25 19:37:10  ssen
 *  Apple_Boot_RAID partitions don't have booters themselves,
 *  so don't look for one.
 *
 *  Revision 1.21  2005/01/16 02:11:53  ssen
 *  misc changes to support updating booters
 *
 *  Revision 1.20  2005/01/16 00:10:12  ssen
 *  <rdar://problem/3861859> bless needs to try getProperty(kIOBootDeviceKey)
 *  Implement -getBoot and -info functionality. If boot-device is
 *  set to the Apple_Boot for one of the RAID members, we map
 *  this back to the top-level RAID device and print that out. This
 *  enables support in Startup Disk
 *
 *  Revision 1.19  2005/01/14 22:29:59  ssen
 *  <rdar://problem/3861859> bless needs to try getProperty(kIOBootDeviceKey)
 *  When determining the "OF path" for a device, figure out if it's
 *  part of a RAID set, and if so, find the booter for the primary
 *  path. Otherwise, find a normal booter, or else no booter at all
 *
 *  Revision 1.18  2004/05/17 23:28:35  ssen
 *  Validate arguments a bit
 *  Bug #:
 *  Submitted by:
 *  Reviewed by:
 *
 *  Revision 1.17  2004/04/20 21:40:45  ssen
 *  Update copyrights to 2004
 *
 *  Revision 1.16  2003/10/24 22:46:36  ssen
 *  don't try to compare IOKit objects to NULL, since they are really mach port integers
 *
 *  Revision 1.15  2003/10/17 00:10:39  ssen
 *  add more const
 *
 *  Revision 1.14  2003/07/25 01:16:25  ssen
 *  When mapping OF -> device, if we found an Apple_Boot, try to
 *  find the corresponding partition that is the real root filesystem
 *
 *  Revision 1.13  2003/07/22 15:58:36  ssen
 *  APSL 2.0
 *
 *  Revision 1.12  2003/05/19 02:17:00  ssen
 *  don't look for booters if an Apple_Boot is specified
 *
 *  Revision 1.11  2003/04/23 00:08:03  ssen
 *  Use blostype2string for OSTypes
 *
 *  Revision 1.10  2003/04/19 00:11:14  ssen
 *  Update to APSL 1.2
 *
 *  Revision 1.9  2003/04/16 23:57:35  ssen
 *  Update Copyrights
 *
 *  Revision 1.8  2002/09/24 21:05:46  ssen
 *  Eliminate use of deprecated constants
 *
 *  Revision 1.7  2002/08/22 00:38:42  ssen
 *  Gah. Search for ",\\:tbxi" from the end of the OF path
 *  instead of the beginning. For SCSI cards that use commas
 *  in the OF path, the search was causing a mis-parse.
 *
 *  Revision 1.6  2002/06/11 00:50:51  ssen
 *  All function prototypes need to use BLContextPtr. This is really
 *  a minor change in all of the files.
 *
 *  Revision 1.5  2002/04/27 17:55:00  ssen
 *  Rewrite output logic to format the string before sending of to logger
 *
 *  Revision 1.4  2002/04/25 07:27:30  ssen
 *  Go back to using errorprint and verboseprint inside library
 *
 *  Revision 1.3  2002/02/23 04:13:06  ssen
 *  Update to context-based API
 *
 *  Revision 1.2  2002/02/03 19:20:23  ssen
 *  look for external booter
 *
 *  Revision 1.1  2001/11/16 05:36:47  ssen
 *  Add libbless files
 *
 *  Revision 1.10  2001/11/11 06:20:59  ssen
 *  readding files
 *
 *  Revision 1.8  2001/10/26 04:19:41  ssen
 *  Add dollar Id and dollar Log
 *
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>

#import <mach/mach_error.h>

#import <IOKit/IOKitLib.h>
#import <IOKit/IOBSD.h>
#import <IOKit/storage/IOMedia.h>
#import <IOKit/storage/IOMediaBSDClient.h>
#import <IOKit/storage/IOPartitionScheme.h>

#import <CoreFoundation/CoreFoundation.h>

#include "bless.h"
#include "bless_private.h"

int getPNameAndPType(BLContextPtr context,
                            unsigned char target[],
			    unsigned char pname[],
			    unsigned char ptype[]);

int getExternalBooter(BLContextPtr context,
                      mach_port_t iokitPort,
					  io_service_t dataPartition,
 					 io_service_t *booterPartition);

/*
 * Get OF string for device
 * If it's a non-whole device
 *   Look for an external booter
 *     If there is, replace the partition number in OF string
 * For new world, add a tbxi
 */

int BLGetOpenFirmwareBootDevice(BLContextPtr context, const unsigned char mntfrm[], char ofstring[]) {

    int err;

    kern_return_t           kret;
    mach_port_t             ourIOKitPort;
    io_service_t            service;

	int32_t					needsBooter	= 0;
	int32_t					isBooter	= 0;
		
    io_string_t				ofpath;
    unsigned char			device[MAXPATHLEN];
	char *split = ofpath;
	char tbxi[5];
    
    CFTypeRef               bootData = NULL;

    if(!mntfrm || 0 != strncmp(mntfrm, "/dev/", 5)) return 1;

	strcpy(device, mntfrm);
    
    // Obtain the I/O Kit communication handle.
    if((kret = IOMasterPort(bootstrap_port, &ourIOKitPort)) != KERN_SUCCESS) {
      return 2;
    }

    err = BLGetRAIDBootDataForDevice(context, mntfrm, &bootData);
    if(err) {
        contextprintf(context, kBLLogLevelError,  "Error while determining if %s is a RAID\n", mntfrm );
        return 3;
    }
    
    if(bootData) {
        CFDictionaryRef primary = NULL;
        CFStringRef bootpath = NULL;
		CFStringRef name = NULL;
        io_string_t iostring;
        
        // update name with the primary partition
        if(CFGetTypeID(bootData) == CFArrayGetTypeID() ) {
			if(CFArrayGetCount(bootData) == 0) {
				contextprintf(context, kBLLogLevelError,  "RAID set has no bootable members\n" );
				return 3;				
			}
			
            primary = CFArrayGetValueAtIndex(bootData,0);
            CFRetain(primary);
        } else if(CFGetTypeID(bootData) == CFDictionaryGetTypeID()) {
            primary = bootData;
            CFRetain(primary);
        }
        
        bootpath = CFDictionaryGetValue(primary, CFSTR(kIOBootDevicePathKey));
        if(bootpath == NULL || CFGetTypeID(bootpath) != CFStringGetTypeID()) {
            CFRelease(primary);
            CFRelease(bootData);
            contextprintf(context, kBLLogLevelError,  "Could not find boot path entry for %s\n" , name);
            return 4;            
        }
        
        if(!CFStringGetCString(bootpath,iostring,sizeof(iostring),kCFStringEncodingUTF8)) {
            CFRelease(primary);
            CFRelease(bootData);
            contextprintf(context, kBLLogLevelError,  "Invalid UTF8 for path entry for %s\n" , name);
            return 4;                        
        }

		contextprintf(context, kBLLogLevelVerbose,  "Primary OF boot path is %s\n" , iostring);
        
        service = IORegistryEntryFromPath(ourIOKitPort, iostring );
        if(service == 0) {
            CFRelease(primary);
            CFRelease(bootData);
            contextprintf(context, kBLLogLevelError,  "Could not find IOKit entry for %s\n" , iostring);
            return 4;                                    
        }

        CFRelease(primary);
        CFRelease(bootData);
		
		name = IORegistryEntryCreateCFProperty( service, CFSTR(kIOBSDNameKey),
												kCFAllocatorDefault, 0);

		if(name == NULL || CFStringGetTypeID() != CFGetTypeID(name)) {
			IOObjectRelease(service);
            contextprintf(context, kBLLogLevelError,  "Could not find bsd name for %s\n" , iostring);
			return 5;
		}

		IOObjectRelease(service); service = 0;
		
		if(!CFStringGetCString(name,device+5,sizeof(iostring)-5,kCFStringEncodingUTF8)) {
			CFRelease(name);
            contextprintf(context, kBLLogLevelError,  "Could not find bsd name for %s\n" , iostring);
			return 5;
		}
		
		CFRelease(name);
    }
    
    // by this point, "service" should point at the data partition, or potentially
    // a RAID member. We'll need to map it to a booter partition if necessary

	err = BLDeviceNeedsBooter(context, device,
							  &needsBooter,
							  &isBooter,
							  &service);
	if(err) {
		contextprintf(context, kBLLogLevelError,  "Could not determine if partition needs booter\n" );		
		return 10;
	}
	
	if(!needsBooter && !isBooter) {
		err = BLGetIOServiceForDeviceName(context, (unsigned char *)device + 5, &service);
		if(err) {
			contextprintf(context, kBLLogLevelError,  "Can't find IOService for %s\n", device + 5 );
			return 10;		
		}
		
	}
	
	kret = IORegistryEntryGetPath(service, kIODeviceTreePlane, ofpath);
	if(kret != KERN_SUCCESS) {
		contextprintf(context, kBLLogLevelError,  "Could not get path in device plane for service\n" );
		IOObjectRelease(service);
		return 11;
	}

	IOObjectRelease(service);

	split = ofpath;

	strsep(&split, ":");
	
	if(split == NULL) {
		contextprintf(context, kBLLogLevelError,  "Bad path in device plane for service\n" );
		IOObjectRelease(service);
		return 11;		
	}
	
	sprintf(ofstring, "%s,\\\\:%s", split, blostype2string(kBL_OSTYPE_PPC_TYPE_BOOTX, tbxi));
	
    return 0;
}



int getPNameAndPType(BLContextPtr context,
                     unsigned char target[],
		     unsigned char pname[],
		     unsigned char ptype[]) {

  kern_return_t       status;
  mach_port_t         ourIOKitPort;
  io_iterator_t       services;
  io_registry_entry_t obj;
    CFStringRef temp = NULL;

  pname[0] = '\0';
  ptype[0] = '\0';

  // Obtain the I/O Kit communication handle.
  status = IOMasterPort(bootstrap_port, &ourIOKitPort);
  if (status != KERN_SUCCESS) {
    return 1;
  }

  // Obtain our list of one object
  // +5 to skip over "/dev/"
  status = IOServiceGetMatchingServices(ourIOKitPort,
					IOBSDNameMatching(ourIOKitPort,
							  0,
							  target),
					&services);
  if (status != KERN_SUCCESS) {
    return 2;
  }

  // Should only be one IOKit object for this volume. (And we only want one.)
  obj = IOIteratorNext(services);
  if (!obj) {
    return 3;
  }

    temp = (CFStringRef)IORegistryEntryCreateCFProperty(
	obj,
	CFSTR(kIOMediaContentKey),
        kCFAllocatorDefault,
	0);

  if (temp == NULL) {
    return 4;
  }

    if(!CFStringGetCString(temp, ptype, MAXPATHLEN, kCFStringEncodingMacRoman)) {
        CFRelease(temp);
        return 4;
    }

    CFRelease(temp);
    
    status = IORegistryEntryGetName(obj,pname);
  if (status != KERN_SUCCESS) {
    return 5;
  }


  
  IOObjectRelease(obj);
  obj = 0;

  IOObjectRelease(services);
  services = 0;

    contextprintf(context, kBLLogLevelVerbose,  "looking at partition %s, type %s, name %s\n", target, ptype, pname );

  return 0;

}

int getExternalBooter(BLContextPtr context,
                      mach_port_t iokitPort,
					  io_service_t dataPartition,
					  io_service_t *booterPartition)
{
	CFStringRef name = NULL;
	CFStringRef content = NULL;
	char cname[MAXPATHLEN];
	char *spos = NULL;
	io_service_t booter = 0;
	int partnum = 0;
	int errnum;
	
	name = (CFStringRef)IORegistryEntryCreateCFProperty(
														dataPartition,
														CFSTR(kIOBSDNameKey),
														kCFAllocatorDefault,
														0);
	
	if(!CFStringGetCString(name, cname, sizeof(cname), kCFStringEncodingUTF8)) {
        CFRelease(name);
        return 1;
    }

	CFRelease(name);
	
	spos = strrchr(cname, 's');
	if(spos == NULL || spos == &cname[2]) {
		contextprintf(context, kBLLogLevelError,  "Can't determine partition for %s\n", cname );
		return 2;
	}
	
	partnum = atoi(spos+1);
	sprintf(spos, "s%d", partnum-1);
	
	errnum = BLGetIOServiceForDeviceName(context, cname, &booter);
	if(errnum) {
		contextprintf(context, kBLLogLevelError,  "Could not find IOKit entry for %s\n" , cname);
		return 4;
	}
	
	content = (CFStringRef)IORegistryEntryCreateCFProperty(
														booter,
														CFSTR(kIOMediaContentKey),
														kCFAllocatorDefault,
														0);
	if(content == NULL || CFGetTypeID(content) != CFStringGetTypeID()) {
		contextprintf(context, kBLLogLevelError,  "Invalid content type for %s\n" , cname);
		IOObjectRelease(booter);
		return 5;		
	}
	
	if(!CFEqual(CFSTR("Apple_Boot"), content)) {
		contextprintf(context, kBLLogLevelError,  "Booter partition %s is not Apple_Boot\n" , cname);
		IOObjectRelease(booter);
		return 6;
	}
	
	*booterPartition = booter;
	return 0;
}
