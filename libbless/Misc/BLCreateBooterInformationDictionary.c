/*
 * Copyright (c) 2006 Apple Computer, Inc. All rights reserved.
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
 *  BLCreateBooterInformationDictionary.c
 *  bless
 *
 *  Created by Shantonu Sen on 5/22/06.
 *  Copyright 2006 Apple Computer, Inc. All rights reserved.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mount.h>

#include <mach/mach_error.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOPartitionScheme.h>
#include <IOKit/storage/IOApplePartitionScheme.h>
#include <IOKit/storage/IOGUIDPartitionScheme.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>

#include <CoreFoundation/CoreFoundation.h>

#include "bless.h"
#include "bless_private.h"

static int addRAIDInfo(BLContextPtr context, CFDictionaryRef dict,
            CFMutableArrayRef dataPartitions,
            CFMutableArrayRef booterPartitions,
            CFMutableArrayRef systemPartitions);      


static int addDataPartitionInfo(BLContextPtr context, io_service_t dataPartition,
                       CFMutableArrayRef dataPartitions,
                       CFMutableArrayRef booterPartitions,
                       CFMutableArrayRef systemPartitions);      

static int addPreferredSystemPartitionInfo(BLContextPtr context,
                                CFMutableArrayRef systemPartitions);      


static bool isPreferredSystemPartition(BLContextPtr context, CFStringRef bsdName);

/*
 * For the given device, we return the set of Auxiliary Partitions and
 * System Partitions. System Partitions are not OS-specific. This routine
 * works for both APM and GPT disks, as well as IOMedia filters/aggregates
 * Using those partition map types.
 *
 * A given partition may not have an Auxiliary Partitions nor System Partitions,
 * for example a single HFS+ partition on an APM disk, or it may have many
 * APs and SPs, for example a 4-member RAID mirror on GPT disks.
 */

int BLCreateBooterInformationDictionary(BLContextPtr context, const char * bsdName,
                                        CFDictionaryRef *outDict)
{
    CFMutableArrayRef dataPartitions = NULL;
    CFMutableArrayRef booterPartitions = NULL;
    CFMutableArrayRef systemPartitions = NULL;
    CFMutableDictionaryRef booters = NULL;
    
    CFArrayRef      array;
    
    CFTypeRef               bootData = NULL;
    
    io_service_t            rootDev;
    int                     ret = 0;
    bool                    foundPreferredSystemPartition = false;
    
    dataPartitions = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if(dataPartitions == NULL)
        return 1;

    booterPartitions = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if(booterPartitions == NULL)
        return 1;
    
    systemPartitions = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if(systemPartitions == NULL)
        return 1;
    
    booters = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                        &kCFTypeDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);
    if(booters == NULL)
        return 1;

	
    rootDev = IOServiceGetMatchingService(kIOMasterPortDefault,
                                          IOBSDNameMatching(kIOMasterPortDefault, 0, bsdName));
    
    if(rootDev == IO_OBJECT_NULL) {
        CFRelease(booters);
        
        contextprintf(context, kBLLogLevelError,
                      "Could not get IOService for %s\n", bsdName);
        return 2;
    }
    
    if(!IOObjectConformsTo(rootDev,kIOMediaClass)) {
        CFRelease(booters);
        IOObjectRelease(rootDev);
        
        contextprintf(context, kBLLogLevelError,
                      "%s is not an IOMedia object\n", bsdName);
        return 2;        
    }
    
    bootData = IORegistryEntrySearchCFProperty( rootDev,
                                            kIOServicePlane,
                                            CFSTR(kIOBootDeviceKey),
                                            kCFAllocatorDefault,
                                            kIORegistryIterateRecursively|
                                            kIORegistryIterateParents);
    
	if(bootData) {

        // if there's boot data, this is an IOMedia filter/aggregate publishing
        // its data members
        if(CFGetTypeID(bootData) == CFArrayGetTypeID()) {
            CFIndex i, count = CFArrayGetCount(bootData);
            
            for(i=0; i < count; i++) {
                CFDictionaryRef dict = CFArrayGetValueAtIndex((CFArrayRef)bootData,i);
                
                ret = addRAIDInfo(context, dict,
                                  dataPartitions,
                                  booterPartitions,
                                  systemPartitions);            
                if(ret) {
                    break;
                }
                
            }
        } else if( CFGetTypeID(bootData) == CFDictionaryGetTypeID()) {
            
            ret = addRAIDInfo(context, (CFDictionaryRef)bootData,
                              dataPartitions,
                              booterPartitions,
                              systemPartitions);            
        } else {
            contextprintf(context, kBLLogLevelError,
                          "Invalid boot data for %s\n", bsdName);

            ret = 5;;
        }
        
        if(ret) {
            CFRelease(booters);
            IOObjectRelease(rootDev);

            return ret;
            
        }
		
	} else {
        ret = addDataPartitionInfo(context, rootDev,
                                   dataPartitions,
                                   booterPartitions,
                                   systemPartitions);    
        if(ret) {
            CFRelease(booters);
            IOObjectRelease(rootDev);
            
            return ret;
        }
    }
	

    IOObjectRelease(rootDev);
    
gotinfo:
	
    if(getenv("BL_PRIMARY_BOOTER_INDEX")) {
        char *bindex = getenv("BL_PRIMARY_BOOTER_INDEX");
        CFIndex index = atoi(bindex);
        
        if(index >= 0 && index < CFArrayGetCount(booterPartitions)) {
            CFArrayExchangeValuesAtIndices(booterPartitions, 0, index);
        }
    }
    
    // we have a set of systemPartitions. Reorder a preferred one to the front.
    // if not, look for a preferred partition manually
    if(CFArrayGetCount(systemPartitions) > 0) {
        CFIndex i, count;
        count = CFArrayGetCount(systemPartitions);
        for(i=0; i < count; i++) {
            CFStringRef testSP = CFArrayGetValueAtIndex(systemPartitions, i);                
            if(isPreferredSystemPartition(context, testSP)) {
                foundPreferredSystemPartition = true;
                CFArrayExchangeValuesAtIndices(systemPartitions, 0, i);      
                break;
            }
        }
    }
    
    if(!foundPreferredSystemPartition) {
        addPreferredSystemPartitionInfo(context, systemPartitions);            
    }

    array = CFArrayCreateCopy(kCFAllocatorDefault, dataPartitions);
    CFDictionaryAddValue(booters, kBLDataPartitionsKey, array);
    CFRelease(array);
    CFRelease(dataPartitions);
    
    array = CFArrayCreateCopy(kCFAllocatorDefault, booterPartitions);
    CFDictionaryAddValue(booters, kBLAuxiliaryPartitionsKey, array);
    CFRelease(array);
    CFRelease(booterPartitions);
    
    array = CFArrayCreateCopy(kCFAllocatorDefault, systemPartitions);
    CFDictionaryAddValue(booters, kBLSystemPartitionsKey, array);
    CFRelease(array);
    CFRelease(systemPartitions);
    
	contextprintf(context, kBLLogLevelVerbose, "Returning booter information dictionary:\n%s\n",
				  BLGetCStringDescription(booters));
	
    *outDict = booters;
    
    return 0;
}

static int addRAIDInfo(BLContextPtr context, CFDictionaryRef dict,
                       CFMutableArrayRef dataPartitions,
                       CFMutableArrayRef booterPartitions,
                       CFMutableArrayRef systemPartitions)
{
    CFStringRef bootpath = NULL;
    io_string_t iostring;
    io_service_t    service;

    int         ret;
    
    bootpath = CFDictionaryGetValue(dict, CFSTR(kIOBootDevicePathKey));
    if(bootpath == NULL || CFGetTypeID(bootpath) != CFStringGetTypeID()) {
        contextprintf(context, kBLLogLevelError,  "Could not find boot path entry\n");
        return 1;            
    }
    
    if(!CFStringGetCString(bootpath,iostring,sizeof(iostring),kCFStringEncodingUTF8)) {
        contextprintf(context, kBLLogLevelError,  "Invalid UTF8 for path entry\n");
        return 2;                        
    }
    
    contextprintf(context, kBLLogLevelVerbose,  "Aggregate boot path is %s\n" , iostring);
    
    service = IORegistryEntryFromPath(kIOMasterPortDefault, iostring );
    if(service == IO_OBJECT_NULL) {
        contextprintf(context, kBLLogLevelError,  "Could not find IOKit entry for %s\n" , iostring);
        return 4;                                    
    }

    ret = addDataPartitionInfo(context, service,
                               dataPartitions,
                               booterPartitions,
                               systemPartitions);      
    
    IOObjectRelease(service);
    
    return ret;
}

static int addDataPartitionInfo(BLContextPtr context, io_service_t dataPartition,
                                CFMutableArrayRef dataPartitions,
                                CFMutableArrayRef booterPartitions,
                                CFMutableArrayRef systemPartitions)
{
    CFStringRef bsdName;
    kern_return_t   kret;
    io_service_t    parent;
    uint32_t        partitionID, searchID;
    CFNumberRef     partitionNum;
    CFStringRef     content;
    bool            needsBooter = false;
    CFNumberRef     neededBooterPartitionNum = NULL;
    CFStringRef     neededBooterContent = NULL;
    CFStringRef     neededSystemContent = NULL;
    
    io_iterator_t   childIterator;
    io_service_t    child;
    
    /* don't require this at this point
    kret = IORegistryEntryGetPath(dataPartition, kIODeviceTreePlane, devPath);
	if(kret != KERN_SUCCESS) {
		contextprintf(context, kBLLogLevelError,  "Could not get path in device plane for service\n" );
		return 1;
	}
    */
    
    bsdName = IORegistryEntryCreateCFProperty(dataPartition, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
    if(bsdName == NULL || (CFGetTypeID(bsdName) != CFStringGetTypeID())) {
        if(bsdName) CFRelease(bsdName);
        
        return 1;
    }

    partitionNum = IORegistryEntryCreateCFProperty(dataPartition, CFSTR(kIOMediaPartitionIDKey), kCFAllocatorDefault, 0);
    if(partitionNum == NULL || (CFGetTypeID(partitionNum) != CFNumberGetTypeID())) {
        if(partitionNum) CFRelease(partitionNum);
        
        return 1;
    }
    
    content = (CFStringRef)IORegistryEntryCreateCFProperty(dataPartition, CFSTR(kIOMediaContentKey), kCFAllocatorDefault, 0);
    if(content == NULL || (CFGetTypeID(content) != CFStringGetTypeID())) {
        if(content) CFRelease(content);
        contextprintf(context, kBLLogLevelError,  "Partition does not have Content key\n" );
        
        return 1;
    }
    
    
    if(!CFNumberGetValue(partitionNum,kCFNumberSInt32Type, &partitionID)) {
        contextprintf(context, kBLLogLevelError,  "Could not get Partition ID for service\n" );
		return 1;        
    }
    
    if(!CFArrayContainsValue(dataPartitions,CFRangeMake(0, CFArrayGetCount(dataPartitions)), bsdName)) {
        CFArrayAppendValue(dataPartitions, bsdName);
    }
    CFRelease(bsdName);
    CFRelease(partitionNum);
    
    kret = IORegistryEntryGetParentEntry(dataPartition, kIOServicePlane, &parent);
    if(kret != KERN_SUCCESS) {
        contextprintf(context, kBLLogLevelError,  "Could not get parent path in device plane for service\n" );
		return 1;
    }
    
        
    if(IOObjectConformsTo(parent, kIOApplePartitionSchemeClass)) {
        contextprintf(context, kBLLogLevelVerbose,  "APM detected\n" );
        // from the OS point of view, only it's an HFS or boot partition, it needs a booter

        if(CFEqual(content, CFSTR("Apple_HFS"))  ||
           CFEqual(content, CFSTR("Apple_Boot")) ||
           CFEqual(content, CFSTR("Apple_Boot_RAID")) ) {
            needsBooter = false;
        } else {
            needsBooter = true;
            searchID = partitionID - 1;
            neededBooterContent = CFSTR("Apple_Boot");
            neededBooterPartitionNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &searchID);
        }
        
    } else if(IOObjectConformsTo(parent, kIOGUIDPartitionSchemeClass)) {
        contextprintf(context, kBLLogLevelVerbose,  "GPT detected\n" );
        
        // from the OS point of view, only it's an HFS or boot partition, it needs a booter
        if(CFEqual(content, CFSTR("48465300-0000-11AA-AA11-00306543ECAC"))  ||
           CFEqual(content, CFSTR("426F6F74-0000-11AA-AA11-00306543ECAC"))) {
            needsBooter = false;
        } else {
            needsBooter = true;
            searchID = partitionID + 1;
            neededBooterContent = CFSTR("426F6F74-0000-11AA-AA11-00306543ECAC");
            neededBooterPartitionNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &searchID);
        }
        
        neededSystemContent = CFSTR("C12A7328-F81F-11D2-BA4B-00A0C93EC93B");
        
    } else {
        contextprintf(context, kBLLogLevelVerbose,  "Other partition scheme detected\n" );
    }
    
    if(needsBooter) {
        contextprintf(context, kBLLogLevelVerbose,  "Booter partition required at index %u\n", searchID);        
    } else {
        contextprintf(context, kBLLogLevelVerbose,  "No auxiliary booter partition required\n");                
    }
    
    if(needsBooter || neededSystemContent) {
        kret = IORegistryEntryGetChildIterator(parent, kIOServicePlane, &childIterator);
        if(kret != KERN_SUCCESS) {
            contextprintf(context, kBLLogLevelError,  "Could not get child iterator for parent\n" );
            return 4;
        }
        
        while((child = IOIteratorNext(childIterator)) != IO_OBJECT_NULL) {
            CFStringRef childContent;
            
            childContent = IORegistryEntryCreateCFProperty(child, CFSTR(kIOMediaContentKey), kCFAllocatorDefault, 0);
            if(childContent && CFGetTypeID(childContent) == CFStringGetTypeID()) {
                CFStringRef childBSDName;
                // does it match
                if(needsBooter && CFEqual(childContent, neededBooterContent)) {
                    CFNumberRef childPartitionID = IORegistryEntryCreateCFProperty(child, CFSTR(kIOMediaPartitionIDKey), kCFAllocatorDefault, 0);
                    
                    if(childPartitionID && (CFGetTypeID(childPartitionID) == CFNumberGetTypeID()) && CFEqual(childPartitionID, neededBooterPartitionNum)) {
                        childBSDName = IORegistryEntryCreateCFProperty(child, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
                        if(childBSDName && (CFGetTypeID(childBSDName) == CFStringGetTypeID())) {
                            if(!CFArrayContainsValue(booterPartitions,CFRangeMake(0, CFArrayGetCount(booterPartitions)), childBSDName)) {
                                CFArrayAppendValue(booterPartitions, childBSDName);
                            }
                        }
                        CFRelease(childBSDName);
                        contextprintf(context, kBLLogLevelVerbose,  "Booter partition found\n" );                        
                    }
                } else if(neededSystemContent && CFEqual(childContent, neededSystemContent)) {
                    childBSDName = IORegistryEntryCreateCFProperty(child, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
                    if(childBSDName && (CFGetTypeID(childBSDName) == CFStringGetTypeID())) {
                        if(!CFArrayContainsValue(systemPartitions,CFRangeMake(0, CFArrayGetCount(systemPartitions)), childBSDName)) {
                            CFArrayAppendValue(systemPartitions, childBSDName);
                        }
                    }
                    CFRelease(childBSDName);
                    contextprintf(context, kBLLogLevelVerbose,  "System partition found\n" );                    
                }
            }
            
            if(childContent) CFRelease(childContent);
            
            IOObjectRelease(child);
        }
        
        IOObjectRelease(childIterator);
        
    }
    
    IOObjectRelease(parent);
    
    return 0;
}

static bool _isPreferredSystemPartition(BLContextPtr context, io_service_t service)
{
    CFDictionaryRef         protocolCharacteristics;
    bool                    foundOne = false;
    
    protocolCharacteristics = IORegistryEntrySearchCFProperty(service,
                                                              kIOServicePlane,
                                                              CFSTR(kIOPropertyProtocolCharacteristicsKey),
                                                              kCFAllocatorDefault,
                                                              kIORegistryIterateRecursively|
                                                              kIORegistryIterateParents);
    
    if(protocolCharacteristics && CFGetTypeID(protocolCharacteristics) == CFDictionaryGetTypeID()) {
        CFStringRef interconnect = CFDictionaryGetValue(protocolCharacteristics,
                                                        CFSTR(kIOPropertyPhysicalInterconnectTypeKey));
        CFStringRef location = CFDictionaryGetValue(protocolCharacteristics,
                                                    CFSTR(kIOPropertyPhysicalInterconnectLocationKey));
        
        if(interconnect && location && CFGetTypeID(interconnect) == CFStringGetTypeID() && CFGetTypeID(location) == CFStringGetTypeID()) {
            if(  (  CFEqual(interconnect,CFSTR(kIOPropertyPhysicalInterconnectTypeATA))
                  || CFEqual(interconnect,CFSTR(kIOPropertyPhysicalInterconnectTypeSerialATA)))
               && CFEqual(location, CFSTR(kIOPropertyInternalKey))) {
                // OK, found an internal ESP
                foundOne = true;
            }
            
        }
        
    }
    if(protocolCharacteristics) CFRelease(protocolCharacteristics);
    
    return foundOne;
    
}

static bool isPreferredSystemPartition(BLContextPtr context, CFStringRef bsdName)
{
    CFMutableDictionaryRef  matching;
    io_service_t            service;
    bool                    ret;
    
    matching = IOServiceMatching(kIOMediaClass);
    CFDictionarySetValue(matching, CFSTR(kIOBSDNameKey), bsdName);

    
    service = IOServiceGetMatchingService(kIOMasterPortDefault,
                                          matching);
    
    if(service == IO_OBJECT_NULL) {
        return false;
    }
        
    ret = _isPreferredSystemPartition(context, service);
    
    IOObjectRelease(service);
    
    return ret;
}


// search for all ESPs on the system. Once we find them, only add internal
// interconnects on a SATA/PATA bus
static int addPreferredSystemPartitionInfo(BLContextPtr context,
                                           CFMutableArrayRef systemPartitions)
{
    
    CFMutableDictionaryRef  matching;
    kern_return_t           kret;
    io_iterator_t           iter;
    io_service_t            service;
    CFStringRef             bsdName;
    bool                    foundOne = false;
    
    matching = IOServiceMatching(kIOMediaClass);
    CFDictionarySetValue(matching, CFSTR(kIOMediaContentKey), CFSTR("C12A7328-F81F-11D2-BA4B-00A0C93EC93B"));
    
    kret = IOServiceGetMatchingServices(kIOMasterPortDefault, matching, &iter);
    if(kret != KERN_SUCCESS) {
        contextprintf(context, kBLLogLevelVerbose,  "No preferred system partitions found\n" );
        return 0;
    }
    
    while((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        
        foundOne = _isPreferredSystemPartition(context, service);
        if(foundOne) {
            bsdName = IORegistryEntryCreateCFProperty(service, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
            if(bsdName && (CFGetTypeID(bsdName) == CFStringGetTypeID())) {
                contextprintf(context, kBLLogLevelVerbose,  "Preferred system partition found: %s\n", BLGetCStringDescription(bsdName));
                // this is a new preferred ESP. Prepend it to the list
                CFArrayInsertValueAtIndex(systemPartitions, 0, bsdName);
            }
            if(bsdName) CFRelease(bsdName);            

            IOObjectRelease(service);
            break;
        }
                
        IOObjectRelease(service);
    }
    
    IOObjectRelease(iter);
    
    return 0;
}

