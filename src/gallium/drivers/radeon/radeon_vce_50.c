/**************************************************************************
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/*
 * Authors:
 *      Christian König <christian.koenig@amd.com>
 *
 */

#include <stdio.h>

#include "pipe/p_video_codec.h"

#include "util/u_video.h"
#include "util/u_memory.h"

#include "vl/vl_video_buffer.h"

#include "r600_pipe_common.h"
#include "radeon_video.h"
#include "radeon_vce.h"

static void task_info(struct rvce_encoder *enc, uint32_t taskOperation)
{
	RVCE_BEGIN(0x00000002); // task info
	RVCE_CS(0xffffffff); // offsetOfNextTaskInfo
	RVCE_CS(taskOperation); // taskOperation
	RVCE_CS(0x00000000); // referencePictureDependency
	RVCE_CS(0x00000000); // collocateFlagDependency
	RVCE_CS(0x00000000); // feedbackIndex
	RVCE_CS(0x00000000); // videoBitstreamRingIndex
	RVCE_END();
}

static void rate_control(struct rvce_encoder *enc)
{
	RVCE_BEGIN(0x04000005); // rate control
	RVCE_CS(enc->pic.rate_ctrl.rate_ctrl_method); // encRateControlMethod
	RVCE_CS(enc->pic.rate_ctrl.target_bitrate); // encRateControlTargetBitRate
	RVCE_CS(enc->pic.rate_ctrl.peak_bitrate); // encRateControlPeakBitRate
	RVCE_CS(enc->pic.rate_ctrl.frame_rate_num); // encRateControlFrameRateNum
	RVCE_CS(0x00000000); // encGOPSize
	RVCE_CS(enc->pic.quant_i_frames); // encQP_I
	RVCE_CS(enc->pic.quant_p_frames); // encQP_P
	RVCE_CS(enc->pic.quant_b_frames); // encQP_B
	RVCE_CS(enc->pic.rate_ctrl.vbv_buffer_size); // encVBVBufferSize
	RVCE_CS(enc->pic.rate_ctrl.frame_rate_den); // encRateControlFrameRateDen
	RVCE_CS(0x00000000); // encVBVBufferLevel
	RVCE_CS(0x00000000); // encMaxAUSize
	RVCE_CS(0x00000000); // encQPInitialMode
	RVCE_CS(enc->pic.rate_ctrl.target_bits_picture); // encTargetBitsPerPicture
	RVCE_CS(enc->pic.rate_ctrl.peak_bits_picture_integer); // encPeakBitsPerPictureInteger
	RVCE_CS(enc->pic.rate_ctrl.peak_bits_picture_fraction); // encPeakBitsPerPictureFractional
	RVCE_CS(0x00000000); // encMinQP
	RVCE_CS(0x00000033); // encMaxQP
	RVCE_CS(0x00000000); // encSkipFrameEnable
	RVCE_CS(0x00000000); // encFillerDataEnable
	RVCE_CS(0x00000000); // encEnforceHRD
	RVCE_CS(0x00000000); // encBPicsDeltaQP
	RVCE_CS(0x00000000); // encReferenceBPicsDeltaQP
	RVCE_CS(0x00000000); // encRateControlReInitDisable
	RVCE_CS(0x00000000); // encLCVBRInitQPFlag
	RVCE_CS(0x00000000); // encLCVBRSATDBasedNonlinearBitBudgetFlag
	RVCE_END();
}

static void encode(struct rvce_encoder *enc)
{
	int i;
	unsigned luma_offset, chroma_offset;

	task_info(enc, 0x00000003);

	RVCE_BEGIN(0x05000001); // context buffer
	RVCE_READWRITE(enc->cpb.res->cs_buf, enc->cpb.res->domains); // encodeContextAddressHi
	RVCE_CS(0x00000000); // encodeContextAddressLo
	RVCE_END();

	RVCE_BEGIN(0x05000004); // video bitstream buffer
	RVCE_WRITE(enc->bs_handle, RADEON_DOMAIN_GTT); // videoBitstreamRingAddressHi
	RVCE_CS(0x00000000); // videoBitstreamRingAddressLo
	RVCE_CS(enc->bs_size); // videoBitstreamRingSize
	RVCE_END();

	RVCE_BEGIN(0x03000001); // encode
	RVCE_CS(enc->pic.frame_num ? 0x0 : 0x11); // insertHeaders
	RVCE_CS(0x00000000); // pictureStructure
	RVCE_CS(enc->bs_size); // allowedMaxBitstreamSize
	RVCE_CS(0x00000000); // forceRefreshMap
	RVCE_CS(0x00000000); // insertAUD
	RVCE_CS(0x00000000); // endOfSequence
	RVCE_CS(0x00000000); // endOfStream
	RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM); // inputPictureLumaAddressHi
	RVCE_CS(enc->luma->level[0].offset); // inputPictureLumaAddressLo
	RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM); // inputPictureChromaAddressHi
	RVCE_CS(enc->chroma->level[0].offset); // inputPictureChromaAddressLo
	RVCE_CS(align(enc->luma->npix_y, 16)); // encInputFrameYPitch
	RVCE_CS(enc->luma->level[0].pitch_bytes); // encInputPicLumaPitch
	RVCE_CS(enc->chroma->level[0].pitch_bytes); // encInputPicChromaPitch
	RVCE_CS(0x00010000); // encInputPic(Addr|Array)Mode,encDisable(TwoPipeMode|MBOffloading)
	RVCE_CS(0x00000000); // encInputPicTileConfig
	RVCE_CS(enc->pic.picture_type); // encPicType
	RVCE_CS(enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_IDR); // encIdrFlag
	RVCE_CS(0x00000000); // encIdrPicId
	RVCE_CS(0x00000000); // encMGSKeyPic
	RVCE_CS(!enc->pic.not_referenced); // encReferenceFlag
	RVCE_CS(0x00000000); // encTemporalLayerIndex
	RVCE_CS(0x00000000); // num_ref_idx_active_override_flag
	RVCE_CS(0x00000000); // num_ref_idx_l0_active_minus1
	RVCE_CS(0x00000000); // num_ref_idx_l1_active_minus1

	i = enc->pic.frame_num - enc->pic.ref_idx_l0;
	if (i > 1 && enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_P) {
		RVCE_CS(0x00000001); // encRefListModificationOp
		RVCE_CS(i - 1);      // encRefListModificationNum
	} else {
		RVCE_CS(0x00000000); // encRefListModificationOp
		RVCE_CS(0x00000000); // encRefListModificationNum
	}

	for (i = 0; i < 3; ++i) {
		RVCE_CS(0x00000000); // encRefListModificationOp
		RVCE_CS(0x00000000); // encRefListModificationNum
	}
	for (i = 0; i < 4; ++i) {
		RVCE_CS(0x00000000); // encDecodedPictureMarkingOp
		RVCE_CS(0x00000000); // encDecodedPictureMarkingNum
		RVCE_CS(0x00000000); // encDecodedPictureMarkingIdx
		RVCE_CS(0x00000000); // encDecodedRefBasePictureMarkingOp
		RVCE_CS(0x00000000); // encDecodedRefBasePictureMarkingNum
	}

	// encReferencePictureL0[0]
	RVCE_CS(0x00000000); // pictureStructure
	if(enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_P ||
	   enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_B) {
		struct rvce_cpb_slot *l0 = l0_slot(enc);
		rvce_frame_offset(enc, l0, &luma_offset, &chroma_offset);
		RVCE_CS(l0->picture_type); // encPicType
		RVCE_CS(l0->frame_num); // frameNumber
		RVCE_CS(l0->pic_order_cnt); // pictureOrderCount
		RVCE_CS(luma_offset); // lumaOffset
		RVCE_CS(chroma_offset); // chromaOffset
	} else {
		RVCE_CS(0x00000000); // encPicType
		RVCE_CS(0x00000000); // frameNumber
		RVCE_CS(0x00000000); // pictureOrderCount
		RVCE_CS(0xffffffff); // lumaOffset
		RVCE_CS(0xffffffff); // chromaOffset
	}

	// encReferencePictureL0[1]
	RVCE_CS(0x00000000); // pictureStructure
	RVCE_CS(0x00000000); // encPicType
	RVCE_CS(0x00000000); // frameNumber
	RVCE_CS(0x00000000); // pictureOrderCount
	RVCE_CS(0xffffffff); // lumaOffset
	RVCE_CS(0xffffffff); // chromaOffset

	// encReferencePictureL1[0]
	RVCE_CS(0x00000000); // pictureStructure
	if(enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_B) {
		struct rvce_cpb_slot *l1 = l1_slot(enc);
		rvce_frame_offset(enc, l1, &luma_offset, &chroma_offset);
		RVCE_CS(l1->picture_type); // encPicType
		RVCE_CS(l1->frame_num); // frameNumber
		RVCE_CS(l1->pic_order_cnt); // pictureOrderCount
		RVCE_CS(luma_offset); // lumaOffset
		RVCE_CS(chroma_offset); // chromaOffset
	} else {
		RVCE_CS(0x00000000); // encPicType
		RVCE_CS(0x00000000); // frameNumber
		RVCE_CS(0x00000000); // pictureOrderCount
		RVCE_CS(0xffffffff); // lumaOffset
		RVCE_CS(0xffffffff); // chromaOffset
	}

	rvce_frame_offset(enc, current_slot(enc), &luma_offset, &chroma_offset);
	RVCE_CS(luma_offset); // encReconstructedLumaOffset
	RVCE_CS(chroma_offset); // encReconstructedChromaOffset
	RVCE_CS(0x00000000); // encColocBufferOffset
	RVCE_CS(0x00000000); // encReconstructedRefBasePictureLumaOffset
	RVCE_CS(0x00000000); // encReconstructedRefBasePictureChromaOffset
	RVCE_CS(0x00000000); // encReferenceRefBasePictureLumaOffset
	RVCE_CS(0x00000000); // encReferenceRefBasePictureChromaOffset
	RVCE_CS(0x00000000); // pictureCount
	RVCE_CS(enc->pic.frame_num); // frameNumber
	RVCE_CS(enc->pic.pic_order_cnt); // pictureOrderCount
	RVCE_CS(0x00000000); // numIPicRemainInRCGOP
	RVCE_CS(0x00000000); // numPPicRemainInRCGOP
	RVCE_CS(0x00000000); // numBPicRemainInRCGOP
	RVCE_CS(0x00000000); // numIRPicRemainInRCGOP
	RVCE_CS(0x00000000); // enableIntraRefresh
	RVCE_END();
}

void radeon_vce_50_init(struct rvce_encoder *enc)
{
	radeon_vce_40_2_2_init(enc);

	/* only the two below are different */
	enc->rate_control = rate_control;
	enc->encode = encode;
}
