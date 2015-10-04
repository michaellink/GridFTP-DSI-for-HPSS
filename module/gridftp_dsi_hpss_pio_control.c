/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright � 2012 NCSA.  All rights reserved.
 *
 * Developed by:
 *
 * Storage Enabling Technologies (SET)
 *
 * Nation Center for Supercomputing Applications (NCSA)
 *
 * http://www.ncsa.illinois.edu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the .Software.),
 * to deal with the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 *    + Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimers.
 *
 *    + Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimers in the
 *      documentation and/or other materials provided with the distribution.
 *
 *    + Neither the names of SET, NCSA
 *      nor the names of its contributors may be used to endorse or promote
 *      products derived from this Software without specific prior written
 *      permission.
 *
 * THE SOFTWARE IS PROVIDED .AS IS., WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS WITH THE SOFTWARE.
 */

/*
 * Globus includes.
 */
#include <globus_gridftp_server.h>

/*
 * HPSS includes.
 */
#include <hpss_api.h>

/*
 * Local includes.
 */
#include "gridftp_dsi_hpss_pio_control.h"
#include "gridftp_dsi_hpss_range_list.h"
#include "gridftp_dsi_hpss_misc.h"
#include "gridftp_dsi_hpss_msg.h"


struct pio_control {
	int                    FileFD;
	int                    DataNodeCount;
	unsigned32             FileStripeWidth;
	hpss_pio_operation_t   OperationType;
	msg_handle_t         * MsgHandle;

	struct {
		hpss_pio_grp_t   StripeGroup;
		range_list_t   * RangeList;

		pio_control_transfer_range_callback_t Callback;
		void                                * CallbackArg;
	} PioExecute;
};

static void *
pio_control_execute_thread(void * Arg);

static globus_result_t
pio_control_can_change_cos(char * Pathname, int * can_change_cos)
{
	int retval;
	hpss_fileattr_t fileattr;
	ns_FilesetAttrBits_t fileset_attr_bits;
	ns_FilesetAttrs_t    fileset_attr;

	GlobusGFSName(pio_control_can_change_cos);

	memset(&fileattr, 0, sizeof(hpss_fileattr_t));
	retval = hpss_FileGetAttributes(Pathname, &fileattr);
	if (retval)
		return GlobusGFSErrorSystemError("hpss_FileGetAttributes", -retval);

	fileset_attr_bits = orbit64m(0, NS_FS_ATTRINDEX_COS);
	memset(&fileset_attr, 0, sizeof(ns_FilesetAttrs_t));
	retval = hpss_FilesetGetAttributes(NULL,
	                                   &fileattr.Attrs.FilesetId,
	                                   NULL,
	                                   NULL,
	                                   fileset_attr_bits,
	                                   &fileset_attr);
	if (retval)
		return GlobusGFSErrorSystemError("hpss_FilesetGetAttributes", -retval);


	*can_change_cos = !fileset_attr.ClassOfService;

	return GLOBUS_SUCCESS;
}

static globus_result_t
pio_control_open_file_for_writing(pio_control_t * PioControl,
                                  char          * Pathname,
                                  globus_off_t    AllocSize,
                                  globus_bool_t   Truncate,
                                  int             FamilyID,
                                  int             CosID)
{
	int                     oflags      = 0;
	int                     retval      = 0;
	int                     can_change_cos = 0;
	globus_off_t            file_length = 0;
	globus_result_t         result      = GLOBUS_SUCCESS;
	hpss_cos_hints_t        hints_in;
	hpss_cos_hints_t        hints_out;
	hpss_cos_priorities_t   priorities;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Initialize the hints in. */
	memset(&hints_in, 0, sizeof(hpss_cos_hints_t));

	/* Initialize the hints out. */
	memset(&hints_out, 0, sizeof(hpss_cos_hints_t));

	/* Initialize the priorities. */
	memset(&priorities, 0, sizeof(hpss_cos_priorities_t));

	/*
 	* If this is a new file we need to determine the class of service
 	* by either:
 	*  1) set explicitly within the session handle or
 	*  2) determined by the size of the incoming file
 	*/
	if (Truncate == GLOBUS_TRUE)
	{
		/* Get our preferened cos id. */
		if (CosID != -1)
		{
			/*
			 * COSIdPriority must be REQUIRED_PRIORITY or it is ignored.
			 */
			hints_in.COSId = CosID;
			priorities.COSIdPriority = REQUIRED_PRIORITY;
		} 

		if (AllocSize != 0)
		{
			/*
			 * Use the ALLO size.
			 */
			file_length = AllocSize;
			CONVERT_LONGLONG_TO_U64(file_length, hints_in.MinFileSize);
			CONVERT_LONGLONG_TO_U64(file_length, hints_in.MaxFileSize);
			priorities.MinFileSizePriority = REQUIRED_PRIORITY;
			/*
			 * If MaxFileSizePriority is required, you can not place
			 *  a file into a COS where it's max size is < the size
			 *  of this file regardless of whether or not enforce max
			 *  is enabled on the COS (it doesn't even try).
			 */
			priorities.MaxFileSizePriority = HIGHLY_DESIRED_PRIORITY;
		}

		/* Get our preferred family id. */
		if (FamilyID != -1)
		{
			hints_in.FamilyId = FamilyID;
			priorities.FamilyIdPriority = REQUIRED_PRIORITY;
		}
	}

	/* Always use O_CREAT in support of S3 transfers. */
	oflags = O_WRONLY|O_CREAT;
	if (Truncate == GLOBUS_TRUE)
		oflags |= O_TRUNC;

	/* Open the HPSS file. */
	PioControl->FileFD = hpss_Open(Pathname,
                    	           oflags,
                    	           S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH,
                    	           &hints_in,
                    	           &priorities,
                    	           &hints_out);
	if (PioControl->FileFD < 0)
	{
		result = GlobusGFSErrorSystemError("hpss_Open", -(PioControl->FileFD));
		goto cleanup;
	}

	result = pio_control_can_change_cos(Pathname, &can_change_cos);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Handle the case of the file that already existed. */
	if (Truncate == GLOBUS_TRUE && can_change_cos)
	{
		hpss_cos_md_t cos_md;

		retval = hpss_SetCOSByHints(PioControl->FileFD,
		                            0,
		                            &hints_in,
		                            &priorities,
		                            &cos_md);

		if (retval)
		{
			result = GlobusGFSErrorSystemError("hpss_SetCOSByHints", -(retval));
			goto cleanup;
		}
	}

	/* Copy out the file stripe width. */
	PioControl->FileStripeWidth = hints_out.StripeWidth;

cleanup:
	if (result != GLOBUS_SUCCESS)
	{
		GlobusGFSHpssDebugExitWithError();
		return result;
	}

	GlobusGFSHpssDebugExit();
	return GLOBUS_SUCCESS;
}

static globus_result_t
pio_control_open_file_for_reading(pio_control_t * PioControl,
                                  char          * Pathname)
{
	globus_result_t       result = GLOBUS_SUCCESS;
	hpss_cos_hints_t      hints_in;
	hpss_cos_hints_t      hints_out;
	hpss_cos_priorities_t priorities;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Initialize the hints in. */
	memset(&hints_in, 0, sizeof(hpss_cos_hints_t));

	/* Initialize the hints out. */
	memset(&hints_out, 0, sizeof(hpss_cos_hints_t));

	/* Initialize the priorities. */
	memset(&priorities, 0, sizeof(hpss_cos_priorities_t));

	/* Open the HPSS file. */
	PioControl->FileFD = hpss_Open(Pathname,
                    	           O_RDONLY,
                    	           S_IRUSR|S_IWUSR,
                    	           &hints_in,
                    	           &priorities,
                    	           &hints_out);
	if (PioControl->FileFD < 0)
	{
		result = GlobusGFSErrorSystemError("hpss_Open", -(PioControl->FileFD));
		goto cleanup;
	}

	/* Copy out the file stripe width. */
	PioControl->FileStripeWidth = hints_out.StripeWidth;

cleanup:
	if (result != GLOBUS_SUCCESS)
	{
		GlobusGFSHpssDebugExitWithError();
		return result;
	}

	GlobusGFSHpssDebugExit();
	return GLOBUS_SUCCESS;
}

static globus_result_t
pio_control_common_init(hpss_pio_operation_t    OperationType,
                        msg_handle_t         *  MsgHandle,
                        pio_control_t        ** PioControl)
{
	globus_result_t result = GLOBUS_SUCCESS;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Allocate the handle. */
	*PioControl = (pio_control_t *) globus_calloc(1, sizeof(pio_control_t));
	if (*PioControl == NULL)
	{
		result = GlobusGFSErrorMemory("pio_control_t");
		goto cleanup;
	}

	/* Initialize entries. */
	(*PioControl)->OperationType = OperationType;
	(*PioControl)->MsgHandle     = MsgHandle;
	(*PioControl)->FileFD        = -1;
	(*PioControl)->DataNodeCount = 1;

cleanup:
	if (result != GLOBUS_SUCCESS)
	{
		GlobusGFSHpssDebugExitWithError();
		return result;
	}

	GlobusGFSHpssDebugExit();
	return GLOBUS_SUCCESS;
}

globus_result_t
pio_control_stor_init(msg_handle_t               *  MsgHandle,
                      session_handle_t           *  Session,
                      globus_gfs_transfer_info_t *  TransferInfo,
                      pio_control_t              ** PioControl)
{
	globus_result_t result = GLOBUS_SUCCESS;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Perform the common init. */
	result = pio_control_common_init(HPSS_PIO_WRITE, MsgHandle, PioControl);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Open the file. */
	result = pio_control_open_file_for_writing(*PioControl,
	                                           TransferInfo->pathname,
	                                           TransferInfo->alloc_size,
	                                           TransferInfo->truncate,
	                                           session_pref_get_family_id(Session),
	                                           session_pref_get_cos_id(Session));
cleanup:
	GlobusGFSHpssDebugExit();
	return result;
}

globus_result_t
pio_control_retr_init(msg_handle_t               *  MsgHandle,
                      globus_gfs_transfer_info_t *  TransferInfo,
                      pio_control_t              ** PioControl)
{
	globus_result_t result = GLOBUS_SUCCESS;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Perform the common init. */
	result = pio_control_common_init(HPSS_PIO_READ, MsgHandle, PioControl);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Open the file. */
	result = pio_control_open_file_for_reading(*PioControl, TransferInfo->pathname);

cleanup:
	GlobusGFSHpssDebugExit();
	return result;
}

globus_result_t
pio_control_cksm_init(msg_handle_t              *  MsgHandle,
                      globus_gfs_command_info_t *  CommandInfo,
                      pio_control_t             ** PioControl)
{
	globus_result_t result = GLOBUS_SUCCESS;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Perform the common init. */
	result = pio_control_common_init(HPSS_PIO_READ, MsgHandle, PioControl);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Open the file. */
	result = pio_control_open_file_for_reading(*PioControl, CommandInfo->pathname);

cleanup:
	GlobusGFSHpssDebugExit();
	return result;
}

void
pio_control_destroy(pio_control_t * PioControl)
{
	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	if (PioControl != NULL)
	{
		/* Close the HPSS file. */
		if (PioControl->FileFD != -1)
			hpss_Close(PioControl->FileFD);

		globus_free(PioControl);
	}

	GlobusGFSHpssDebugExit();
}

void
pio_control_transfer_ranges(pio_control_t                         * PioControl,
                            unsigned32                              ClntStripeWidth,
                            globus_off_t                            StripeBlockSize,
                            range_list_t                          * RangeList,
                            pio_control_transfer_range_callback_t   Callback,
                            void                                  * CallbackArg)
{
	int                 retval        = 0;
	int                 node_id       = 0;
	char                int_buf[16];
	void              * group_buffer  = NULL;
	unsigned int        buffer_length = 0;
	globus_bool_t       pio_started   = GLOBUS_FALSE;
	globus_result_t     result        = GLOBUS_SUCCESS;
	hpss_pio_params_t   pio_params;
	globus_thread_t     thread_id;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	PioControl->PioExecute.RangeList   = RangeList;
	PioControl->PioExecute.Callback    = Callback;
	PioControl->PioExecute.CallbackArg = CallbackArg;

	/*
	 * Don't use HPSS_PIO_HANDLE_GAP, it's bugged in HPSS 7.4.
	 */
	pio_params.Operation       = PioControl->OperationType;
	pio_params.ClntStripeWidth = ClntStripeWidth;
	pio_params.BlockSize       = StripeBlockSize;
	pio_params.FileStripeWidth = PioControl->FileStripeWidth;
	pio_params.IOTimeOutSecs   = 0;
	pio_params.Transport       = HPSS_PIO_MVR_SELECT;
	pio_params.Options         = 0;

	/* Now call the start routine. */
	retval = hpss_PIOStart(&pio_params, &PioControl->PioExecute.StripeGroup);
	if (retval != 0)
	{
		result = GlobusGFSErrorSystemError("hpss_PIOStart", -retval);
		goto cleanup;
	}

	/* Indicate that pio has been started. */
	pio_started = GLOBUS_TRUE;

	/* Export the stripe group for the caller. */
	retval = hpss_PIOExportGrp(PioControl->PioExecute.StripeGroup,
	                           &group_buffer,
	                           &buffer_length);
	if (retval != 0)
	{
		result = GlobusGFSErrorSystemError("hpss_PIOExportGrp", -retval);
		goto cleanup;
	}

	for (node_id = 0; node_id < PioControl->DataNodeCount; node_id++)
	{
		/* Encode this node's stripe index. */
		snprintf(int_buf, sizeof(int_buf), "%u", node_id);

		/* Send the stripe index message to the pio data node. */
		result = msg_send(PioControl->MsgHandle,
		                  MSG_COMP_ID_TRANSFER_DATA_PIO,
		                  MSG_COMP_ID_TRANSFER_CONTROL_PIO,
		                  PIO_CONTROL_MSG_TYPE_STRIPE_INDEX,
		                  sizeof(int_buf),
		                  int_buf);

		if (result != GLOBUS_SUCCESS)
			goto cleanup;

		/* Send the stripe group message to the pio data node. */
		result = msg_send(PioControl->MsgHandle,
		                  MSG_COMP_ID_TRANSFER_DATA_PIO,
		                  MSG_COMP_ID_TRANSFER_CONTROL_PIO,
		                  PIO_CONTROL_MSG_TYPE_STRIPE_GROUP,
		                  buffer_length,
		                  group_buffer);

		if (result != GLOBUS_SUCCESS)
			goto cleanup;
	}


	/* Launch the pio execute thread. */
	retval = globus_thread_create(&thread_id,
                                  NULL,
                                  pio_control_execute_thread,
                                  PioControl);

	if (retval != 0)
		result = GlobusGFSErrorSystemError("globus_thread_create", retval);

cleanup:
	/* Release the group buffer. */
	if (group_buffer != NULL)
		free(group_buffer);

	if (result != GLOBUS_SUCCESS)
	{
		/* Inform the caller. */
		Callback(CallbackArg, result);

		/* Shutdown PIO. */
		if (pio_started == GLOBUS_TRUE)
			hpss_PIOEnd(PioControl->PioExecute.StripeGroup);

		GlobusGFSHpssDebugExitWithError();
		return;
	}

	GlobusGFSHpssDebugExit();
}

static void *
pio_control_execute_thread(void * Arg)
{

	int                          retval      = 0;
	globus_result_t              result      = GLOBUS_SUCCESS;
	pio_control_t              * pio_control = NULL;
	hpss_pio_gapinfo_t           gap_info;
	u_signed64                   bytes_moved;
	u_signed64                   u_offset;
	u_signed64                   u_length;
	globus_off_t                 range_offset = 0;
	globus_off_t                 range_length = 0;
	pio_control_restart_marker_t restart_marker;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Make sure we got our handle. */
	globus_assert(Arg != NULL);
	/* Cast the handle. */
	pio_control = (pio_control_t *)Arg;

	while (range_list_empty(pio_control->PioExecute.RangeList) == GLOBUS_FALSE)
	{
		range_list_pop(pio_control->PioExecute.RangeList, &range_offset, &range_length);

		/* Convert to HPSS 64. */
		CONVERT_LONGLONG_TO_U64(range_offset, u_offset);
		CONVERT_LONGLONG_TO_U64(range_length, u_length);

		/* Initialize bytes_moved. */
		bytes_moved = cast64(0);

		do {
			u_offset = add64m(u_offset, bytes_moved);
			u_length = sub64m(u_length, bytes_moved);
			bytes_moved = cast64(0);
			memset(&gap_info, 0, sizeof(gap_info));

			/* Call pio execute. */
			retval = hpss_PIOExecute(pio_control->FileFD,
			                         u_offset,
			                         u_length,
			                         pio_control->PioExecute.StripeGroup,
			                         &gap_info,
			                         &bytes_moved);


			/*
			 * It appears that gap_info.offset is relative to u_offset. So you
			 * must add the two to get the real offset of the gap. Also, if 
			 * there is a gap, bytes_moved = gap_info.offset since bytes_moved
			 * is also relative to u_offset.
			 */

			/* Add in any hole we may have found. */
			if (neqz64m(gap_info.Length))
				bytes_moved = add64m(gap_info.Offset, gap_info.Length);

			if (pio_control->OperationType == HPSS_PIO_WRITE)
			{
				restart_marker.Offset = u_offset;
				restart_marker.Length = u_offset + bytes_moved;

				msg_send(pio_control->MsgHandle,
				         MSG_COMP_ID_ANY,
				         MSG_COMP_ID_TRANSFER_CONTROL_PIO,
				         PIO_CONTROL_MSG_TYPE_RESTART_MARKER,
				         sizeof(restart_marker),
				         &restart_marker);
			}

		} while (retval == 0 && !eq64(bytes_moved, u_length));

		if (retval != 0)
		{
			result = GlobusGFSErrorSystemError("hpss_PIOExecute", -retval);
			break;
		}
	}


	/* Stop PIO */
	retval = hpss_PIOEnd(pio_control->PioExecute.StripeGroup);
	if (retval != 0 && result == GLOBUS_SUCCESS)
		result = GlobusGFSErrorSystemError("hpss_PIOEnd", -retval);

	/* Report back to the caller that we are done. */
	pio_control->PioExecute.Callback(pio_control->PioExecute.CallbackArg, result);

	if (result != GLOBUS_SUCCESS)
	{
		GlobusGFSHpssDebugExitWithError();
		return NULL;
	}

	GlobusGFSHpssDebugExit();
	return NULL;
}
