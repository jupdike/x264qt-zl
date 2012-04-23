/*
 *  x264Compressor.c
 *  x264qtcodec
 *
 *  Created by Henry Mason on 12/28/05.
 *  Copyright 2005 Henry Mason. All rights reserved.
 *
 */

#if __APPLE_CC__
    #include <QuickTime/QuickTime.h>
#else
    #include <ConditionalMacros.h>
    #include <Endian.h>
    #include <ImageCodec.h>
#endif

#include "x264.h"

#import <sys/types.h>
#import <sys/sysctl.h>

//#define rebug(x)
#define rebug(x) (fprintf(stderr, "[rebug] %s (%d) %s\n", __func__, __LINE__, x))

typedef struct x264_compressor_globals_struct
{
	ComponentInstance 	self;
	ComponentInstance	target;
	
	ICMCompressorSessionRef 		session; /* NOTE: we do not need to retain or release this */
	ICMCompressionSessionOptionsRef	session_options;
	
	CFMutableArrayRef source_frame_queue;
	
    x264_t          *encoder;
    x264_param_t    params;
    x264_picture_t  source_picture;
    Boolean         source_picture_allocated;
	Boolean			emit_frames;

    CFStringRef     stat_file_path;
	
    TimeValue64     total_duration;
    TimeScale       time_scale;
    unsigned int    total_frame_count;

	WindowRef settings_window;
	Boolean settings_okayed;
	
} x264_compressor_globals_t;

#define STAT_FILE_PATH_BUFFER_SIZE  1024

#define X264_COMPRESSOR_VERSION 	(0x00060000 + X264_BUILD)
#define X264_COMPRESSOR_FLAGS (codecInfoDoes32 | codecInfoDoesMultiPass | codecInfoDoesTemporal | codecInfoDoesReorder | codecInfoDoesRateConstrain)
#define X264_COMPRESSOR_FORMAT_FLAGS (codecInfoSequenceSensitive | codecInfoDepth24)

/* Setup required for ComponentDispatchHelper.c */
#define IMAGECODEC_BASENAME() 		x264_compressor_
#define IMAGECODEC_GLOBALS() 		x264_compressor_globals_t *storage

#define CALLCOMPONENT_BASENAME()	IMAGECODEC_BASENAME()
#define	CALLCOMPONENT_GLOBALS()		IMAGECODEC_GLOBALS()

#define QT_BASENAME()				CALLCOMPONENT_BASENAME()
#define QT_GLOBALS()				CALLCOMPONENT_GLOBALS()

#define COMPONENT_UPP_PREFIX()		uppImageCodec
#define COMPONENT_DISPATCH_FILE		"x264CompressorDispatch.h"
#define COMPONENT_SELECT_PREFIX()  	kImageCodec

#if __APPLE_CC__
#include <CoreServices/Components.k.h>
#include <QuickTime/ImageCodec.k.h>
#include <QuickTime/ImageCompression.k.h>
#include <QuickTime/ComponentDispatchHelper.c>
#else
#include <Components.k.h>
#include <ImageCodec.k.h>
#include <ImageCompression.k.h>
#include <ComponentDispatchHelper.c>
#endif

/* Thanks to Apple for some of these functions */

static void copy_2vuy_to_planar_YUV420(size_t width, size_t height, 
									   const UInt8 *baseAddr_2vuy, size_t rowBytes_2vuy,
									   UInt8 *baseAddr_y, size_t rowBytes_y, 
									   UInt8 *baseAddr_u, size_t rowBytes_u, 
									   UInt8 *baseAddr_v, size_t rowBytes_v)
{
	size_t x, y;
	const UInt8 *lineBase_2vuy = baseAddr_2vuy;
	UInt8 *lineBase_y = baseAddr_y;
	UInt8 *lineBase_u = baseAddr_u;
	UInt8 *lineBase_v = baseAddr_v;
	for (y = 0; y < height; y += 2) {
		/* Take two lines at a time and average the U and V samples. */
		const UInt8 *pixelPtr_2vuy_top = lineBase_2vuy;
		const UInt8 *pixelPtr_2vuy_bot = lineBase_2vuy + rowBytes_2vuy;
		UInt8 *pixelPtr_y_top = lineBase_y;
		UInt8 *pixelPtr_y_bot = lineBase_y + rowBytes_y;
		UInt8 *pixelPtr_u = lineBase_u;
		UInt8 *pixelPtr_v = lineBase_v;
		for (x = 0; x < width; x += 2) {
			/* 2vuy contains samples clustered Cb, Y0, Cr, Y1.  */
			/* Convert a 2x2 block of pixels from two 2vuy pixel blocks to 4 separate Y samples, 1 U and 1 V. */
			pixelPtr_y_top[0] = pixelPtr_2vuy_top[1];
			pixelPtr_y_top[1] = pixelPtr_2vuy_top[3];
			pixelPtr_y_bot[0] = pixelPtr_2vuy_bot[1];
			pixelPtr_y_bot[1] = pixelPtr_2vuy_bot[3];
			pixelPtr_u[0] = ( pixelPtr_2vuy_top[0] + pixelPtr_2vuy_bot[0] ) / 2;
			pixelPtr_v[0] = ( pixelPtr_2vuy_top[2] + pixelPtr_2vuy_bot[2] ) / 2;
			/* Advance to the next 2x2 block of pixels. */
			pixelPtr_2vuy_top += 4;
			pixelPtr_2vuy_bot += 4;
			pixelPtr_y_top += 2;
			pixelPtr_y_bot += 2;
			pixelPtr_u += 1;
			pixelPtr_v += 1;
		}
		lineBase_2vuy += 2 * rowBytes_2vuy;
		lineBase_y += 2 * rowBytes_y;
		lineBase_u += rowBytes_u;
		lineBase_v += rowBytes_v;
	}
}

/* Utility to add an SInt32 to a CFMutableDictionary. */
static void add_int_to_dictionary(CFMutableDictionaryRef dictionary, CFStringRef key, SInt32 numberSInt32)
{
	CFNumberRef number = CFNumberCreate( NULL, kCFNumberSInt32Type, &numberSInt32 );
	if (!number) return;
	CFDictionaryAddValue(dictionary, key, number);
	CFRelease(number);
}

/* Utility to add a double to a CFMutableDictionary. */
static void add_double_to_dictionary(CFMutableDictionaryRef dictionary, CFStringRef key, double numberDouble)
{
	CFNumberRef number = CFNumberCreate( NULL, kCFNumberDoubleType, &numberDouble );
	if (!number) return;
	CFDictionaryAddValue(dictionary, key, number);
	CFRelease(number);
}

/* These functions are mine. */

static pascal OSStatus settings_window_event_handler(EventHandlerCallRef handler, EventRef event, void *user_data)
{
	x264_compressor_globals_t *glob = (x264_compressor_globals_t *)user_data;
  	HICommand command;
	WindowRef window;
	
  	window = glob->settings_window;

  	GetEventParameter(event, kEventParamDirectObject, typeHICommand, NULL, sizeof(command), NULL, &command);
	
	switch (command.commandID) {
		case kHICommandOK:
			glob->settings_okayed = TRUE;
		    QuitAppModalLoopForWindow(window);
			break;
	  	case kHICommandCancel:
			glob->settings_okayed = FALSE;
			QuitAppModalLoopForWindow(window);
			break;
	}
}

static void x264_compress_log(void *blah, int i_level, const char *psz, va_list foo)
{
	/* we really don't care */
}

static ComponentResult release_source_frame_for_encoded(x264_compressor_globals_t *glob, x264_picture_t *picture)
{
	// find the source frame that matches the "outputted" frame
	int count = CFArrayGetCount(glob->source_frame_queue);
	int i;
	ICMCompressorSourceFrameRef CF_value = NULL;
	ICMCompressorSourceFrameRef CF_value_temp = NULL;
	
	for (i = 0; i < count; i++) {
		TimeValue64 displayTimeStamp, displayDuration;
		TimeScale timeScale;
		CF_value_temp = (ICMCompressorSourceFrameRef)CFArrayGetValueAtIndex(glob->source_frame_queue, i);
		ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(CF_value_temp, &displayTimeStamp, &displayDuration, &timeScale, NULL);

		if (displayTimeStamp == picture->i_pts) {
			CF_value = CF_value_temp;
			CFArrayRemoveValueAtIndex(glob->source_frame_queue, i);
			break;
		}
	}
	
	// if we can't find a matching frame, what should we do?
	if (CF_value == NULL) return 0;
	
	ICMCompressorSourceFrameRelease(CF_value);
	return 0;
}


static ComponentResult emit_frame_from_nals(x264_compressor_globals_t *glob, x264_nal_t *nals, int i_nals, x264_picture_t *picture, int frame_size)
{
    ComponentResult err = noErr;    
    ICMMutableEncodedFrameRef frame;
    ICMFrameType frame_type;
    ICMCompressorSourceFrameRef source_frame;
    MediaSampleFlags media_sample_flags;
    TimeValue64 display_duration, decode_time_stamp;
    unsigned int i, buffer_space_left = 1024 * 1024, total_size;
    unsigned char *frame_data_ptr;

    media_sample_flags = 0;
    
    switch (picture->i_type) {
        case X264_TYPE_B:       
            media_sample_flags |= mediaSampleIsNotDependedOnByOthers;
        case X264_TYPE_P:
        case X264_TYPE_BREF:   
        default:
            media_sample_flags |= mediaSampleNotSync;
            break;
        case X264_TYPE_I:
        case X264_TYPE_IDR:
            media_sample_flags |= mediaSampleIsDependedOnByOthers;
    }
    
    switch (picture->i_type) {
        case X264_TYPE_I:
        case X264_TYPE_IDR:
            frame_type = kICMFrameType_I;
            break;
        case X264_TYPE_P:
            frame_type = kICMFrameType_P;
            break;
        case X264_TYPE_B:   
        case X264_TYPE_BREF:   
            frame_type = kICMFrameType_B;
            break;
        default:
            frame_type = kICMFrameType_Unknown;
    }
    
    source_frame = (ICMCompressorSourceFrameRef)CFArrayGetValueAtIndex(glob->source_frame_queue, 0);
    
    err = ICMEncodedFrameCreateMutable(glob->session, source_frame, buffer_space_left, &frame);
    if (err) return err;       
    
    err = ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(source_frame, &decode_time_stamp, &display_duration, NULL, NULL);
    if (err) { ICMEncodedFrameRelease(frame); return err; }
    
    err = ICMEncodedFrameSetDisplayDuration(frame, display_duration);
    if (err) { ICMEncodedFrameRelease(frame); return err; }
    
    err = ICMEncodedFrameSetDecodeTimeStamp(frame, decode_time_stamp);
    if (err) { ICMEncodedFrameRelease(frame); return err; }
    
    err = ICMEncodedFrameSetDisplayTimeStamp(frame, picture->i_pts);
    if (err) { ICMEncodedFrameRelease(frame); return err; }  
    
    err = ICMEncodedFrameSetMediaSampleFlags(frame, media_sample_flags);
    if (err) { ICMEncodedFrameRelease(frame); return err; }
    
    err = ICMEncodedFrameSetFrameType(frame, frame_type);
    if (err) { ICMEncodedFrameRelease(frame); return err; }
    
    err = ICMEncodedFrameSetValidTimeFlags(frame, kICMValidTime_DisplayTimeStampIsValid);
    if (err) { ICMEncodedFrameRelease(frame); return err; }
    
    frame_data_ptr = ICMEncodedFrameGetDataPtr(frame);
    
    
    //x264_nal_t* nals;
    //int i_nals;
    //int frame_size = x264_encoder_encode(encoder, &nals, &i_nals, &pic_in, &pic_out);
    uint8_t* buf = frame_data_ptr;
    if (frame_size >= 0 && buf) // OOPS! = (uint8_t*)malloc(frame_size)))
    {
        //printf ("frame_size = %d\n", frame_size);
        int sum = 0;
        for (int i = 0; i < i_nals; i++)
        {
            //printf ("payload size of nal %d = %d\n", i, nals[i].i_payload);
            memcpy((buf+sum), nals[i].p_payload, nals[i].i_payload);
            sum += nals[i].i_payload;
        }
        if (sum != frame_size)
        {
            fprintf(stderr, "[ERROR] NAL units don't add up to frame_size\n");
            exit(1);
        }
        //bool isDiscont = pic_out.b_keyframe; //framenum == 0;
        //printf ("%s\n", isDiscont ? "discont" : "delta");
        //deposit (frame_size, buf, w, h, isDiscont);
        //free(buf);
    }

/*    
    ///
    
    //void x264_nal_encode( x264_t *h, uint8_t *dst, x264_nal_t *nal );
    total_size = 0;
    for (i = 0; i < nal_count; i++) {
        //void x264_nal_encode( x264_t *h, uint8_t *dst, x264_nal_t *nal );
        //x264_nal_encode(frame_data_ptr, &buffer_space_left, &nal[i]);
        //TODO changed this
        x264_nal_encode(glob->encoder, (uint8_t*)frame_data_ptr, &nal[i]);

        int encoded_size = nal[i].i_payload-4;
        
        // need to set the size of this frame for some reason(?)
        
        if (encoded_size > 0) {
            *(UInt32 *)frame_data_ptr = EndianU32_NtoB(encoded_size - sizeof(UInt32));
            frame_data_ptr += encoded_size;
            total_size += encoded_size;
        }
    }
    ///
    
    */
    
    total_size = frame_size; // = sum; (checked to be equal, above)
    
    err = ICMEncodedFrameSetDataSize(frame, total_size);
    if (err) { ICMEncodedFrameRelease(frame); return err; }

    err = ICMCompressorSessionEmitEncodedFrame(glob->session, frame, 1, &source_frame);                     
    ICMEncodedFrameRelease(frame);
	if (err) return err;

    CFArrayRemoveValueAtIndex(glob->source_frame_queue, 0);
    ICMCompressorSourceFrameRelease(source_frame);

    return err;
}


static CFStringRef create_temp_file_path()
{
    FSRef temp_folder;
	CFURLRef temp_folder_URL;
	CFStringRef temp_folder_path;
    CFStringRef ret;
    ComponentResult err;

	err = FSFindFolder(kOnAppropriateDisk, kTemporaryFolderType, TRUE, &temp_folder);    
    if (err)
        return NULL;  
    
    temp_folder_URL = CFURLCreateFromFSRef(NULL, &temp_folder);
    temp_folder_path = CFURLCopyFileSystemPath(temp_folder_URL, kCFURLPOSIXPathStyle);
    srandom(time(NULL));
    ret = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@/x264Codec-statfile-%ld"), temp_folder_path, random());
    
	CFRelease(temp_folder_URL);
	CFRelease(temp_folder_path);

    return ret;
}

ComponentResult x264_compressor_Open(x264_compressor_globals_t *glob, ComponentInstance self)
{
	CFStringRef temp_file_path;
    int	mib[2];
    size_t len;
	
	glob = calloc(sizeof(x264_compressor_globals_t), 1);
	if (!glob)
		return memFullErr;

	SetComponentInstanceStorage(self, (Handle)glob);

	glob->self = self;
	glob->target = self;

/*	
	x264_param_default(&glob->params);
    
    glob->params.i_bframe = 2;
    glob->params.i_bframe_adaptive = TRUE;
    glob->params.analyse.b_chroma_me = TRUE;
	glob->params.analyse.b_psnr = FALSE;
	glob->params.b_cabac = FALSE;
*/


    // hacked to be zero latency for streaming, etc.

    x264_param_t* param = &glob->params;

    x264_param_default_preset(param, "veryfast", "zerolatency");
    param->i_threads = 1;
    //param->i_width = width;
    //param->i_height = height;
    //param->i_fps_num = fps;
    //param->i_fps_den = 1;
    // Intra refres:
    param->i_keyint_max = 20;  // TODO ?????  //fps;
    param->b_intra_refresh = 0; //1;
    //Rate control:
    param->rc.i_rc_method = X264_RC_CRF;
    param->rc.f_rf_constant = 25;
    param->rc.f_rf_constant_max = 35;
    //For streaming:
    param->b_repeat_headers = 1;
    param->b_annexb = 1;
    x264_param_apply_profile(param, "baseline");


    rebug("");
	
	/* Get the CPU count via sysctl */
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU;
	len = sizeof(glob->params.i_threads);
    sysctl(mib, 2, &glob->params.i_threads, &len, NULL, 0);
    
    glob->encoder = NULL;

	temp_file_path = create_temp_file_path();

    glob->params.rc.psz_stat_out = malloc(STAT_FILE_PATH_BUFFER_SIZE);
	CFStringGetFileSystemRepresentation(temp_file_path, glob->params.rc.psz_stat_out, STAT_FILE_PATH_BUFFER_SIZE);
	
    glob->params.rc.psz_stat_in = malloc(STAT_FILE_PATH_BUFFER_SIZE);
	CFStringGetFileSystemRepresentation(temp_file_path, glob->params.rc.psz_stat_in, STAT_FILE_PATH_BUFFER_SIZE);

	CFRelease(temp_file_path);
	
	return noErr;
}

ComponentResult x264_compressor_Close(x264_compressor_globals_t *glob, ComponentInstance self)
{
	if (glob) {
		
		if (glob->encoder) {
	        x264_encoder_close(glob->encoder);
	        glob->encoder = NULL;
	    }
	
		if (glob->session_options) {
	        ICMCompressionSessionOptionsRelease(glob->session_options);
	        glob->session_options = NULL;
	    }
	
		if (glob->stat_file_path) {
	        CFURLRef stat_file_URL;
	
			stat_file_URL = CFURLCreateWithFileSystemPath(NULL, glob->stat_file_path, kCFURLPOSIXPathStyle, FALSE);

	        CFURLDestroyResource(stat_file_URL, NULL);

	        CFRelease(stat_file_URL);

	        CFRelease(glob->stat_file_path);
	        glob->stat_file_path = NULL;
	    }
	
		if (glob->source_picture_allocated) {
	        x264_picture_clean(&glob->source_picture);
	        glob->source_picture_allocated = FALSE;
	    }
	
	    if (glob->source_frame_queue) {
	        CFRelease(glob->source_frame_queue);
	        glob->source_frame_queue = NULL;
	    }
	
	    free(glob->params.rc.psz_stat_in);
	    glob->params.rc.psz_stat_in = NULL;

	    free(glob->params.rc.psz_stat_out);
	    glob->params.rc.psz_stat_out = NULL;
		
		free(glob);
	}
	
	return noErr;
}

ComponentResult x264_compressor_Version(x264_compressor_globals_t *glob)
{
	return X264_COMPRESSOR_VERSION;
}

ComponentResult x264_compressor_Target(x264_compressor_globals_t *glob, ComponentInstance target)
{
	glob->target = target;
	return noErr;
}

ComponentResult x264_compressor_GetCodecInfo(x264_compressor_globals_t *glob, CodecInfo *info)
{
    rebug("");
    
	if (info == NULL) {
		fprintf(stderr, "%s: [ERROR] no info location given\n", __FUNCTION__);
		return paramErr;
	} else {
		info->typeName[0] = sizeof("H.264 (x264)");
		info->typeName[1] = 'H';
		info->typeName[2] = '.';
		info->typeName[3] = '2';
		info->typeName[4] = '6';
		info->typeName[5] = '4';
		info->typeName[6] = ' ';
		info->typeName[7] = '(';
		info->typeName[8] = 'x';
		info->typeName[9] = '2';
		info->typeName[10] = '6';
		info->typeName[11] = '4';
		info->typeName[12] = ')';
		info->typeName[13] = 0;
		info->version = 1;
		info->revisionLevel = 1;
		info->vendor = 'x264';
		info->decompressFlags = 0;
		info->compressFlags = X264_COMPRESSOR_FLAGS;
		info->formatFlags = X264_COMPRESSOR_FORMAT_FLAGS;
		info->compressionAccuracy = 128;
		info->decompressionAccuracy = 128;
		info->compressionSpeed = 200;
		info->decompressionSpeed = 200;
		info->compressionLevel = 128;
		info->resvd = 0;
		info->minimumHeight = 16;
		info->minimumWidth = 16;
		info->decompressPipelineLatency = 0;
		info->compressPipelineLatency = 0;
		info->privateData = 0;
		
	}

	return noErr;
}

ComponentResult x264_compressor_GetMaxCompressionSize(x264_compressor_globals_t *glob, PixMapHandle src, const Rect *srcRect, short depth, CodecQ quality, long *size)
{
	if (!size) {
		fprintf(stderr, "%s: [ERROR] no size location given\n", __FUNCTION__);
		return paramErr;
	}
	*size = (srcRect->right - srcRect->left) * (srcRect->bottom - srcRect->top) * 4;

	return noErr;
}

ComponentResult x264_compressor_RequestSettings(x264_compressor_globals_t *glob, Handle settings, Rect *rp, ModalFilterUPP filter_proc)
{

	ComponentResult err = noErr;
	CFBundleRef bundle;
	IBNibRef nib = NULL;
  	WindowRef window = NULL;
	ControlID control_id;
	ControlRef control;
  	EventTypeSpec event_list[] = {{kEventClassCommand, kEventCommandProcess}};
  	EventHandlerUPP settings_window_event_handler_UPP;
	GrafPtr saved_port;
	
	GetPort(&saved_port);

    rebug("");
    	
	x264_compressor_SetSettings(glob, settings);
	
	bundle = CFBundleGetBundleWithIdentifier(CFSTR("org.vlc.x264.qtcodec"));
	
	err = CreateNibReferenceWithCFBundle(bundle, CFSTR("Settings"), &nib);
	if (err) return err;
	
	err = CreateWindowFromNib(nib, CFSTR("Settings"), &window);
	if (err) return err;
	
	control_id.signature = 'x264';

	/* Adaptive B-Frame Checkbox */
	control_id.id = 128;
  	GetControlByID(window, &control_id, &control);
	SetControl32BitValue(control, glob->params.i_bframe_adaptive);
	
	/* B-frame Pyramids Checkbox */
	control_id.id = 129;
  	GetControlByID(window, &control_id, &control);
	SetControl32BitValue(control, glob->params.i_bframe_pyramid);
	
	/* Deblocking Filter Checkbox */
	control_id.id = 130;
  	GetControlByID(window, &control_id, &control);
	SetControl32BitValue(control, glob->params.b_deblocking_filter);
	
	/* Deblocking Filter Slider */
	control_id.id = 131;
  	GetControlByID(window, &control_id, &control);
	SetControl32BitValue(control, glob->params.i_deblocking_filter_alphac0);
	
	/* CABAC Checkbox */
	control_id.id = 132;
  	GetControlByID(window, &control_id, &control);
	SetControl32BitValue(control, glob->params.b_cabac);
	
	/* Chroma ME Checkbox */
	control_id.id = 133;
  	GetControlByID(window, &control_id, &control);
	SetControl32BitValue(control, glob->params.analyse.b_chroma_me);
	
	/* B-Frame RD Checkbox */
	//control_id.id = 134;
  	//GetControlByID(window, &control_id, &control);
	//SetControl32BitValue(control, glob->params.analyse.b_bframe_rdo);
	
	/* Mixed References Checkbox */
	control_id.id = 135;
  	GetControlByID(window, &control_id, &control);
	SetControl32BitValue(control, glob->params.analyse.b_mixed_references);
	
	glob->settings_window = window;
  	settings_window_event_handler_UPP = NewEventHandlerUPP(settings_window_event_handler);

	InstallWindowEventHandler(window, settings_window_event_handler_UPP, GetEventTypeCount(event_list), event_list, glob, NULL); 

	ShowWindow(window);

  	RunAppModalLoopForWindow(window);
	
	if (glob->settings_okayed) {
		
		/* Adaptive B-Frame Checkbox */
		//control_id.id = 128;
	  	//GetControlByID(window, &control_id, &control);
		//glob->params.b_bframe_adaptive = GetControl32BitValue(control);
		
		/* B-frame Pyramids Checkbox */
		//control_id.id = 129;
	  	//GetControlByID(window, &control_id, &control);
		//glob->params.b_bframe_pyramid = GetControl32BitValue(control);

		/* Deblocking Filter Checkbox */
		control_id.id = 130;
	  	GetControlByID(window, &control_id, &control);
		glob->params.b_deblocking_filter = GetControl32BitValue(control);

		/* Deblocking Filter Slider */
		control_id.id = 131;
	  	GetControlByID(window, &control_id, &control);
		glob->params.i_deblocking_filter_alphac0 = GetControl32BitValue(control);

		/* CABAC Checkbox */
		control_id.id = 132;
	  	GetControlByID(window, &control_id, &control);
		glob->params.b_cabac = GetControl32BitValue(control);

		/* Chroma ME Checkbox */
		control_id.id = 133;
	  	GetControlByID(window, &control_id, &control);
	    glob->params.analyse.b_chroma_me = GetControl32BitValue(control);

		/* B-Frame RD Checkbox */
		//control_id.id = 134;
	  	//GetControlByID(window, &control_id, &control);
		//glob->params.analyse.b_bframe_rdo = GetControl32BitValue(control);

		/* Mixed References Checkbox */
		control_id.id = 135;
	  	GetControlByID(window, &control_id, &control);
	    glob->params.analyse.b_mixed_references = GetControl32BitValue(control);
	
	}
	
	glob->settings_window = NULL;
	
    DisposeWindow(window);

	DisposeEventHandlerUPP(settings_window_event_handler_UPP);

	DisposeNibReference(nib);
	
	x264_compressor_GetSettings(glob, settings);
	
	MacSetPort(saved_port);
	
	return err;
}

ComponentResult x264_compressor_GetSettings(x264_compressor_globals_t *glob, Handle settings)
{
    rebug("");
    SetHandleSize(settings, sizeof(glob->params));
	
	memcpy(*settings, &glob->params, sizeof(glob->params));
	
	return noErr;
}

ComponentResult x264_compressor_SetSettings(x264_compressor_globals_t *glob, Handle settings)
{
    rebug("");

   	char *old_stat_out = glob->params.rc.psz_stat_out;
   	char *old_stat_in = glob->params.rc.psz_stat_in;
   	//char *old_rc_eq = glob->params.rc.psz_rc_eq;

	if (sizeof(glob->params) > GetHandleSize(settings))
		return noErr;
	
	memcpy(&glob->params, *settings, sizeof(glob->params));
	
	/* the pointer to the file paths is an allocated block, we need to retain it */
	glob->params.rc.psz_stat_out = old_stat_out;
	glob->params.rc.psz_stat_in = old_stat_in;
	//glob->params.rc.psz_rc_eq = old_rc_eq;



    // hacked to be zero latency for streaming, etc.
    x264_param_t* param = &glob->params;
    x264_param_default_preset(param, "veryfast", "zerolatency");
    param->i_threads = 1;
    //param->i_width = width;
    //param->i_height = height;
    //param->i_fps_num = fps;
    //param->i_fps_den = 1;
    // Intra refres:
    param->i_keyint_max = 5;  // TODO ?????  //fps;
    param->b_intra_refresh = 0; //1;
    //Rate control:
    param->rc.i_rc_method = X264_RC_CRF;
    param->rc.f_rf_constant = 25;
    param->rc.f_rf_constant_max = 35;
    //For streaming:
    param->b_repeat_headers = 1;
    param->b_annexb = 1;
    x264_param_apply_profile(param, "baseline");
    

    //TODOx

	
	return noErr;
}

ComponentResult x264_compressor_PrepareToCompressFrames(x264_compressor_globals_t *glob,
  														ICMCompressorSessionRef session,
  														ICMCompressionSessionOptionsRef session_options,
  														ImageDescriptionHandle image_description,
  														void *reserved,
  														CFDictionaryRef *compressor_pixel_buffer_attributes_out)
{
	ComponentResult err = noErr;
	Fixed gamma_level;
	CFMutableDictionaryRef pixel_buffer_attributes;
	CFMutableArrayRef pixel_formats;
	OSType pixel_format = k422YpCbCr8CodecType;
    SInt32 data_rate;
	CodecQ quality;
	Fixed frame_rate;
    x264_nal_t *nal;
	x264_t	*temp_encoder;
	UInt8 *avcC_buffer;
	size_t avcC_buffer_size, avcC_buffer_alloc_size;
    Handle avcC_handle;
    int i, nal_i;
    unsigned int sps_count, *sps_indexes;
    unsigned int pps_count, *pps_indexes;
	int b_stat_write_old;
	
	/* store the session */
	glob->session = session;
	
	/* retain the session options */
	ICMCompressionSessionOptionsRelease(glob->session_options);
	glob->session_options = session_options;
	ICMCompressionSessionOptionsRetain(glob->session_options);
	
	/* set a reasonable gamma level */
	gamma_level = kQTCCIR601VideoGammaLevel;
	err = ICMImageDescriptionSetProperty(image_description,
										 kQTPropertyClass_ImageDescription,
										 kICMImageDescriptionPropertyID_GammaLevel,
			   							 sizeof(gamma_level),
										 &gamma_level);
	if (err) return err;
	
	CFStringGetPascalString(CFSTR("H.264 (x264)"), (*image_description)->name, sizeof(Str31), kCFStringEncodingUTF8);
	
        
    glob->params.i_width = (*image_description)->width;
    glob->params.i_height = (*image_description)->height;
	glob->params.i_keyint_max = ICMCompressionSessionOptionsGetMaxKeyFrameInterval(glob->session_options);
	if (!glob->params.i_keyint_max)
		glob->params.i_keyint_max = 150;        // arbitrary; this is seems to be the value that Apple's H.264 encoder uses for automatic
		                                        // x264 defaults to 1 if it's unspecified
	
	err = ICMCompressionSessionOptionsGetProperty(glob->session_options, 
                                                  kQTPropertyClass_ICMCompressionSessionOptions,
                                                  kICMCompressionSessionOptionsPropertyID_Quality,
                                                  sizeof(quality), &quality, NULL);
    if (err) return err;
	glob->params.rc.i_qp_constant = (51 - ((quality * 51) / codecLosslessQuality));
	
	err = ICMCompressionSessionOptionsGetProperty(glob->session_options, 
                                                  kQTPropertyClass_ICMCompressionSessionOptions,
                                                  kICMCompressionSessionOptionsPropertyID_ExpectedFrameRate,
                                                  sizeof(frame_rate), &frame_rate, NULL);
    if (err) return err;
    
	if (frame_rate > 0) {
		glob->params.i_fps_num = frame_rate << 16;
		glob->params.i_fps_den = 65536;	
	}
    
	err = ICMCompressionSessionOptionsGetProperty(glob->session_options, 
                                                  kQTPropertyClass_ICMCompressionSessionOptions,
                                                  kICMCompressionSessionOptionsPropertyID_AverageDataRate,
                                                  sizeof(data_rate), &data_rate, NULL);
    if (err) return err;
	glob->params.rc.i_bitrate = (data_rate * 8) / 1024;
        
	if (glob->params.rc.i_bitrate > 0)
		glob->params.rc.i_rc_method = X264_RC_ABR;
	else 
		glob->params.rc.i_rc_method = X264_RC_CQP;

	glob->params.pf_log = x264_compress_log;
	
	glob->emit_frames = TRUE;

	/* create a pixel buffer attributes dictionary */
	pixel_buffer_attributes = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	pixel_formats = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	CFArrayAppendValue(pixel_formats, CFNumberCreate(NULL, kCFNumberSInt32Type, &pixel_format));
	CFDictionaryAddValue(pixel_buffer_attributes, kCVPixelBufferPixelFormatTypeKey, pixel_formats);
	add_int_to_dictionary(pixel_buffer_attributes, kCVPixelBufferWidthKey, glob->params.i_width);
	add_int_to_dictionary(pixel_buffer_attributes, kCVPixelBufferHeightKey, glob->params.i_height);
	add_int_to_dictionary(pixel_buffer_attributes, kCVPixelBufferExtendedPixelsRightKey, 0);
	add_int_to_dictionary(pixel_buffer_attributes, kCVPixelBufferExtendedPixelsBottomKey, 0);
	add_int_to_dictionary(pixel_buffer_attributes, kCVPixelBufferBytesPerRowAlignmentKey, 16);
	add_double_to_dictionary(pixel_buffer_attributes, kCVImageBufferGammaLevelKey, 2.2);
	CFDictionaryAddValue(pixel_buffer_attributes, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4);
	*compressor_pixel_buffer_attributes_out = pixel_buffer_attributes;
	pixel_buffer_attributes = NULL;
    
	glob->source_frame_queue = CFArrayCreateMutable(NULL, 100, NULL);
    
	/* allocate x264 source frame */
    if (glob->source_picture_allocated) {
        x264_picture_clean(&glob->source_picture);
        glob->source_picture_allocated = FALSE;
    }
    
    x264_picture_alloc(&glob->source_picture, X264_CSP_I420, glob->params.i_width, glob->params.i_height);
    glob->source_picture_allocated = TRUE;

	/* don't want temp encoder to overwrite our log file */
	b_stat_write_old = glob->params.rc.b_stat_write;
	glob->params.rc.b_stat_write = FALSE;
	
    /* JFU hack to fix crashing with encoder.c in x264_encoder_open at the lines x264_reduce_fraction() */
    glob->params.i_fps_den = 1;
    glob->params.i_fps_num = 24;
    
	/* need to briefly open an encoder in order to get the SPS/PPS */  
    temp_encoder = x264_encoder_open(&glob->params);
    
    x264_encoder_headers(temp_encoder, &nal, &nal_i);

 	sps_count = 0;
	sps_indexes = malloc(nal_i * sizeof(int));
    
	pps_count = 0;
	pps_indexes = malloc(nal_i * sizeof(int));
    
	/* scan through the NALs to find the SPSs and PPSs */
	for (i = 0; i < nal_i; i++) {            
	    switch (nal[i].i_type) {
	        case NAL_SPS:
	            sps_indexes[sps_count] = i;
	            sps_count++;                    
	            break;
	        case NAL_PPS:
	            pps_indexes[pps_count] = i;
	            pps_count++;   
	    }
	}
        
	if (!sps_count) {
		fprintf(stderr, "%s: [ERROR] no SPS found!\n", __FUNCTION__);
		free(sps_indexes);
		free(pps_indexes);
		return paramErr;
	}
	
	/* construct an 'avcC' atom */
	avcC_buffer_alloc_size = 2048;
	avcC_buffer_size = 0;
	avcC_buffer = malloc(avcC_buffer_alloc_size);
	
    /* 1 byte version = 8-bit hex version  (current = 1) */
	avcC_buffer[avcC_buffer_size] = 0x1;
	avcC_buffer_size += sizeof(UInt8);
	
	/* AVCProfileIndication = 8-bit */
    avcC_buffer[avcC_buffer_size] = (UInt8)nal[sps_indexes[0]].p_payload[0];
	avcC_buffer_size += sizeof(UInt8);
	
	/* profile_compatibility = 8-bit */
    avcC_buffer[avcC_buffer_size] = (UInt8)nal[sps_indexes[0]].p_payload[1];
	avcC_buffer_size += sizeof(UInt8);
	
	/* AVCLevelIndication = 8-bit */
    avcC_buffer[avcC_buffer_size] = (UInt8)nal[sps_indexes[0]].p_payload[2];
	avcC_buffer_size += sizeof(UInt8);
	
	/* 6 bits of 111111 + 2 bits of lengthSizeMinusOne = 8-bit total */
	/* we seem to always use 4 bytes */
    avcC_buffer[avcC_buffer_size] = (UInt8)0xFC + (UInt8)0x3;
	avcC_buffer_size += sizeof(UInt8);
	
    /* 3 bits of 111 + 5 bits numOfSequenceParameterSets = 8-bit total */ 
    avcC_buffer[avcC_buffer_size] = (UInt8)0xE0 + (UInt8)sps_count;
	avcC_buffer_size += sizeof(UInt8);
    
	/* Append all the SPSs */
    for (i = 0; i < sps_count; i++) {
		char sps_buffer[2048];
        int sps_index = sps_indexes[i];             
        int buffer_size = sizeof(sps_buffer);
        
        /* Encode the SPS NAL */
        //buffer_size =
        // TODO changed this
        //x264_nal_encode((uint8_t*)sps_buffer, &buffer_size, &nal[sps_index]);

        x264_nal_encode(temp_encoder, (uint8_t*)sps_buffer, &nal[sps_index]);
            
        buffer_size = nal[i].i_payload-4;
        
		/* Look before we leap */
		if (avcC_buffer_alloc_size < (avcC_buffer_size + buffer_size)) {
			avcC_buffer_alloc_size += buffer_size + 1024;
			avcC_buffer = realloc(avcC_buffer, avcC_buffer_alloc_size);
		}

        /* Now we need to lop off the start of the SPS, which is just the size in 32-bit int form. */
        buffer_size -= sizeof(UInt32);
        
        /* sequenceParameterSetLength (16-bit BE int) */
	    avcC_buffer[avcC_buffer_size] = buffer_size >> 8;
	    avcC_buffer[avcC_buffer_size + 1] = buffer_size & 0xFF;
		avcC_buffer_size += sizeof(UInt16);
        
        /* sequenceParameterSetNALUnit (advanced 4 bytes for the size, which we don't need) */
		memcpy(avcC_buffer + avcC_buffer_size, sps_buffer + sizeof(UInt32), buffer_size);
		avcC_buffer_size += buffer_size;
    }
        
	/* numOfPictureParameterSets = 8-bits */
    avcC_buffer[avcC_buffer_size] = (UInt8)pps_count;
	avcC_buffer_size += sizeof(UInt8);
    
	/* Append all the PPSs */
	for (i = 0; i < pps_count; i++) {
		char pps_buffer[2048];
	    int pps_index = pps_indexes[i];             
	    int buffer_size = sizeof(pps_buffer);
    
	    /* Encode the SPS NAL */
        // TODO changed this
	    //buffer_size = x264_nal_encode(pps_buffer, &buffer_size, 1, &nal[pps_index]);
        x264_nal_encode(temp_encoder, (uint8_t*)pps_buffer, &nal[pps_index]);
        // TODO &buffer_size = ?
        
		/* Look before we leap */
		if (avcC_buffer_alloc_size < (avcC_buffer_size + buffer_size)) {
			avcC_buffer_alloc_size += buffer_size + 1024;
			avcC_buffer = realloc(avcC_buffer, avcC_buffer_alloc_size);
		}

	    /* Now we need to lop off the start of the SPS, which is just the size in 32-bit int form. */
	    buffer_size -= sizeof(UInt32);
    
	    /* sequenceParameterSetLength (16-bit BE int) */
	    avcC_buffer[avcC_buffer_size] = buffer_size >> 8;
	    avcC_buffer[avcC_buffer_size + 1] = buffer_size & 0xFF;
		avcC_buffer_size += sizeof(UInt16);
    
	    /* pictureParameterSetNALUnit (advanced 4 bytes for the size, which we don't need) */
		memcpy(avcC_buffer + avcC_buffer_size, pps_buffer + sizeof(UInt32), buffer_size);
		avcC_buffer_size += buffer_size;
	}
        
	/* Handle-ize it */
    PtrToHand(avcC_buffer, &avcC_handle, avcC_buffer_size);
	
	/* Add avcC to the image description */
    err = AddImageDescriptionExtension(image_description, avcC_handle, 'avcC');
	if (err) return err;
        
    DisposeHandle(avcC_handle);
	free(avcC_buffer);
	free(sps_indexes);
	free(pps_indexes);
	
	/* Need to close the encoder so it can be re-opened with the correct pass properties. */
    x264_encoder_close(temp_encoder);
	
	if (glob->encoder) {
		x264_encoder_close(glob->encoder);
    	glob->encoder = NULL;
	}
    
	/* reset stat writing to what it used to be. */
	glob->params.rc.b_stat_write = b_stat_write_old;
	
	glob->total_duration = 0;
	glob->time_scale = 0;
	glob->total_frame_count = 0;
    
	return err;
}

ComponentResult x264_compressor_EncodeFrame(x264_compressor_globals_t *glob, ICMCompressorSourceFrameRef source_frame, UInt32 flags)
{
    ComponentResult err = noErr;    
	ICMCompressionFrameOptionsRef frame_options;
    CVPixelBufferRef pixel_buffer;
    TimeValue64 display_duration;
 	OSType pixel_format_type;
	Boolean force_key_frame;
    x264_nal_t *nals;
    x264_picture_t picture_out;
    int frame_size = 0;
    int nal_count;

	if (glob->encoder == NULL) {
		glob->encoder = x264_encoder_open(&glob->params);
	}
	    
	if (glob->encoder == NULL) {
		fprintf(stderr, "%s: [ERROR] could not open encoder, bailing\n", __FUNCTION__);
		return paramErr;
	}

    if (!source_frame)  return err;
	
    pixel_buffer = ICMCompressorSourceFrameGetPixelBuffer(source_frame);
	if (!pixel_buffer) return err;
	
	pixel_format_type = CVPixelBufferGetPixelFormatType(pixel_buffer);
	if (pixel_format_type != k422YpCbCr8CodecType) return '!yuv';
	
    ICMCompressorSourceFrameRetain(source_frame);

    CFArrayAppendValue(glob->source_frame_queue, source_frame);

	err = ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(source_frame, NULL, &display_duration, &glob->time_scale, NULL);
	if (err) return err;
	
	if ((display_duration != glob->params.i_fps_den) || (glob->time_scale != glob->params.i_fps_num)) {
		glob->params.i_fps_num = glob->time_scale;
		glob->params.i_fps_den = display_duration;
		x264_encoder_reconfig(glob->encoder, &glob->params);
	}
	
    glob->total_duration += display_duration;
    glob->total_frame_count += 1;

	// okay, now we need to convert packed YUV 4:2:2 -> planar YUV 4:2:0
	
	CVPixelBufferLockBaseAddress(pixel_buffer, 0);

	copy_2vuy_to_planar_YUV420(glob->params.i_width, glob->params.i_height, 
							   CVPixelBufferGetBaseAddress(pixel_buffer), CVPixelBufferGetBytesPerRow(pixel_buffer),
							   glob->source_picture.img.plane[0], glob->source_picture.img.i_stride[0], 
							   glob->source_picture.img.plane[1], glob->source_picture.img.i_stride[1], 
							   glob->source_picture.img.plane[2], glob->source_picture.img.i_stride[2]);
	
    CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);

	// encode the frame

	frame_options = ICMCompressorSourceFrameGetFrameOptions(source_frame);
	force_key_frame = (!ICMCompressionSessionOptionsGetAllowTemporalCompression(glob->session_options)) || (ICMCompressionFrameOptionsGetForceKeyFrame(frame_options));

	if (force_key_frame)
		glob->source_picture.i_type = X264_TYPE_I;
	else
		glob->source_picture.i_type = X264_TYPE_AUTO;

	err = ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(source_frame, &glob->source_picture.i_pts, NULL, NULL, NULL);
	if (err) return err;

    frame_size = x264_encoder_encode(glob->encoder, &nals, &nal_count, &glob->source_picture, &picture_out);
	//if (err < 0)	return err;
	
	if ((glob->emit_frames) && (nal_count)) {
		err = emit_frame_from_nals(glob, nals, nal_count, &picture_out, frame_size);
		if (err) return err;
	} else if (nal_count) {
		release_source_frame_for_encoded(glob, &picture_out);
	}
    
	return err;
}

ComponentResult x264_compressor_CompleteFrame(x264_compressor_globals_t *glob, ICMCompressorSourceFrameRef source_frame, UInt32 flags)
{
    ComponentResult err = noErr;

    rebug("");

	/* Need to empty out delayed b-frames */
    while (CFArrayGetCount(glob->source_frame_queue)) {
        
		ICMCompressionFrameOptionsRef frame_options;
        x264_nal_t      *nals;
        x264_picture_t  picture;
        int             nal_count;
        //int 			x264err;
        int             frame_size;
		Boolean 		force_key_frame;

        frame_options = ICMCompressorSourceFrameGetFrameOptions(source_frame);
		force_key_frame = (!ICMCompressionSessionOptionsGetAllowTemporalCompression(glob->session_options)) || (ICMCompressionFrameOptionsGetForceKeyFrame(frame_options));

		if (force_key_frame)
			glob->source_picture.i_type = X264_TYPE_I;
		else
			glob->source_picture.i_type = X264_TYPE_AUTO;

        rebug("");

        frame_size = x264_encoder_encode(glob->encoder, &nals, &nal_count, NULL, &picture);
        if (frame_size <= 0) {
            //err = x264err;
			//return err;
            return err;
        }

        rebug("");
        
		if ((glob->emit_frames) && (nal_count)) {
			err = emit_frame_from_nals(glob, nals, nal_count, &picture, frame_size);
			if (err) return err;
		}
    }
    
    rebug("");

	return err;
}

ComponentResult x264_compressor_BeginPass (x264_compressor_globals_t *glob, ICMCompressionPassModeFlags pass_mode_flags, UInt32 flags, ICMMultiPassStorageRef storage)
{
	glob->params.rc.b_stat_read = (pass_mode_flags & kICMCompressionPassMode_ReadFromMultiPassStorage) ? TRUE : FALSE;
	glob->params.rc.b_stat_write = (pass_mode_flags & kICMCompressionPassMode_WriteToMultiPassStorage) ? TRUE : FALSE;
	glob->emit_frames = (pass_mode_flags & kICMCompressionPassMode_OutputEncodedFrames) ? TRUE : FALSE;
	
	return noErr;
}

ComponentResult x264_compressor_EndPass(x264_compressor_globals_t *glob)
{	
	if (glob->encoder) {
		x264_encoder_close(glob->encoder);
    	glob->encoder = NULL;
	}
	return noErr;
}

ComponentResult x264_compressor_ProcessBetweenPasses(x264_compressor_globals_t *glob, ICMMultiPassStorageRef storage, Boolean *done, ICMCompressionPassModeFlags *flags)
{	
	if (glob->params.rc.b_stat_write)  {
		*flags = kICMCompressionPassMode_ReadFromMultiPassStorage | kICMCompressionPassMode_OutputEncodedFrames;
	} else {
		*flags = kICMCompressionPassMode_WriteToMultiPassStorage;
	}
	
	*done = TRUE;
	return noErr;
}
