/*
 * Copyright (c) 2001-2007 Apple Inc. All Rights Reserved.
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
 *  handleInfo.c
 *  bless
 *
 *  Created by Shantonu Sen <ssen@apple.com> on Thu Dec 6 2001.
 *  Copyright (c) 2001-2007 Apple Inc. All Rights Reserved.
 *
 *  $Id: handleInfo.c,v 1.45 2006/06/24 00:46:37 ssen Exp $
 *
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <paths.h>

#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/param.h>

#include <sys/socket.h>
#include <net/if.h>
#include <arpa/nameser.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "enums.h"
#include "structs.h"

#include "bless.h"
#include "bless_private.h"
#include "protos.h"

#include <IOKit/storage/IOMedia.h>
#include <IOKit/IOBSD.h>
#include <CoreFoundation/CoreFoundation.h>
#include <APFS/APFS.h>


#define kPrebootIndicator	"<Preboot>"

static int interpretEFIString(BLContextPtr context, CFStringRef efiString, char *bootdevice, int deviceLen,
                              char *relPath, char *relPathLen);

static void addElements(const void *key, const void *value, void *context);

static int findBootRootAggregate(BLContextPtr context, char *memberPartition, char *bootRootDevice, int deviceLen);
static int FixupPrebootMountPointInPaths(CFMutableDictionaryRef dict, const char *mountPoint);



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
 * 1 is blessed system file for EFI systems
 * 2 is first link in linked list of folders to open at mount (deprecated)
 *   (9 and X are supposed to honor this if set and ignore OpenFolderListDF)
 * 3 OS 9 blessed system folder (maybe OS X?)
 * 4 thought to be unused (reserved for Finder, once was PowerTalk Inbox)
 * 5 OS X blessed system folder (maybe OS 9?)
 * 6 & 7 are 64 bit volume identifier (high 32 bits in 6; low in 7)
 */

#define MISSINGMSG "<missing>"
static const char *messages[7][2] = {
    
    { "No Blessed System Folder", "Blessed System Folder is " },    /* 0 */
    { "No Blessed System File", "Blessed System File is " },
    { "Open-folder linked list empty", "1st dir in open-folder list is " },
    { "No alternate OS blessed file/folder", "Alternate OS blessed file/folder is " },  /* 3 */
    { "Unused field unset", "Thought-to-be-unused field points to " },
    { "No OS 9 + X blessed X folder", "OS X blessed folder is " },  /* 5 */
    { "64-bit VSDB volume id not present", "64-bit VSDB volume id: " }
    
};

int modeInfo(BLContextPtr context, struct clarg actargs[klast]) {
    int                 	ret;
    CFDictionaryRef     	dict;
    struct statfs       	sb;
    CFMutableDictionaryRef  allInfo = NULL;
	int						isHFS = 0;
    int                 	isAPFS = 0;
	bool					doSearch = false;
	char					booterPart[MNAMELEN];
	CFDictionaryRef			booterDict;
	CFArrayRef				prebootBSDs;
	CFStringRef				prebootDev;
	char					prebootNode[MNAMELEN];
	CFMutableDictionaryRef	matching;
	bool					explicitPreboot = false;
    bool                    explicitRecovery = false;
	bool					noAccessToPreboot = false;
	char					*volToCheck;
	io_service_t			service = IO_OBJECT_NULL;
	CFStringRef				newBSDName;
	int             		mntsize;
	struct statfs   		*mnts;
	char            		realMountPoint[MAXPATHLEN];
	char					prebootMountPoint[MAXPATHLEN];
    UInt16                  role;

    if(!actargs[kinfo].hasArg || actargs[kgetboot].present) {
        char currentDev[1024]; // may contain URLs like bsdp://foo
        char currentPath[MAXPATHLEN];
        BLPreBootEnvType	preboot;
        
        ret = BLGetPreBootEnvironmentType(context, &preboot);
        if(ret)
            return 1;
        
        if (preboot == kBLPreBootEnvType_EFI) {
            CFStringRef     efibootdev = NULL;
            
            ret = BLCopyEFINVRAMVariableAsString(context,
                                                 CFSTR("efi-boot-device"),
                                                 &efibootdev);
            
            if(ret || efibootdev == NULL) {
                blesscontextprintf(context, kBLLogLevelError,
                                   "Can't access \"efi-boot-device\" NVRAM variable\n");
                return 1;
            }

            blesscontextprintf(context, kBLLogLevelVerbose,  "Current EFI boot device string is: '%s'\n",
                               BLGetCStringDescription(efibootdev));                    

            ret = BLValidateXMLBootOption(context,
                                          CFSTR("efi-boot-device"),
                                          CFSTR("efi-boot-device-data"));
            if(ret) {
                CFRelease(efibootdev);
                blesscontextprintf(context, kBLLogLevelError,
                                   "XML representation doesn't match true boot preference\n");
                return 2;                    					
            }
            
            ret = interpretEFIString(context, efibootdev, currentDev, sizeof currentDev,
                                     currentPath, sizeof currentPath);
            if(ret) {
                CFRelease(efibootdev);
                blesscontextprintf(context, kBLLogLevelError,
                                   "Can't interpret EFI boot device\n");
                return 2;                    
            }
            
            CFRelease(efibootdev);
        } else {
            blesscontextprintf(context, kBLLogLevelError,  "Unknown preboot environment\n");
            return 1;
        }
        
        // Check for Boot!=Root for physical disk boot paths
        if (0 == strncmp(currentDev, "/dev/", 5)) {
            char parentDev[MNAMELEN];
            uint32_t partNum;
            BLPartitionType mapType;
            CFStringRef contentHint;
            int apfs = 0;
            
            ret = BLGetIOServiceForDeviceName(context, currentDev + 5, &service);
            if (ret) {
                blesscontextprintf(context, kBLLogLevelError,
                                   "Can't get IOService for %s\n", currentDev);
                return 3;
            }
            
            contentHint = IORegistryEntryCreateCFProperty(service, CFSTR(kIOMediaContentKey), kCFAllocatorDefault, 0);
            IOObjectRelease(service);
            
            if (contentHint && CFGetTypeID(contentHint) == CFStringGetTypeID()) {
                ret = BLGetParentDeviceAndPartitionType(context, currentDev,
                                                        parentDev,
                                                        &partNum,
                                                        &mapType);
                if (ret == 10) {
                    blesscontextprintf(context, kBLLogLevelVerbose, "Detected APFS volume.  Skipping Boot!=Root check.\n");
                    apfs = 1;
                    ret = 0;
                }
                if (ret) {
                    blesscontextprintf(context, kBLLogLevelError,
                                       "Can't determine parent media for %s\n", currentDev);
                    return 3;
                }
                if (apfs) {
                    ret = BLCreateBooterInformationDictionary(context, currentDev + strlen("/dev/"), &booterDict);
                    if (ret) {
                        blesscontextprintf(context, kBLLogLevelError, "Can't get booter information for %s\n", currentDev);
                        return 3;
                    }
                    prebootBSDs = CFDictionaryGetValue(booterDict, kBLAPFSPrebootVolumesKey);
                    if (prebootBSDs && CFGetTypeID(prebootBSDs) == CFArrayGetTypeID()) {
                        CFIndex count, i;
                        CFStringRef uuidStr;
                        char *slash = NULL;
						
                        count = CFArrayGetCount(prebootBSDs);
                        for (i = 0; i < count; i++) {
                            prebootDev = CFArrayGetValueAtIndex(prebootBSDs, i);
                            CFStringGetCString(prebootDev, prebootNode, sizeof prebootNode, kCFStringEncodingUTF8);
                            if (strcmp(prebootNode, currentDev + strlen("/dev/")) == 0) break;
                        }
                        if (i < count) {
                            // NVRAM contained a reference to an APFS preboot volume.  Let's find out
                            // the UUID of the system volume it references.
                            if (currentPath[0] == '/') {
                                // There was a path in NVRAM.  Let's just pull the UUID out of there.
                                // First character should be '/', UUID is right after it, followed by
                                // another '/'.
                                slash = strchr(currentPath + 1, '/');
                                if (!slash || slash - (currentPath+1) != 36) slash = NULL;
                            }
                            if (!slash) {
                                // Either there was no path in NVRAM or it didn't have the right form.
                                // Let's try to get the path from the volume bless information.
                                // Check if the preboot volume is mounted.
                                bool            mustUnmount = false;
                                uint64_t        blessWords[2];
                                
                                mntsize = getmntinfo(&mnts, MNT_NOWAIT);
                                for (i = 0; i < mntsize; i++) {
                                    if (strcmp(mnts[i].f_mntfromname, currentDev) == 0) break;
                                }
                                if (i < mntsize) {
                                    strlcpy(prebootMountPoint, mnts[i].f_mntonname, sizeof prebootMountPoint);
                                } else {
                                    // The preboot volume isn't mounted right now.  We'll have to mount it.
                                    ret = BLMountContainerVolume(context, currentDev + strlen("/dev/"), prebootMountPoint,
															 sizeof prebootMountPoint, true);
                                    if (ret) {
                                        blesscontextprintf(context, kBLLogLevelError, "Couldn't mount preboot volume %s\n", currentDev);
                                        return 3;
                                    }
                                    mustUnmount = true;
                                }
                                ret = BLGetAPFSBlessData(context, prebootMountPoint, blessWords);
                                if (ret) {
                                    blesscontextprintf(context, kBLLogLevelError, "Couldn't get bless data from preboot volume.\n");
                                }
                                if (!ret) {
                                    ret = BLLookupFileIDOnMount64(context, prebootMountPoint, blessWords[0], currentPath, sizeof currentPath);
                                    if (ret) {
                                        blesscontextprintf(context, kBLLogLevelError,
                                                           "Couldn't find path for blessed file %lld on preboot volume.\n",
                                                           (long long)blessWords[0]);
                                    }
                                }
                                if (mustUnmount) BLUnmountContainerVolume(context, prebootMountPoint);
                                if (ret) {
                                    CFRelease(booterDict);
                                    return 4;
                                }
                                realpath(prebootMountPoint, realMountPoint);
                                memmove(currentPath, currentPath + strlen(realMountPoint), strlen(currentPath) - strlen(realMountPoint) + 1);
                                slash = strchr(currentPath + 1, '/');
                                if (!slash || slash - (currentPath+1) != 36) slash = NULL;
                            }
                            if (!slash) {
                                // Can't find a valid UUID in blessed path.
                                blesscontextprintf(context, kBLLogLevelError, "Couldn't find a valid volume UUID in the boot path\n");
                                CFRelease(booterDict);
                                return 4;
                            }
                            *slash = '\0';
                            uuidStr = CFStringCreateWithCString(kCFAllocatorDefault, currentPath + 1, kCFStringEncodingUTF8);
                            *slash = '/';
                            matching = IOServiceMatching("AppleAPFSVolume");
                            CFDictionarySetValue(matching, CFSTR(kIOMediaUUIDKey), uuidStr);
                            CFRelease(uuidStr);
                            service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);
                            if (service) {
                                newBSDName = IORegistryEntryCreateCFProperty(service, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
                                if (newBSDName) {
                                    blesscontextprintf(context, kBLLogLevelVerbose, "%s is a preboot volume\n", currentDev);
                                    CFStringGetCString(newBSDName,
                                                       currentDev + strlen("/dev/"),
                                                       sizeof currentDev - strlen("/dev/"),
                                                       kCFStringEncodingUTF8);
                                    blesscontextprintf(context, kBLLogLevelVerbose, "Substituting found system volume %s\n", currentDev);
                                }
                            }
                        }
                    }
                    CFRelease(booterDict);
                } else {
                    switch(mapType) {
                        case kBLPartitionType_APM:
                            if (CFEqual(contentHint, CFSTR("Apple_Boot"))) {
                                doSearch = true;
                                snprintf(booterPart, sizeof(booterPart), "%ss%u", parentDev, partNum+1);
                            }
                            break;
                        case kBLPartitionType_GPT:
                            if (CFEqual(contentHint, CFSTR("426F6F74-0000-11AA-AA11-00306543ECAC"))) {
                                doSearch = true;
                                snprintf(booterPart, sizeof(booterPart), "%ss%u", parentDev, partNum-1);
                            }
                            break;
                        default:
                            blesscontextprintf(context, kBLLogLevelVerbose,
                                               "Partition map type does not support Boot!=Root\n");
                            break;
                    }
                    
                    if (doSearch) {
                        ret = findBootRootAggregate(context, booterPart, currentDev, sizeof currentDev);
                        if (ret) {
                            blesscontextprintf(context, kBLLogLevelError,
                                               "Failed to find Boot!=Root aggregate media for %s\n", currentDev);
                            return 3;
                        }
                    }
                }
            }
            

            if (contentHint) CFRelease(contentHint);
            
        }
            
	    if( actargs[kgetboot].present) {
            if(actargs[kplist].present) {
                CFStringRef vol = CFStringCreateWithCString(kCFAllocatorDefault,
                                                            currentDev,
                                                            kCFStringEncodingUTF8);
                CFMutableDictionaryRef dict = NULL;
                CFDataRef		tempData = NULL;
                
                dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                                 &kCFTypeDictionaryKeyCallBacks,
                                                 &kCFTypeDictionaryValueCallBacks);
                CFDictionaryAddValue(dict, CFSTR("Boot Volume"), vol);
                CFRelease(vol);
                
				tempData = CFPropertyListCreateData(kCFAllocatorDefault, dict, kCFPropertyListXMLFormat_v1_0, 0, NULL);
				
                write(fileno(stdout), CFDataGetBytePtr(tempData), CFDataGetLength(tempData));
                
                CFRelease(tempData);
                CFRelease(dict);
                
            } else {
                blesscontextprintf(context, kBLLogLevelNormal, "%s\n", currentDev);
            }
            return 0;
	    }
	    
        // only look at mountpoints if it looks like a dev node
        if (0 == strncmp(currentDev, "/dev/", 5)) {
            struct statfs *mnts;
            int vols;

            vols = getmntinfo(&mnts, MNT_NOWAIT);
            if(vols == -1) {
                blesscontextprintf(context, kBLLogLevelError,  "Error gettings mounts\n" );
                return 1;
            }
            
            while(--vols >= 0) {
                struct statfs sb;
                
                // somewhat redundant, but blsustatfs will canonicalize the mount device
                if(0 != blsustatfs(mnts[vols].f_mntonname, &sb)) {
                    continue;
                }
                
                if(strncmp(sb.f_mntfromname, currentDev, strlen(currentDev)+1) == 0) {
                    blesscontextprintf(context, kBLLogLevelVerbose,  "mount: %s\n", mnts[vols].f_mntonname );
                    strlcpy(actargs[kinfo].argument, mnts[vols].f_mntonname, kMaxArgLength);
                    actargs[kinfo].hasArg = 1;
                    break;
                }
                
            }
        }
        
	    if(!actargs[kinfo].hasArg) {
            blesscontextprintf(context, kBLLogLevelError,
                               "Volume for path %s is not available\n",
                               currentDev);
            return 2;
	    }
    }
    
    
    ret = BLGetCommonMountPoint(context, actargs[kinfo].argument, "", actargs[kmount].argument);
    if(ret) {
        blesscontextprintf(context, kBLLogLevelError,  "Can't get mount point for %s\n", actargs[kinfo].argument );
        return 1;
    }
    
    ret = blsustatfs(actargs[kmount].argument, &sb);
    if(ret) {
        blesscontextprintf(context, kBLLogLevelError,  "Can't get device for %s\n", actargs[kmount].argument );
        return 1;        
        
    }
    
	ret = BLIsMountHFS(context, actargs[kmount].argument, &isHFS);
    if(ret) {
		blesscontextprintf(context, kBLLogLevelError,  "Could not determine filesystem of %s\n", actargs[kmount].argument );
		return 1;
    }
    
    ret = BLIsMountAPFS(context, actargs[kmount].argument, &isAPFS);
    if(ret) {
        blesscontextprintf(context, kBLLogLevelError,  "Could not determine filesystem of %s\n", actargs[kmount].argument );
        return 1;
    }

	
	if (isHFS) {
		ret = BLCreateVolumeInformationDictionary(context, actargs[kmount].argument,
												  &dict);
		if(ret) {
			blesscontextprintf(context, kBLLogLevelError,  "Can't print Finder information\n" );
			return 1;
		}
        
		allInfo = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, dict);
		CFRelease(dict);
		dict = NULL;
    } else if (!isAPFS) {
		allInfo = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	}
    
    
    ret = BLCreateBooterInformationDictionary(context, sb.f_mntfromname + 5, &dict);
    if (ret) {
        blesscontextprintf(context, kBLLogLevelError,  "Can't get booter information\n" );
		return 3;
    }
	
	if (isAPFS) {
        // Are we being asked specifically about the recovery volume?
        APFSVolumeRole(sb.f_mntfromname + 5, &role, NULL);
        if (role & APFS_VOL_ROLE_RECOVERY) {
            ret = BLCreateAPFSVolumeInformationDictionary(context, actargs[kmount].argument, (void *)&allInfo);
            if (ret) {
                blesscontextprintf(context, kBLLogLevelError, "Couldn't get bless data from recovery volume.\n");
                return 4;
            }
            explicitRecovery = true;
            volToCheck = actargs[kmount].argument;
        } else {
            prebootBSDs = CFDictionaryGetValue(dict, kBLAPFSPrebootVolumesKey);
            if (prebootBSDs && CFGetTypeID(prebootBSDs) == CFArrayGetTypeID()) {
                CFIndex count, i;
                bool            mustUnmount = false;
                
                count = CFArrayGetCount(prebootBSDs);
                for (i = 0; i < count; i++) {
                    prebootDev = CFArrayGetValueAtIndex(prebootBSDs, i);
                    CFStringGetCString(prebootDev, prebootNode, sizeof prebootNode, kCFStringEncodingUTF8);
                    if (strcmp(prebootNode, sb.f_mntfromname + strlen(_PATH_DEV)) == 0) break;
                }
                if (i < count) {
                    // We're being asked directly about the preboot volume.
                    explicitPreboot = true;
                    volToCheck = actargs[kmount].argument;
                } else if (count > 0) {
                    // We've been asked about a non-preboot volume.  We need to find the preboot
                    // volume and get its information.
                    prebootDev = CFArrayGetValueAtIndex(prebootBSDs, 0);
                    CFStringGetCString(prebootDev, prebootNode, sizeof prebootNode, kCFStringEncodingUTF8);
                    
                    mntsize = getmntinfo(&mnts, MNT_NOWAIT);
                    for (i = 0; i < mntsize; i++) {
                        if (strcmp(mnts[i].f_mntfromname + strlen(_PATH_DEV), prebootNode) == 0) break;
                    }
                    if (i < mntsize) {
                        strlcpy(prebootMountPoint, mnts[i].f_mntonname, sizeof prebootMountPoint);
                    } else {
                        // The preboot volume isn't mounted right now.  We'll have to mount it.
                        ret = BLMountContainerVolume(context, prebootNode, prebootMountPoint, sizeof prebootMountPoint, true);
                        if (ret) {
                            blesscontextprintf(context, kBLLogLevelError, "Couldn't mount preboot volume /dev/%s\n", prebootNode);
                            noAccessToPreboot = true;
                            ret = 0;
                        } else {
                            mustUnmount = true;
                        }
                    }
                    volToCheck = prebootMountPoint;
                } else {
                    blesscontextprintf(context, kBLLogLevelError, "The container for this APFS volume doesn't appear to have a preboot volume\n");
                    return 5;
                }
                if (noAccessToPreboot) {
                    allInfo = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                } else {
                    ret = BLCreateAPFSVolumeInformationDictionary(context, volToCheck, (void *)&allInfo);
                    if (mustUnmount) BLUnmountContainerVolume(context, prebootMountPoint);
                    if (ret) {
                        blesscontextprintf(context, kBLLogLevelError, "Couldn't get bless data from preboot volume.\n");
                        return 4;
                    }
                }
                if (mustUnmount) {
                    // We had to privately mount the preboot volume, and now that it's
                    // been unmounted, the mount-point part of the paths in the dictionary
                    // don't make much sense.  So let's clean them up.
                    ret = FixupPrebootMountPointInPaths(allInfo, volToCheck);
                    if (ret) {
                        blesscontextprintf(context, kBLLogLevelError, "Couldn't fix path names for blessed file and folder.\n");
                        return 3;
                    }
                    volToCheck = kPrebootIndicator;
                }
            } else {
                blesscontextprintf(context, kBLLogLevelError, "The container for this APFS volume doesn't appear to have a preboot volume\n");
                return 5;
            }
        }
	}
    
    CFDictionaryApplyFunction(dict, addElements, (void *)allInfo);    
    CFRelease(dict);
    
    dict = (CFDictionaryRef)allInfo;
    
    if(actargs[kplist].present) {
        CFDataRef		tempData = NULL;
        
		tempData = CFPropertyListCreateData(kCFAllocatorDefault, dict, kCFPropertyListXMLFormat_v1_0, 0, NULL);
        
        write(fileno(stdout), CFDataGetBytePtr(tempData), CFDataGetLength(tempData));
        
        CFRelease(tempData);
        
    } else if (isHFS) {
        CFArrayRef	finfo = CFDictionaryGetValue(dict, CFSTR("Finder Info"));
        int			j;
        CFNumberRef	vsdbref;
        uint64_t	vsdb;
		
        for(j = 0; j < (8-2); j++) {
            CFDictionaryRef word = CFArrayGetValueAtIndex(finfo, j);
            CFNumberRef dirID = CFDictionaryGetValue(word, CFSTR("Directory ID"));
            CFStringRef path = CFDictionaryGetValue(word, CFSTR("Path"));
            uint32_t dirint;
            char cpath[MAXPATHLEN];
            
            if(!CFNumberGetValue(dirID, kCFNumberSInt32Type, &dirint)) {
                continue;
            }
            
            if(dirint > 0 && CFStringGetLength(path) == 0) {
                strlcpy(cpath, MISSINGMSG, sizeof cpath);
            } else {
                if(!CFStringGetCString(path, cpath, MAXPATHLEN, kCFStringEncodingUTF8)) {
                    continue;
                }
            }
            
            blesscontextprintf(context, kBLLogLevelNormal,
                               "finderinfo[%i]: %6u => %s%s\n", j, dirint,
                               messages[j][dirint > 0], cpath);
            
        }
        
        
        vsdbref = CFDictionaryGetValue(dict, CFSTR("VSDB ID"));
        CFNumberGetValue(vsdbref, kCFNumberSInt64Type, &vsdb);
        
        
    	blesscontextprintf(context, kBLLogLevelNormal, "%s 0x%016llX\n", messages[6][1],
                           vsdb);
        
    } else if (isAPFS) {
		if (noAccessToPreboot) {
			blesscontextprintf(context, kBLLogLevelNormal, "This is an APFS volume.  The preboot volume in its container\n"
							   "cannot be accessed.  Try mounting the preboot volume (/dev/%s)\nor running as root.\n", prebootNode);
			return 5;
		} else {
			CFArrayRef binfo = CFDictionaryGetValue(dict, CFSTR("Bless Info"));
			CFDictionaryRef word;
			CFNumberRef dirID;
			CFStringRef path;
			uint64_t dirint;
			char cpath[MAXPATHLEN];
			int j;
			
			for (j = 0; j < 2; j++) {
				word = CFArrayGetValueAtIndex(binfo, j);
				dirID = CFDictionaryGetValue(word, CFSTR("Directory ID"));
				path = CFDictionaryGetValue(word, CFSTR("Path"));
				
				if (!CFNumberGetValue(dirID, kCFNumberSInt64Type, &dirint)) {
					continue;
				}
				
				if (dirint > 0 && CFStringGetLength(path) == 0) {
					strlcpy(cpath, MISSINGMSG, sizeof cpath);
				} else {
					if (!CFStringGetCString(path, cpath, sizeof cpath, kCFStringEncodingUTF8)) {
						continue;
					}
				}
				
				blesscontextprintf(context, kBLLogLevelNormal,
								   "%12llu => %s%s\n", dirint,
								   messages[1-j][dirint > 0], cpath);
				
				if (CFStringGetLength(path) > 0 && j == 1) {
					char cpath[MAXPATHLEN];
					char *slash;
					CFStringRef uuidStr;
					char		bsd[64] = _PATH_DEV;
					int			i;
					bool		gotIt = false;
					
					CFStringGetCString(path, cpath, sizeof cpath, kCFStringEncodingUTF8);
					if (strcmp(volToCheck, kPrebootIndicator) != 0) {
						realpath(volToCheck, realMountPoint);
					} else {
						strlcpy(realMountPoint, kPrebootIndicator, sizeof realMountPoint);
					}
					memmove(cpath, cpath + strlen(realMountPoint), strlen(cpath) - strlen(realMountPoint) + 1);
					slash = strchr(cpath + 1, '/');
					if ((!slash && strlen(cpath+1) != 36) || (slash && slash - (cpath+1) != 36)) {
						// Can't find a valid UUID in blessed path.
						blesscontextprintf(context, kBLLogLevelError, "Couldn't find a valid volume UUID in the boot path\n");
                        CFRelease(dict);
						return 4;
					}
					if (slash) *slash = '\0';
					uuidStr = CFStringCreateWithCString(kCFAllocatorDefault, cpath + 1, kCFStringEncodingUTF8);
					matching = IOServiceMatching("AppleAPFSVolume");
					CFDictionarySetValue(matching, CFSTR(kIOMediaUUIDKey), uuidStr);
					CFRelease(uuidStr);
					service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);
					if (service) {
						newBSDName = IORegistryEntryCreateCFProperty(service, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
						if (newBSDName) {
							CFStringGetCString(newBSDName, bsd + strlen(_PATH_DEV), sizeof bsd - strlen(_PATH_DEV),
											   kCFStringEncodingUTF8);
							gotIt = true;
							mntsize = getmntinfo(&mnts, MNT_NOWAIT);
							for (i = 0; i < mntsize; i++) {
								if (strcmp(mnts[i].f_mntfromname, bsd) == 0) break;
							}
							if (i < mntsize) {
                                if (explicitPreboot || explicitRecovery) {
                                    blesscontextprintf(context, kBLLogLevelNormal, "These paths are associated with the volume \"%s\".\n",
                                                       mnts[i].f_mntonname);
                                } else {
                                    blesscontextprintf(context, kBLLogLevelNormal, "The blessed volume in this APFS container is \"%s\".\n",
                                                       mnts[i].f_mntonname);
                                }
							} else {
                                if (explicitPreboot || explicitRecovery) {
                                    blesscontextprintf(context, kBLLogLevelNormal, "These paths are associated with the device %s, "
                                                       "which is not mounted.\n", bsd);
                                } else {
                                    blesscontextprintf(context, kBLLogLevelNormal, "The blessed volume in this APFS container is %s, which is not mounted.\n", bsd);
                                }
							}
						}
					}
					if (!gotIt) {
						blesscontextprintf(context, kBLLogLevelError, "Could not find an APFS volume with UUID %s\n", cpath + 1);
                        CFRelease(dict);
						return 6;
					}
				}
			}
		}
	}

	CFRelease(dict);
    
    return 0;
}

static int interpretEFIString(BLContextPtr context, CFStringRef efiString, char *bootdevice, int deviceLen,
                              char *relPath, char *relPathLen)
{
    char                interface[IF_NAMESIZE];
    char                host[NS_MAXDNAME];
    char                path[1024];
    int                 ret;
    
    *relPath = '\0';
    ret = BLInterpretEFIXMLRepresentationAsDeviceWithPath(context,
                                                          efiString,
                                                          path,
                                                          sizeof path,
                                                          relPath,
                                                          relPathLen);
    if(ret == 0) {
        blesscontextprintf(context, kBLLogLevelVerbose,  "Disk boot device detected\n" );
        
        snprintf(bootdevice, deviceLen, "/dev/%s", path);
        
        return 0;
    } else {
		BLNetBootProtocolType protocol;
		
        ret = BLInterpretEFIXMLRepresentationAsNetworkPath(context,
                                                           efiString,
														   &protocol,
                                                           interface,
                                                           host,
                                                           path);
        
        if(ret == 0) {
            blesscontextprintf(context, kBLLogLevelVerbose,  "Network boot device detected\n" );
            
            if(strlen(path) > 0) {
                sprintf(bootdevice, "tftp://%s@%s/%s", interface, host, path);
            } else {
                sprintf(bootdevice, "%s://%s@%s",
                                        (protocol == kBLNetBootProtocol_PXE ? "pxe" : "bsdp"),
                                        interface, host);            
            }
            
            return 0;
        } else {
			ret = BLInterpretEFIXMLRepresentationAsLegacyDevice(context,
                                                                efiString,
                                                                path,
                                                                sizeof path);
			if(ret == 0) {
				blesscontextprintf(context, kBLLogLevelVerbose,  "Legacy boot device detected\n" );
				
				sprintf(bootdevice, "/dev/%s", path);
				
				return 0;
			}				
		}
    }
    
    blesscontextprintf(context, kBLLogLevelError,  "Could not interpret boot device as either network or disk\n" );
    
    return 1;
}

// stolen from BLGetDeviceForOFPath
static int isBootRootPath(BLContextPtr context, mach_port_t iokitPort, io_service_t member,
					  CFDictionaryRef raidEntry)
{
	CFStringRef path;
	io_string_t	cpath;
	io_service_t	service;
	
	path = CFDictionaryGetValue(raidEntry, CFSTR(kIOBootDevicePathKey));
	if(path == NULL) return 0;
    
	if(!CFStringGetCString(path,cpath,sizeof(cpath),kCFStringEncodingUTF8))
		return 0;
    
	contextprintf(context, kBLLogLevelVerbose,  "Comparing member to %s", cpath);
	
	service = IORegistryEntryFromPath(iokitPort, cpath);
	if(service == 0) {
		contextprintf(context, kBLLogLevelVerbose,  "\nCould not find service\n");
		return 0;
	}
	
	if(IOObjectIsEqualTo(service, member)) {
		contextprintf(context, kBLLogLevelVerbose,  "\tEQUAL\n");
		IOObjectRelease(service);
		return 1;
	} else {
		contextprintf(context, kBLLogLevelVerbose,  "\tNOT EQUAL\n");		
	}
	
	IOObjectRelease(service);	
	
	return 0;
}



static int findBootRootAggregate(BLContextPtr context, char *memberPartition, char *bootRootDevice, int deviceLen)
{
    io_service_t member = IO_OBJECT_NULL, testmedia = IO_OBJECT_NULL;
    io_iterator_t iter;
    kern_return_t kret;
    int ret;
    bool                foundBootRoot = false;
    CFStringRef         memberContent = NULL;
    
    ret = BLGetIOServiceForDeviceName(context, memberPartition + 5, &member);
    if (ret) {
        blesscontextprintf(context, kBLLogLevelError,
                           "Can't get IOService for %s\n", memberPartition);
        return 3;
    }
    
    kret = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(kIOMediaClass), &iter);
	if(kret != KERN_SUCCESS) {
        IOObjectRelease(member);
		contextprintf(context, kBLLogLevelVerbose,  "Could not find any media devices on the system\n");
		return 2;
	}
    
    while((testmedia = IOIteratorNext(iter))) {
		CFTypeRef			data = NULL;
        io_string_t         iopath;
        
        if(0 == IORegistryEntryGetPath(testmedia, kIOServicePlane, iopath)) {
            contextprintf(context, kBLLogLevelVerbose,  "Checking %s\n", iopath);
        }
        
        data = IORegistryEntrySearchCFProperty( testmedia,
                                               kIOServicePlane,
                                               CFSTR(kIOBootDeviceKey),
                                               kCFAllocatorDefault,
                                               kIORegistryIterateRecursively|
                                               kIORegistryIterateParents);
        
        if(data) {
            if (CFGetTypeID(data) == CFArrayGetTypeID()) {
                CFIndex i, count = CFArrayGetCount(data);
                for(i=0; i < count; i++) {
                    CFDictionaryRef ent = CFArrayGetValueAtIndex((CFArrayRef)data,i);
                    if(isBootRootPath(context, kIOMasterPortDefault, member, ent)) {
                        foundBootRoot = true;
                        break;
                    }
                }
                
            } else if(CFGetTypeID(data) == CFDictionaryGetTypeID()) {
                if(isBootRootPath(context, kIOMasterPortDefault, member, (CFDictionaryRef)data)) {
                    foundBootRoot = true;
                }
            }
            CFRelease(data);
		}
        
        if (foundBootRoot) {
            CFStringRef bsdName;
            
            bsdName = IORegistryEntryCreateCFProperty(testmedia, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
            
            contextprintf(context, kBLLogLevelVerbose,  "Found Boot!=Root aggregate media %s\n", BLGetCStringDescription(bsdName));
            
            strlcpy(bootRootDevice, "/dev/", deviceLen);
            CFStringGetCString(bsdName, bootRootDevice+5, deviceLen-5, kCFStringEncodingUTF8);
            
            CFRelease(bsdName);
            
        }
        
        IOObjectRelease(testmedia);
        
        if (foundBootRoot) break;
    }
    IOObjectRelease(iter);
    
    memberContent = IORegistryEntryCreateCFProperty(member, CFSTR(kIOMediaContentKey), kCFAllocatorDefault, 0);
    IOObjectRelease(member);
    

    // not boot root. It might still be something like UFS
    if (!foundBootRoot) {
        if (memberContent && (!CFEqual(memberContent, CFSTR("Apple_Boot")) && !CFEqual(memberContent, CFSTR("Apple_HFS")))) {
            foundBootRoot = true;
            contextprintf(context, kBLLogLevelVerbose,  "Found legacy Apple_Boot media %s\n", BLGetCStringDescription(memberContent));
            
            strlcpy(bootRootDevice, memberPartition, deviceLen);
        }
    }
    
    if (memberContent) CFRelease(memberContent);
    
    if (foundBootRoot) {
        return 0;
    } else {
        return 1;
    }
}
    
static void addElements(const void *key, const void *value, void *context)
{
    CFMutableDictionaryRef dict = (CFMutableDictionaryRef)context;
    
    CFDictionaryAddValue(dict, key, value);
}


static int FixupPrebootMountPointInPaths(CFMutableDictionaryRef dict, const char *mountPoint)
{
	char canonicalPath[MAXPATHLEN];
	CFStringRef				mount;
	int						j;
	CFMutableArrayRef		binfo;
	CFMutableDictionaryRef	word;
	CFStringRef				path;
	CFMutableStringRef		newPath;
	
	realpath(mountPoint, canonicalPath);
	mount = CFStringCreateWithCString(kCFAllocatorDefault, canonicalPath, kCFStringEncodingUTF8);
	binfo = (void *)CFDictionaryGetValue(dict, CFSTR("Bless Info"));
	for (j = 0; j < 2; j++) {
		word = (void *)CFArrayGetValueAtIndex(binfo, j);
		path = CFDictionaryGetValue(word, CFSTR("Path"));
		newPath = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, path);
		CFStringFindAndReplace(newPath, mount, CFSTR(kPrebootIndicator), CFRangeMake(0, CFStringGetLength(newPath)), 0);
		CFDictionarySetValue(word, CFSTR("Path"), newPath);
	}
	return 0;
}
