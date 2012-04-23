/*
 *  x264CompressorDispatch.h
 *  x264qtcodec
 *
 *  Created by Henry Mason on 12/28/05.
 *  Copyright 2005 Henry Mason. All rights reserved.
 *
 */

	ComponentSelectorOffset (8)

	ComponentRangeCount (1)
	ComponentRangeShift (8)
	ComponentRangeMask	(FF)

	ComponentRangeBegin (0)
		ComponentError	 (GetMPWorkFunction)
		ComponentError	 (Unregister)
		StdComponentCall (Target)
		ComponentError   (Register)
		StdComponentCall (Version)
		StdComponentCall (CanDo)
		StdComponentCall (Close)
		StdComponentCall (Open)
	ComponentRangeEnd (0)
		
	ComponentRangeBegin (1)
		ComponentCall (GetCodecInfo)							// 0
		ComponentDelegate (GetCompressionTime)
		ComponentCall (GetMaxCompressionSize)
		ComponentDelegate (PreCompress)
		ComponentDelegate (BandCompress)
		ComponentDelegate (PreDecompress)							// 5
		ComponentDelegate (BandDecompress)
		ComponentDelegate (Busy)
		ComponentDelegate (GetCompressedImageSize)
		ComponentDelegate (GetSimilarity)
		ComponentDelegate (TrimImage)								// 10
		ComponentCall (RequestSettings)
		ComponentCall (GetSettings)
		ComponentCall (SetSettings)
		ComponentDelegate (Flush)
		ComponentDelegate (SetTimeCode)							// 15
		ComponentDelegate (IsImageDescriptionEquivalent)
		ComponentDelegate (NewMemory)
		ComponentDelegate (DisposeMemory)
		ComponentDelegate (HitTestData)
		ComponentDelegate (NewImageBufferMemory)					// 20
		ComponentDelegate (ExtractAndCombineFields)
		ComponentDelegate (GetMaxCompressionSizeWithSources)
		ComponentDelegate (SetTimeBase)
		ComponentDelegate (SourceChanged)
		ComponentDelegate (FlushLastFrame)							// 25
		ComponentDelegate (GetSettingsAsText)
		ComponentDelegate (GetParameterListHandle)
		ComponentDelegate (GetParameterList)
		ComponentDelegate (CreateStandardParameterDialog)
		ComponentDelegate (IsStandardParameterDialogEvent)			// 30
		ComponentDelegate (DismissStandardParameterDialog)
		ComponentDelegate (StandardParameterDialogDoAction)
		ComponentDelegate (NewImageGWorld)
		ComponentDelegate (DisposeImageGWorld)
		ComponentDelegate (HitTestDataWithFlags)					// 35
		ComponentDelegate (ValidateParameters)
		ComponentDelegate (GetBaseMPWorkFunction)
		ComponentDelegate (LockBits)
		ComponentDelegate (UnlockBits)
		ComponentDelegate (RequestGammaLevel)						// 40
		ComponentDelegate (GetSourceDataGammaLevel)
		ComponentDelegate (42)
		ComponentDelegate (GetDecompressLatency)
		ComponentDelegate (MergeFloatingImageOntoWindow)
		ComponentDelegate (RemoveFloatingImage)					// 45
		ComponentDelegate (GetDITLForSize)
		ComponentDelegate (DITLInstall)
		ComponentDelegate (DITLEvent)
		ComponentDelegate (DITLItem)
		ComponentDelegate (DITLRemove)								// 50
		ComponentDelegate (DITLValidateInput)
		ComponentDelegate (52)
		ComponentDelegate (53)
		ComponentDelegate (GetPreferredChunkSizeAndAlignment)
		ComponentCall (PrepareToCompressFrames)					// 55
		ComponentCall (EncodeFrame)
		ComponentCall (CompleteFrame)
 		ComponentCall (BeginPass)
 		ComponentCall (EndPass)
		ComponentCall (ProcessBetweenPasses)					// 60
	ComponentRangeEnd (1)
