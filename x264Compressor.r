/*
 *  x264Compressor.r
 *  x264qtcodec
 *
 *  Created by Henry Mason on 12/29/05.
 *  Copyright 2005 Henry Mason. All rights reserved.
 *
 */

// Set to 1 == building Mac OS X
#define TARGET_REZ_CARBON_MACHO 1

#if TARGET_REZ_CARBON_MACHO

	#if defined(ppc_YES)
		// PPC architecture
		#define TARGET_REZ_MAC_PPC 1
	#else
		#define TARGET_REZ_MAC_PPC 0
	#endif

	#if defined(i386_YES)
		// x86 architecture
		#define TARGET_REZ_MAC_X86 1
	#else
		#define TARGET_REZ_MAC_X86 0
	#endif

	#define TARGET_REZ_WIN32 0
#else
	// Must be building on Windows
	#define TARGET_REZ_WIN32 1
#endif

#define thng_RezTemplateVersion 2

#if TARGET_REZ_CARBON_MACHO
#include <Carbon/Carbon.r>
#include <QuickTime/QuickTime.r>
#else
#include "ConditionalMacros.r"
#include "MacTypes.r"
#include "Components.r"
#include "ImageCodec.r"
#endif

//#define	X264_COMPRESSOR_FORMAT_TYPE	'avc1'
#define	X264_COMPRESSOR_FORMAT_TYPE	'x264'
#define	X264_COMPRESSOR_FORMAT_NAME	"H.264 (x264 2012)"
#define X264_COMPRESSOR_FLAGS (codecInfoDoes32 | codecInfoDoesMultiPass | codecInfoDoesTemporal | codecInfoDoesReorder | codecInfoDoesRateConstrain)
#define X264_COMPRESSOR_FORMAT_FLAGS (codecInfoSequenceSensitive | codecInfoDepth24)

// Component Description
resource 'cdci' (256) {
	X264_COMPRESSOR_FORMAT_NAME,	// Type
	1,								// Version
	1,								// Revision level
	'x264',							// Manufacturer
	0,	// Decompression Flags
	X264_COMPRESSOR_FORMAT_FLAGS,	// Compression Flags
	X264_COMPRESSOR_FLAGS,			// Format Flags
	128,							// Compression Accuracy
	128,							// Decomression Accuracy
	200,							// Compression Speed
	200,							// Decompression Speed
	128,							// Compression Level
	0,								// Reserved
	1,								// Minimum Height
	1,								// Minimum Width
	0,								// Decompression Pipeline Latency
	0,								// Compression Pipeline Latency
	0								// Private Data
};

// Component Name
resource 'STR ' (256) {
	"H.264 (x264 2012)"
};

resource 'thng' (258) {
	compressorComponentType,				// Type			
	X264_COMPRESSOR_FORMAT_TYPE,		    // SubType
	'x264',									// Manufacturer
	0,										// - use componentHasMultiplePlatforms
	0,
	0,
	0,
	'STR ',									// Name Type
	256,									// Name ID
	'STR ',									// Info Type
	258,									// Info ID
	0,										// Icon Type
	0,										// Icon ID
	0x00060000,								// Version
	componentHasMultiplePlatforms +			// Registration Flags 
		componentDoAutoVersion,
	0,										// Resource ID of Icon Family
	{
#if TARGET_REZ_CARBON_MACHO
    #if !(TARGET_REZ_MAC_PPC || TARGET_REZ_MAC_X86)
        #error "Platform architecture not defined, TARGET_REZ_MAC_PPC and/or TARGET_REZ_MAC_X86 must be defined!"
    #endif
    
    #if TARGET_REZ_MAC_PPC    
        X264_COMPRESSOR_FLAGS | cmpThreadSafe, 
        'dlle',
        258,
        platformPowerPCNativeEntryPoint,
    #endif
    #if TARGET_REZ_MAC_X86
        X264_COMPRESSOR_FLAGS | cmpThreadSafe, 
        'dlle',
        258,
        platformIA32NativeEntryPoint,
    #endif
#endif

#if TARGET_OS_WIN32
	X264_COMPRESSOR_FLAGS, 
	'dlle',
	258,
	platformWin32,
#endif
	},
	0, 0;
};

// Component Information
resource 'STR ' (258) {
	"H.264 video encoder using the x264 encoder library"
};

// Code Entry Point for Mach-O and Windows
resource 'dlle' (258) {
	"x264_compressor_ComponentDispatch"
};
