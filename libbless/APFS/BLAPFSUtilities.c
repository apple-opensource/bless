/*
 * Copyright (c) 2018-2020 Apple Inc. All Rights Reserved.
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

//
//  BLAPFSUtilities.c
//

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <paths.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOMedia.h>
#include <APFS/APFS.h>
#include <apfs/apfs_fsctl.h>
#include "bless.h"
#include "bless_private.h"
#include "protos.h"

#ifndef    FSOPT_LIST_SNAPSHOT
#define    FSOPT_LIST_SNAPSHOT 0x00000040
#endif/* FSOPT_LIST_SNAPSHOT */

static uint16_t RoleNameToValue(CFStringRef roleName);


//
// GetSpecialRoledVolumeBSDForContainerDev -- Looks up the (assumed only, first) Volume in
//    the given Container that has the given APFS-role. It does not use APFSVolumeRoleFind()
//    and thus does not cause a dependency on the APFS.framework library. The given role
//    must be one of the special ones supported by this routine; they will be roles for which
//    there is at most one (1) volume in a container (for example, there can be multiple
//    System-roled volumes per container because there can be multiple groups per container).
//
// Input: Container, as Dev -- has a preceding /dev/; example: "/dev/disk123"
// Output: Volume, as BSD -- does not have a preceding /dev/; example: "disk123s654"
//

static int GetSpecialRoledVolumeBSDForContainerDev(BLContextPtr context, const char *containerDev, int specialRole, char *roledVolumeBSD, int roledVolumeLen)
{
    int                 ret = 0;
    char                containerBSD[MAXPATHLEN];
    io_service_t        conIOMedia = IO_OBJECT_NULL;
    io_service_t        volIOMedia = IO_OBJECT_NULL;
    io_iterator_t       volIter = IO_OBJECT_NULL;
    CFArrayRef          volRoles = NULL;
    CFStringRef         roleName = NULL;
    CFStringRef         volBSDName = NULL;
    bool                roleMatch = false;
    bool                found = false;
    kern_return_t       kret = KERN_SUCCESS;
    
    if (roledVolumeLen < 1) {
        contextprintf(context, kBLLogLevelError, "Buffer error\n");
        ret = 1;
        goto exit;
    }
    *roledVolumeBSD = '\0';
    
    if (strnlen (containerDev, MAXPATHLEN-1) < 10 /* at least "/dev/diskX" */) {
        contextprintf(context, kBLLogLevelError, "Container dev format error\n");
        ret = 1;
        goto exit;
    }
    sscanf (containerDev, "/dev/%s", containerBSD);
    /* this does not exhaustively catch all param input errors like NULL or super-long */
    
    conIOMedia = IOServiceGetMatchingService(kIOMasterPortDefault, IOBSDNameMatching(kIOMasterPortDefault, 0, containerBSD));
    if (conIOMedia == IO_OBJECT_NULL) {
        contextprintf(context, kBLLogLevelError, "Could not get IOService for %s\n", containerDev);
        ret = 2;
        goto exit;
    }
    if (!IOObjectConformsTo(conIOMedia, APFS_MEDIA_OBJECT)) {
        contextprintf(context, kBLLogLevelError, "%s is not an APFS container\n", containerDev);
        ret = 2;
        goto exit;
    }
    
    kret = IORegistryEntryCreateIterator (conIOMedia, kIOServicePlane, kIORegistryIterateRecursively, &volIter);
    if (kret) {
        contextprintf(context, kBLLogLevelError, "Could not get iterator for volumes\n");
        ret = 2;
        goto exit;
    }
    
    while (IO_OBJECT_NULL != (volIOMedia = IOIteratorNext(volIter)))
    {
        if (IOObjectConformsTo(volIOMedia, APFS_VOLUME_OBJECT))
        {
            volRoles = IORegistryEntryCreateCFProperty(volIOMedia, CFSTR(kAPFSRoleKey), kCFAllocatorDefault, 0);
            /* It is currently (2019+) defined that an APFS Volume have exactly 0 or 1 Role. */
            /* It is not an error for us if a certain Volume in our Container does not have a Role. */
            /* Here we see if we have the caller's desired Role, and skip if not. */
            if ((volRoles != NULL) && (CFArrayGetCount(volRoles) == 1)) {
                roleName = CFArrayGetValueAtIndex(volRoles, 0);
                roleMatch = false;
                switch (specialRole) {
                case APFS_VOL_ROLE_PREBOOT:
                if (CFStringCompare(roleName, CFSTR(kAPFSVolumeRolePreBoot),  0) == kCFCompareEqualTo) { roleMatch = true; } break;
                case APFS_VOL_ROLE_RECOVERY:
                if (CFStringCompare(roleName, CFSTR(kAPFSVolumeRoleRecovery), 0) == kCFCompareEqualTo) { roleMatch = true; } break;
                default: break;
                }
            }
            if (volRoles) { CFRelease(volRoles); volRoles = NULL; }
            
            if (roleMatch) {
                volBSDName = IORegistryEntryCreateCFProperty(volIOMedia, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
                if (!volBSDName) {
                    contextprintf(context, kBLLogLevelError, "No BSD name\n");
                    /* Should never happen. */
                    ret = 2;
                    goto exit;
                }
                CFStringGetCString(volBSDName, roledVolumeBSD, roledVolumeLen, kCFStringEncodingUTF8);
                CFRelease(volBSDName);
                volBSDName = NULL;
                found = true;
                contextprintf(context, kBLLogLevelVerbose, "Found roled volume\n");
                break;
            }
        }
        IOObjectRelease(volIOMedia);
        volIOMedia = IO_OBJECT_NULL;
    }
    
exit:;
    if (volRoles)   CFRelease(volRoles);
    if (volBSDName) CFRelease(volBSDName);
    if (volIOMedia) IOObjectRelease(volIOMedia);
    if (volIter)    IOObjectRelease(volIter);
    if (conIOMedia) IOObjectRelease(conIOMedia);
    contextprintf(context, kBLLogLevelVerbose, "Container %s does%s have a volume%s%s with role 0x%04X\n",
                  containerDev,
                  found ? ""             : " not",
                  found ? " "            : "",
                  found ? roledVolumeBSD : "",
                  specialRole);
    return ret;
}



int BLAPFSCreatePhysicalStoreBSDsFromVolumeBSD(BLContextPtr context, const char *volBSD, CFArrayRef *physBSDs)
{
    io_service_t        volDev;
    kern_return_t       kret = KERN_SUCCESS;
    io_service_t        parent;
    io_service_t        p = IO_OBJECT_NULL;
    io_iterator_t       psIter;
    CFStringRef         bsd;
    CFMutableArrayRef   devs;
    
    
    volDev = IOServiceGetMatchingService(kIOMasterPortDefault, IOBSDNameMatching(kIOMasterPortDefault, 0, volBSD));
    if (volDev == IO_OBJECT_NULL) {
        contextprintf(context, kBLLogLevelError, "Could not get IOService for %s\n", volBSD);
        return 2;
    }
    if (!IOObjectConformsTo(volDev, APFS_VOLUME_OBJECT)) {
        contextprintf(context, kBLLogLevelError, "%s is not an APFS volume\n", volBSD);
        IOObjectRelease(volDev);
        return 2;
    }
    
    // Hierarchy is IOMedia (for physical store 1)                   IOMedia (for physical store 2)
    //                |                                                 |
    //                -> AppleAPFSContainerScheme <----------------------
    //                              |
    //                              -> IOMedia (for synthesized whole disk)
    //                                      |
    //                                      -> AppleAPFSContainer (subclass of IOPartitionScheme)
    //                                                  |
    //                                                  -> IOMedia (individual APFS volume)
    
    // We're at the bottom, trying to get to the top, so we have to go up 4 levels.
    kret = IORegistryEntryGetParentEntry(volDev, kIOServicePlane, &parent);
    if (kret) goto badHierarchy;
    p = parent;
    kret = IORegistryEntryGetParentEntry(p, kIOServicePlane, &parent);
    if (kret) goto badHierarchy;
    IOObjectRelease(p);
    p = parent;
    kret = IORegistryEntryGetParentEntry(p, kIOServicePlane, &parent);
    if (kret) goto badHierarchy;
    IOObjectRelease(p);
    p = parent;
    
    devs = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    IORegistryEntryGetParentIterator(p, kIOServicePlane, &psIter);
    while ((parent = IOIteratorNext(psIter))) {
        if (!IOObjectConformsTo(parent, kIOMediaClass)) {
            contextprintf(context, kBLLogLevelError, "IORegistry hierarchy for %s has unexpected type\n", volBSD);
            IOObjectRelease(parent);
            IOObjectRelease(psIter);
            CFRelease(devs);
            return 2;
        }
        bsd = IORegistryEntryCreateCFProperty(parent, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
        CFArrayAppendValue(devs, bsd);
        CFRelease(bsd);
        IOObjectRelease(parent);
    }
    IOObjectRelease(psIter);
    IOObjectRelease(p);
    p = IO_OBJECT_NULL;
    *physBSDs = devs;
    
badHierarchy:
    if (kret) {
        contextprintf(context, kBLLogLevelError, "Couldn't get physical store IOMedia for %s\n", volBSD);
    }
    if (p != IO_OBJECT_NULL) IOObjectRelease(p);
    IOObjectRelease(volDev);
    return kret ? 3 : 0;
}



int BLMountContainerVolume(BLContextPtr context, const char *bsdName, char *mntPoint, int mntPtStrSize, bool readOnly)
{
    int		ret;
    char    vartmpLoc[MAXPATHLEN];
    char	fulldevpath[MNAMELEN];
    char	*newargv[13];
    char    *installEnv;
	int		i;
    
    installEnv = getenv("__OSINSTALL_ENVIRONMENT");
    if (installEnv && (atoi(installEnv) > 0 || strcasecmp(installEnv, "yes") == 0 || strcasecmp(installEnv, "true") == 0)) {
        strlcpy(vartmpLoc, "/var/tmp/RecoveryTemp", sizeof vartmpLoc);
    } else {
        if (!confstr(_CS_DARWIN_USER_TEMP_DIR, vartmpLoc, sizeof vartmpLoc)) {
            // We couldn't get our path in /var/folders, so just try /var/tmp.
            strlcpy(vartmpLoc, "/var/tmp/", sizeof vartmpLoc);
        }
    }
    if (access(vartmpLoc, W_OK) < 0) {
        contextprintf(context, kBLLogLevelError, "Temporary directory \"%s\" is not writable.\n", vartmpLoc);
    }
	realpath(vartmpLoc, fulldevpath);
    snprintf(mntPoint, mntPtStrSize, "%s/bless.XXXX", fulldevpath);
    if (!mkdtemp(mntPoint)) {
        contextprintf(context, kBLLogLevelError,  "Can't create mountpoint %s\n", mntPoint);
    }
    
    contextprintf(context, kBLLogLevelVerbose, "Mounting at %s\n", mntPoint);
    
    snprintf(fulldevpath, sizeof(fulldevpath), "/dev/%s", bsdName);
    
    newargv[0] = "/sbin/mount";
    newargv[1] = "-t";
    newargv[2] = "apfs";
    newargv[3] = "-o";
    newargv[4] = "perm";
	newargv[5] = "-o";
	newargv[6] = "owners";
    newargv[7] = "-o";
    newargv[8] = "nobrowse";
	i = 9;
	if (readOnly) newargv[i++] = "-r";
    newargv[i++] = fulldevpath;
    newargv[i++] = mntPoint;
    newargv[i] = NULL;
    
    
    contextprintf(context, kBLLogLevelVerbose, "Executing \"%s\"\n", "/sbin/mount");
    
    pid_t p = fork();
    if (p == 0) {
        setuid(geteuid());
        ret = execv("/sbin/mount", newargv);
        if (ret == -1) {
            contextprintf(context, kBLLogLevelError,  "Could not exec %s\n", "/sbin/mount");
        }
        _exit(1);
    }
    
    do {
        p = wait(&ret);
    } while (p == -1 && errno == EINTR);
    
    contextprintf(context, kBLLogLevelVerbose, "Returned %d\n", ret);
    if (p == -1 || ret) {
        contextprintf(context, kBLLogLevelError,  "%s returned non-0 exit status\n", "/sbin/mount");
        rmdir(mntPoint);
        return 3;
    }
    
    return 0;
}



int BLUnmountContainerVolume(BLContextPtr context, char *mntPoint)
{
    int				err;
    char			*newargv[3];
	struct statfs	sfs;
	int				fd;
	
	// We allow the passed-in path to be a general path, not just
	// a mount point, so we first need to resolve it to a mount point
	// or device.
	if (statfs(mntPoint, &sfs) < 0) {
		return errno;
	}
	
    newargv[0] = "/sbin/umount";
    newargv[1] = sfs.f_mntfromname;
    newargv[2] = NULL;
    
    contextprintf(context, kBLLogLevelVerbose, "Executing \"%s\"\n", "/sbin/umount");
    
    pid_t p = fork();
    if (p == 0) {
		fd = open("/dev/null", O_RDWR);
		dup2(fd, STDOUT_FILENO);
		close(fd);
        setuid(geteuid());
        err = execv("/sbin/umount", newargv);
        if (err == -1) {
            contextprintf(context, kBLLogLevelError,  "Could not exec %s\n", "/sbin/umount");
        }
        _exit(1);
    }
    
    do {
        p = wait(&err);
    } while (p == -1 && errno == EINTR);
    
    contextprintf(context, kBLLogLevelVerbose, "Returned %d\n", err);
    if(p == -1 || err) {
        contextprintf(context, kBLLogLevelError,  "%s returned non-0 exit status\n", "/sbin/umount");
        return 3;
    }
    rmdir(sfs.f_mntonname);
    
    return 0;
}


int BLMountSnapshot(BLContextPtr context, const char *bsdName, const char *snapName, char *mntPoint, int mntPtStrSize)
{
    int        ret;
    char    vartmpLoc[MAXPATHLEN];
    char    fulldevpath[MNAMELEN];
    char    snapNameLoc[MNAMELEN];
    char    *newargv[14];
    char    *installEnv;
    
    installEnv = getenv("__OSINSTALL_ENVIRONMENT");
    if (installEnv && (atoi(installEnv) > 0 || strcasecmp(installEnv, "yes") == 0 || strcasecmp(installEnv, "true") == 0)) {
        strlcpy(vartmpLoc, "/var/tmp/RecoveryTemp", sizeof vartmpLoc);
    } else {
        if (!confstr(_CS_DARWIN_USER_TEMP_DIR, vartmpLoc, sizeof vartmpLoc)) {
            // We couldn't get our path in /var/folders, so just try /var/tmp.
            strlcpy(vartmpLoc, "/var/tmp/", sizeof vartmpLoc);
        }
    }
    if (access(vartmpLoc, W_OK) < 0) {
        contextprintf(context, kBLLogLevelError, "Temporary directory \"%s\" is not writable.\n", vartmpLoc);
    }
    realpath(vartmpLoc, fulldevpath);
    snprintf(mntPoint, mntPtStrSize, "%s/snapshot.XXXX", fulldevpath);
    if (!mkdtemp(mntPoint)) {
        contextprintf(context, kBLLogLevelError,  "Can't create mountpoint %s\n", mntPoint);
    }
    
    contextprintf(context, kBLLogLevelVerbose, "Mounting at %s\n", mntPoint);
    
    snprintf(fulldevpath, sizeof(fulldevpath), "/dev/%s", bsdName);
    snprintf(snapNameLoc, sizeof(snapNameLoc), "%s", snapName);
    
    newargv[0] = "/sbin/mount_apfs";
    newargv[1] = "-s";
    newargv[2] = snapNameLoc;
    newargv[3] = "-o";
    newargv[4] = "perm";
    newargv[5] = "-o";
    newargv[6] = "owners";
    newargv[7] = "-o";
    newargv[8] = "nobrowse";
    newargv[9] = "-o";
    newargv[10] = "rdonly";
    newargv[11] = fulldevpath;
    newargv[12] = mntPoint;
    newargv[13] = NULL;
    
    
    contextprintf(context, kBLLogLevelVerbose, "Executing \"%s\"\n", "/sbin/mount_apfs");
    
    pid_t p = fork();
    if (p == 0) {
        setuid(geteuid());
        ret = execv("/sbin/mount_apfs", newargv);
        if (ret == -1) {
            contextprintf(context, kBLLogLevelError,  "Could not exec %s\n", "/sbin/mount_apfs");
        }
        _exit(1);
    }
    
    do {
        p = wait(&ret);
    } while (p == -1 && errno == EINTR);
    
    contextprintf(context, kBLLogLevelVerbose, "Returned %d\n", ret);
    if (p == -1 || ret) {
        contextprintf(context, kBLLogLevelError,  "%s returned non-0 exit status\n", "/sbin/mount_apfs");
        rmdir(mntPoint);
        return 3;
    }
    
    return 0;
}



int BLEnsureSpecialAPFSVolumeUUIDPath(BLContextPtr context, const char *volumeDev, int specialRole, bool useGroupUUID, char *subjectPath, int subjectLen, bool *didMount)
{
    int             ret;
    char            specialDevPath[64];
    char            containerDev[MAXPATHLEN];
    struct statfs   *mnts;
    int             mntsize;
    int             i;
    io_service_t    service = IO_OBJECT_NULL;
    CFStringRef     uuidKey = NULL;
    CFStringRef     uuidVal = NULL;
    int             len;

    strlcpy(specialDevPath, _PATH_DEV, sizeof specialDevPath);

    i = -1;
    if ((sscanf(volumeDev, "/dev/disk%d", &i) != 1) || (i == -1)) {
        ret = 6;
        goto exit;
    }
    snprintf(containerDev, sizeof containerDev, "/dev/disk%d", i);
    contextprintf(context, kBLLogLevelVerbose, "Getting path to %s UUID folder on volume with role 0x%04X in container %s\n", useGroupUUID ? "volume group" : "target", specialRole, containerDev);

    ret = GetSpecialRoledVolumeBSDForContainerDev(context,
                                                  containerDev,
                                                  specialRole,
                                                  specialDevPath + strlen(_PATH_DEV),
                                                  sizeof specialDevPath - strlen(_PATH_DEV));
    if (ret) goto exit;

    // Check if the given volume is mounted.
    mntsize = getmntinfo(&mnts, MNT_NOWAIT);
    if (!mntsize) {
        ret = 5;
        goto exit;
    }
    for (i = 0; i < mntsize; i++) {
        if (strcmp(mnts[i].f_mntfromname, specialDevPath) == 0) break;
    }
    if (i < mntsize) {
        strlcpy(subjectPath, mnts[i].f_mntonname, subjectLen);
        *didMount = false;
    } else {
        // The preboot volume isn't mounted right now.  We'll have to mount it.
        ret = BLMountContainerVolume(context, specialDevPath + strlen(_PATH_DEV), subjectPath, subjectLen, false);
        if (ret) goto exit;
        *didMount = true;
    }
    ret = BLGetIOServiceForDeviceName(context, volumeDev + strlen(_PATH_DEV), &service);
    if (ret) goto exit;
    uuidKey = useGroupUUID ? CFSTR(kAPFSVolGroupUUIDKey) : CFSTR(kIOMediaUUIDKey);
    uuidVal = IORegistryEntryCreateCFProperty(service, uuidKey, kCFAllocatorDefault, 0);
    if (!uuidVal) {
        ret = EINVAL;
        goto exit;
    }
    len = strlen(subjectPath);
    if (subjectLen <= len + 1) {
        ret = EINVAL;
        goto exit;
    }
    subjectPath[len] = '/';
    CFStringGetCString(uuidVal, subjectPath + len + 1, subjectLen - len - 1, kCFStringEncodingUTF8);
    contextprintf(context, kBLLogLevelVerbose, "Got subject path %s\n", subjectPath);

exit:
    if (service) IOObjectRelease(service);
    if (uuidKey) CFRelease(uuidKey);
    return ret;
}



int BLRoleForAPFSVolumeDev(BLContextPtr context, const char *volumeDev, uint16_t *role)
{
	int                 ret = 0;
	io_service_t        volIOMedia = IO_OBJECT_NULL;
	CFArrayRef          volRoles = NULL;
	CFStringRef         roleName = NULL;
	
	if (strnlen(volumeDev, MAXPATHLEN-1) < 10 /* at least "/dev/diskX" */) {
		contextprintf(context, kBLLogLevelError, "Volume dev format error\n");
		return 1;
	}
	
	volIOMedia = IOServiceGetMatchingService(kIOMasterPortDefault,
											 IOBSDNameMatching(kIOMasterPortDefault, 0, volumeDev + 5));
	if (volIOMedia == IO_OBJECT_NULL) {
		contextprintf(context, kBLLogLevelError, "Could not get IOService for %s\n", volumeDev);
		return 2;
	}
	
	if (IOObjectConformsTo(volIOMedia, "AppleAPFSSnapshot")) {
		io_registry_entry_t volMedia;
		
		contextprintf(context, kBLLogLevelVerbose, "%s is a snapshot volume\n", volumeDev);
		ret = BLAPFSSnapshotToVolume(context, volIOMedia, &volMedia);
		if (ret) {
			contextprintf(context, kBLLogLevelError, "Could not resolve snapshot at %s to a volume\n", volumeDev);
			IOObjectRelease(volIOMedia);
			return ret;
		}
		IOObjectRelease(volIOMedia);
		volIOMedia = volMedia;
	}
	if (!IOObjectConformsTo(volIOMedia, APFS_VOLUME_OBJECT)) {
		contextprintf(context, kBLLogLevelError, "%s is not an APFS volume\n", volumeDev);
		IOObjectRelease(volIOMedia);
		return 2;
	}
	
	volRoles = IORegistryEntryCreateCFProperty(volIOMedia, CFSTR(kAPFSRoleKey), kCFAllocatorDefault, 0);
	if (volRoles && CFArrayGetCount(volRoles) > 1) {
		contextprintf(context, kBLLogLevelError, "Volume at %s has more than one role\n", volumeDev);
		IOObjectRelease(volIOMedia);
		return 3;
	}
	
	if (!volRoles || CFArrayGetCount(volRoles) == 0) {
		*role = APFS_VOL_ROLE_NONE;
	} else {
		roleName = CFArrayGetValueAtIndex(volRoles, 0);
		*role = RoleNameToValue(roleName);
	}

	if (volRoles) CFRelease(volRoles);
	if (volIOMedia) IOObjectRelease(volIOMedia);
	return 0;
}



int BLIsDataRoleForAPFSVolumeDev(BLContextPtr context, const char *volumeDev, bool *isDataRole)
{
	int ret;
	uint16_t role;
	
	ret = BLRoleForAPFSVolumeDev(context, volumeDev, &role);
	if (!ret) {
		*isDataRole = role == APFS_VOL_ROLE_DATA;
	}
	return ret;
}



int BLIsVolumeARV(BLContextPtr context, const char *mountPoint, const char *volBSD, bool *arv)
{
	int	ret;
	bool isARV = false;
    char rootHashPath[MAXPATHLEN];
    struct stat existingStat;
    char prebootMountPoint[MAXPATHLEN];
    char prebootBSD[MAXPATHLEN];
    bool mustUnmount = false;
    char prebootFolderPath[MAXPATHLEN];
	char volDev[64];
	uint16_t role;
	struct attrlist attr_list = {
		.bitmapcount = ATTR_BIT_MAP_COUNT,
		.volattr = ATTR_VOL_INFO | ATTR_VOL_CAPABILITIES,
	};
	struct {
		u_int32_t va_length;
		vol_capabilities_attr_t va_capabilities;
	} __attribute__((aligned(4), packed)) attr_buf;
	
	snprintf(volDev, sizeof volDev, _PATH_DEV "%s", volBSD);
	ret = BLRoleForAPFSVolumeDev(context, volDev, &role);
	if (ret) {
		contextprintf(context, kBLLogLevelError, "Couldn't get role for volume at %s\n", volDev);
		goto exit;
	}
	if (role != APFS_VOL_ROLE_SYSTEM) {
		// Not a system volume, so not ARV.  No further checks needed.
		contextprintf(context, kBLLogLevelVerbose, "Volume does not have system role\n");
		goto exit;
	}
	ret = getattrlist(mountPoint, &attr_list, &attr_buf, sizeof(attr_buf), 0);
	if (ret) {
		contextprintf(context, kBLLogLevelError, "Couldn't get volume attributes for %s: %s\n", mountPoint, strerror(errno));
		goto exit;
	}
	if ((attr_buf.va_capabilities.capabilities[VOL_CAPABILITIES_FORMAT] & VOL_CAP_FMT_SEALED)) {
		contextprintf(context, kBLLogLevelVerbose, "Volume %s is sealable\n", mountPoint);
		isARV = true;
	}
    // If isARV is false, check if OS.dmg.root_hash exists in preboot
    
    // In some UMIA installs, sealing could be skipped but OS.dmg.root_hash would exist.
    // Treat this case same as ARV since the volume should still have ARV semantics.
    
    // Now let's get the BSD name of the preboot volume.
    if (isARV == false) {
        ret = GetPrebootBSDForVolumeBSD(context, volBSD, prebootBSD, sizeof prebootBSD);
        if (ret) {
            contextprintf(context, kBLLogLevelVerbose, "Could not find preboot BSD for : %s\n", volBSD);
            goto exit;
        }
    
        ret = GetMountForBSD(context, prebootBSD, prebootMountPoint, sizeof prebootMountPoint);
        if (ret) {
            contextprintf(context, kBLLogLevelError, "Error looking up mount points\n");
            goto exit;
        }
        if (!prebootMountPoint[0]) {
            // The preboot volume isn't mounted right now.  We'll have to mount it.
            ret = BLMountContainerVolume(context, prebootBSD, prebootMountPoint, sizeof prebootMountPoint, false);
            if (ret) {
                goto exit;
            }
            mustUnmount = true;
        }
        
        ret = GetUUIDFolderPathInPreboot(context, prebootMountPoint, volBSD, prebootFolderPath, sizeof prebootFolderPath);
        if (ret) {
            contextprintf(context, kBLLogLevelError, "Error looking up UUID folder in preboot\n");
            goto exit;
        }
        snprintf(rootHashPath, sizeof rootHashPath, "%s/usr/standalone/OS.dmg.root_hash", prebootFolderPath);
        if (0 == stat(rootHashPath, &existingStat)) {
            isARV = true;
            contextprintf(context, kBLLogLevelVerbose, "%s exists\n", rootHashPath);
        }
    }
exit:
	*arv = isARV;
    if (mustUnmount) {
       BLUnmountContainerVolume(context, prebootMountPoint);
    }
	return ret;
}




int BLAPFSSnapshotToVolume(BLContextPtr context, io_service_t snapshotDev, io_service_t *volumeDev)
{
	io_registry_entry_t	device;
	io_registry_entry_t	parent;
	bool					foundVolume;
	CFStringRef			bsdCF;
	char					bsd[64] = "";
	
	if (!volumeDev) return EINVAL;
	*volumeDev = IO_OBJECT_NULL;
	bsdCF = IORegistryEntryCreateCFProperty(snapshotDev, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
	if (bsdCF) {
		CFStringGetCString(bsdCF, bsd, sizeof bsd, kCFStringEncodingUTF8);
		CFRelease(bsdCF);
	}
	if (!IOObjectConformsTo(snapshotDev, "AppleAPFSSnapshot")) {
		// Not a snapshot and not a volume.  We can't do anything
		// with this.
		contextprintf(context, kBLLogLevelError, "%s is not an APFS snapshot\n", bsd);
		return EINVAL;
	}
	device = snapshotDev;
	IOObjectRetain(snapshotDev);
	foundVolume = false;
	while (IORegistryEntryGetParentEntry(device, kIOServicePlane, &parent) == kIOReturnSuccess) {
		IOObjectRelease(device);
		device = parent;
		if (IOObjectConformsTo(device, APFS_VOLUME_OBJECT)) {
			foundVolume = true;
			break;
		}
	}
	if (!foundVolume) {
		contextprintf(context, kBLLogLevelError, "No corresponding volume for APFS snapshot %s\n", bsd);
		IOObjectRelease(device);
		return 2;
	}
	*volumeDev = device;
	return 0;
}



int BLAPFSSnapshotAsRoot(BLContextPtr context, const char *mountPoint, char *snapName, int nameLen)
{
	int ret = 0;
	int	fd = -1;
	struct attrlist attrList;
	char	attrBuf[4096];
	bool	found = false;
	int	count;
	int	i;
	char	*entry;
	char	*cur;
	int32_t	length;
	attribute_set_t	*returned;
	attrreference_t	*nameAttr;
	char    *name;
    
	fd = open(mountPoint, O_RDONLY);
	if (fd < 0) {
		ret = errno;
		goto exit;
	}
	memset(&attrList, 0, sizeof(attrList));
	attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
	attrList.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_OBJID;
	while (!found) {
		count = getattrlistbulk(fd, &attrList, attrBuf, sizeof attrBuf, FSOPT_LIST_SNAPSHOT);
		if (count < 0) {
			ret = errno;
			goto exit;
		}
		if (count == 0) break;
		for (i = 0, entry = attrBuf; i < count; i++, entry += length) {
			length = *(int32_t *)entry;
			cur = entry + 4;
			returned = (attribute_set_t *)cur;
			cur += sizeof *returned;
			if (returned->commonattr & ATTR_CMN_NAME) {
				nameAttr = (attrreference_t *)cur;
				name = cur + nameAttr->attr_dataoffset;
				cur += sizeof *nameAttr;
			}
			if (returned->commonattr & ATTR_CMN_OBJID) {
				uint64_t xid = * ( uint64_t * )cur;
				if ((xid & SNAPSHOT_MARKED_AS_ROOT_TO_BIT) == 0) continue;
				if (snapName) strlcpy(snapName, name, nameLen);
				found = true;
				break;
			} 	else {
				ret = EINVAL;
				goto exit;
			}
		}
	}
	if (!found) ret = ENOENT;
	
exit:

	if (fd >= 0) close(fd);
	return ret;
}

static uint16_t RoleNameToValue(CFStringRef roleName)
{
	if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleNone))) {
		return APFS_VOL_ROLE_NONE;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleSystem))) {
		return APFS_VOL_ROLE_SYSTEM;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleUser))) {
		return APFS_VOL_ROLE_USER;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleRecovery))) {
		return APFS_VOL_ROLE_RECOVERY;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleVM))) {
		return APFS_VOL_ROLE_VM;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRolePreBoot))) {
		return APFS_VOL_ROLE_PREBOOT;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleInstaller))) {
		return APFS_VOL_ROLE_INSTALLER;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleData))) {
		return APFS_VOL_ROLE_DATA;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleBaseband))) {
		return APFS_VOL_ROLE_BASEBAND;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleXART))) {
		return APFS_VOL_ROLE_XART;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleInternal))) {
		return APFS_VOL_ROLE_INTERNAL;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleBackup))) {
		return APFS_VOL_ROLE_BACKUP;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleUpdate))) {
		return APFS_VOL_ROLE_UPDATE;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleHardware))) {
		return APFS_VOL_ROLE_HARDWARE;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleSideCar))) {
		return APFS_VOL_ROLE_SIDECAR;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleEnterprise))) {
		return APFS_VOL_ROLE_ENTERPRISE;
	} else if (CFEqual(roleName, CFSTR(kAPFSVolumeRoleIDiags))) {
		return APFS_VOL_ROLE_IDIAGS;
	}
	return -1U;
}

int GetPrebootBSDForVolumeBSD(BLContextPtr context, const char *volBSD, char *prebootBSD, int prebootBSDLen)
{
    int                ret;
    CFDictionaryRef    booterDict;
    CFArrayRef        prebootVols;
    CFStringRef        prebootVol;
    
    ret = BLCreateBooterInformationDictionary(context, volBSD, &booterDict);
    if (ret) {
       contextprintf(context, kBLLogLevelError, "Could not get boot information for device %s\n", volBSD);
        goto exit;
    }
    prebootVols = CFDictionaryGetValue(booterDict, kBLAPFSPrebootVolumesKey);
    if (!prebootVols || !CFArrayGetCount(prebootVols)) {
     contextprintf(context, kBLLogLevelError, "No preboot volume associated with device %s\n", volBSD);
        ret = EINVAL;
        goto exit;
    }
    prebootVol = CFArrayGetValueAtIndex(prebootVols, 0);
    if (CFGetTypeID(prebootVol) != CFStringGetTypeID()) {
     contextprintf(context, kBLLogLevelError, "Badly formed registry entry for preboot volume\n");
        ret = EILSEQ;
        goto exit;
    }
    CFStringGetCString(prebootVol, prebootBSD, prebootBSDLen, kCFStringEncodingUTF8);
    
exit:
    return ret;
}

int GetMountForBSD(BLContextPtr context, const char *bsd, char *mountPoint, int mountPointLen)
{
    struct statfs    *mnts;
    int             mntsize;
    int             i;

    mntsize = getmntinfo(&mnts, MNT_NOWAIT);
    if (!mntsize) {
        return errno;
    }
    for (i = 0; i < mntsize; i++) {
        if (strcmp(mnts[i].f_mntfromname + strlen(_PATH_DEV), bsd) == 0) break;
    }
    if (i < mntsize) {
        strlcpy(mountPoint, mnts[i].f_mntonname, mountPointLen);
    } else {
        *mountPoint = '\0';
    }
    return 0;
}

int GetUUIDFolderPathInPreboot(BLContextPtr context, const char *prebootMountPoint, const char *rootBSD, char *prebootDirPath, int len)
{
    char            prebootFolderPath[MAXPATHLEN];
    char            *pathEnd;
    size_t          pathFreeSpace;
    int             ret = 0;
    CFStringRef     bootUUID = NULL;
    struct stat     existingStat;
    io_service_t    rootMedia = IO_OBJECT_NULL;
    io_service_t    systemMedia = IO_OBJECT_NULL;
    
    // Let's get the IOMedia for this device.
    rootMedia = IOServiceGetMatchingService(kIOMasterPortDefault, IOBSDNameMatching(kIOMasterPortDefault, 0, rootBSD));
    if (!rootMedia) {
        ret = 1;
        goto exit;
    }

    if (IOObjectConformsTo(rootMedia, "AppleAPFSSnapshot")) {
        contextprintf(context, kBLLogLevelVerbose, "%s is a snapshot device\n", rootBSD);
        ret = BLAPFSSnapshotToVolume(context, rootMedia, &systemMedia);
        if (ret) {
            contextprintf(context, kBLLogLevelError, "Could not resolve snapshot at %s to volume\n", rootBSD);
            goto exit;
        }
    } else {
        systemMedia = rootMedia;
        IOObjectRetain(systemMedia);
    }

    // Find the appropriate UUID folder
    // We look for a volume UUID folder first.  If that's present, use it.
    // If not, look for a group UUID folder.  If that's present, use it.
    // If not, we fail out.
    snprintf(prebootFolderPath, sizeof prebootFolderPath, "%s/", prebootMountPoint);
    pathEnd = prebootFolderPath + strlen(prebootFolderPath);
    pathFreeSpace = sizeof prebootFolderPath - strlen(prebootFolderPath);
    bootUUID = IORegistryEntryCreateCFProperty(systemMedia, CFSTR(kIOMediaUUIDKey), kCFAllocatorDefault, 0);
    if (!bootUUID) {
        contextprintf(context, kBLLogLevelError, "No valid volume UUID for device %s\n", rootBSD);
        ret = 2;
        goto exit;
    }
    CFStringGetCString(bootUUID, pathEnd, pathFreeSpace, kCFStringEncodingUTF8);
    if (stat(prebootFolderPath, &existingStat) < 0 || !S_ISDIR(existingStat.st_mode)) {
        CFRelease(bootUUID);
        bootUUID = NULL;
    } else {
        contextprintf(context, kBLLogLevelVerbose, "Found system volume UUID path in preboot for %s\n", rootBSD);
    }
    if (!bootUUID) {
        bootUUID = IORegistryEntryCreateCFProperty(systemMedia, CFSTR(kAPFSVolGroupUUIDKey), kCFAllocatorDefault, 0);
        if (bootUUID) CFStringGetCString(bootUUID, pathEnd, pathFreeSpace, kCFStringEncodingUTF8);
        if (!bootUUID || stat(prebootFolderPath, &existingStat) < 0 || !S_ISDIR(existingStat.st_mode)) {
            contextprintf(context, kBLLogLevelError, "No valid group or volume UUID folder for %s\n", rootBSD);
            ret = 2;
            goto exit;
        } else {
            contextprintf(context, kBLLogLevelVerbose, "Found volume group UUID path in preboot for %s\n", rootBSD);
        }
    }
exit:
    if (0 == ret) {
        snprintf(prebootDirPath, len, "%s", prebootFolderPath);
    }
    if (rootMedia) IOObjectRelease(rootMedia);
    if (systemMedia) IOObjectRelease(systemMedia);
    if (bootUUID) CFRelease(bootUUID);
    return ret;
}
