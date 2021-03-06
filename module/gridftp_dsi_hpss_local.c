/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright � 2012-2014 NCSA.  All rights reserved.
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
 * System includes.
 */
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <utime.h>
#include <time.h>

/*
 * Globus includes.
 */
#include <globus_gridftp_server.h>

/*
 * HPSS includes.
 */
#include <hpss_api.h>
#include <u_signed64.h>

/*
 * Local includes.
 */
#include "gridftp_dsi_hpss_transfer_control.h"
#include "gridftp_dsi_hpss_transfer_data.h"
#include "gridftp_dsi_hpss_checksum.h"
#include "gridftp_dsi_hpss_pio_data.h"
#include "gridftp_dsi_hpss_session.h"
#include "gridftp_dsi_hpss_gridftp.h"
#include "gridftp_dsi_hpss_markers.h"
#include "gridftp_dsi_hpss_misc.h"
#include "gridftp_dsi_hpss_msg.h"
#include "../config.h"

#ifdef DMALLOC
/*
 * Dmalloc
 */
#include <dmalloc.h>
#endif /* DMALLOC */

/*
 * This is used to define the debug print statements.
 */
GlobusDebugDefine(GLOBUS_GRIDFTP_SERVER_HPSS);

typedef struct {
	msg_handle_t     * Msg;
	commands_t       * Commands;
	session_handle_t * Session;
} module_t;

typedef struct monitor {
	globus_gfs_operation_t   Operation;
	globus_mutex_t           Lock;
	globus_cond_t            Cond;
	globus_result_t          Result;
	globus_bool_t            Complete;
	globus_off_t             PartialOffset;
} monitor_t;

static void
local_monitor_init(globus_gfs_operation_t       Operation, 
                   globus_gfs_transfer_info_t * TransferInfo,
                   monitor_t                  * Monitor)
{
	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	Monitor->Operation = Operation;
	globus_mutex_init(&Monitor->Lock, NULL);
	globus_cond_init(&Monitor->Cond, NULL);
	Monitor->Result        = GLOBUS_SUCCESS;
	Monitor->Complete      = GLOBUS_FALSE;
	Monitor->PartialOffset = TransferInfo->partial_offset;

	GlobusGFSHpssDebugExit();
}

static void
local_monitor_destroy(monitor_t * Monitor)
{
	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	globus_mutex_destroy(&Monitor->Lock);
	globus_cond_destroy(&Monitor->Cond);

	GlobusGFSHpssDebugExit();
}

static void
local_msg_recv(void          * CallbackArg,
               msg_comp_id_t   DstMsgCompID,
               msg_comp_id_t   SrcMsgCompID,
               int             MsgType,
               int             MsgLen,
               void          * Msg)
{
	monitor_t                       * monitor        = NULL;
	transfer_control_complete_msg_t * complete_msg   = NULL;
	pio_data_bytes_written_t        * bytes_written  = NULL;
	pio_control_restart_marker_t    * restart_marker = NULL;

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	/* Cast to our monitor. */
	monitor = (monitor_t *)CallbackArg;

	switch (SrcMsgCompID)
	{
	case MSG_COMP_ID_TRANSFER_CONTROL:
		switch(MsgType)
		{
		case TRANSFER_CONTROL_MSG_TYPE_COMPLETE:
			/* Cast our message. */
			complete_msg = (transfer_control_complete_msg_t*) Msg;

			globus_mutex_lock(&monitor->Lock);
			{
				/* Indicate that we have finished. */
				monitor->Complete = GLOBUS_TRUE;
				/* Save any error. */
				monitor->Result   = complete_msg->Result;
				/* Wake any waiters. */
				globus_cond_signal(&monitor->Cond);
			}
			globus_mutex_unlock(&monitor->Lock);
			break;
		default:
			break;
		}
		break;

	case MSG_COMP_ID_TRANSFER_DATA_PIO:
		switch (MsgType)
		{
		case PIO_DATA_MSG_TYPE_BYTES_WRITTEN:
			/* Cast our message. */
			bytes_written = (pio_data_bytes_written_t *) Msg;

			/* Inform the server of the bytes written. */
			markers_update_perf_markers(monitor->Operation,
			                            bytes_written->Offset - monitor->PartialOffset,
			                            bytes_written->Length);
			break;
		default:
			break;
		}
		break;

	case MSG_COMP_ID_TRANSFER_CONTROL_PIO:
		switch (MsgType)
		{
		case PIO_CONTROL_MSG_TYPE_RESTART_MARKER:
			/* Cast our message. */
			restart_marker = (pio_control_restart_marker_t *) Msg;

			/* Send the marker to the server. */
			markers_update_restart_markers(monitor->Operation,
			                               restart_marker->Offset - monitor->PartialOffset,
			                               restart_marker->Length);
			break;
		}
		break;

	default:
		globus_assert(0);
	}

	GlobusGFSHpssDebugExit();
}

/*
 * RETR operation
 */
static void
local_retr(globus_gfs_operation_t       Operation,
           globus_gfs_transfer_info_t * TransferInfo,
           void                       * UserArg)
{
	monitor_t            monitor;
	globus_result_t      result            = GLOBUS_SUCCESS;
	module_t           * module            = UserArg;
	transfer_data_t    * transfer_data     = NULL;
	transfer_control_t * transfer_control  = NULL;
	msg_register_id_t    msg_register_id   = MSG_REGISTER_ID_NONE;

	GlobusGFSName(local_retr);
	GlobusGFSHpssDebugEnter();

	/* Make sure we got our UserArg. */
	globus_assert(UserArg);

	/* Initialize the monitor */
	local_monitor_init(Operation, TransferInfo, &monitor);

	/* Register to receive messages. */
	result = msg_register(module->Msg,
	                      MSG_COMP_ID_TRANSFER_DATA_PIO|MSG_COMP_ID_TRANSFER_CONTROL,
	                      MSG_COMP_ID_NONE,
	                      local_msg_recv,
	                      &monitor,
	                      &msg_register_id);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Initialize the control side. */
	result = transfer_control_retr_init(module->Msg,
	                                    Operation,
	                                    TransferInfo,
	                                    &transfer_control);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Initialize the data side. */
	result = transfer_data_retr_init(module->Msg,
	                                 Operation,
	                                 TransferInfo,
	                                 &transfer_data);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

    /* Inform the server that we are starting. */
	globus_gridftp_server_begin_transfer(Operation, 0, NULL);

	/* Start the data side. (sync) */
	result = transfer_data_run(transfer_data);

	/* Pass this message to the control side. */
	transfer_control_data_complete(transfer_control, result);

	/* Wait for the control side to complete. */
	globus_mutex_lock(&monitor.Lock);
	{
		if (monitor.Complete == GLOBUS_FALSE)
			globus_cond_wait(&monitor.Cond, &monitor.Lock);
	}
	globus_mutex_unlock(&monitor.Lock);

	/* Save off the result. */
	result = monitor.Result;

cleanup:
	/* Unregister to receive messages. */
	msg_unregister(module->Msg, msg_register_id);

	/* Destroy the data side. */
	transfer_data_destroy(transfer_data);

	/* Destroy the control side. */
	transfer_control_destroy(transfer_control);

	/* Destroy the monitor */
	local_monitor_destroy(&monitor);

	/* Let the server know we are finished. */
	globus_gridftp_server_finished_transfer(Operation, result);

	GlobusGFSHpssDebugExit();
}

/*
 * STOR operation
 * expected_checksum, expected_checksum_alg SCKS
 * 500 Command failed. : Actual checksum of e8f9f165cb40cc21695d90c24ff978d6 does not match expected checksum of 666.
 */
static void
local_stor(globus_gfs_operation_t       Operation,
           globus_gfs_transfer_info_t * TransferInfo,
           void                       * UserArg)
{
	monitor_t            monitor;
	globus_result_t      result            = GLOBUS_SUCCESS;
	module_t           * module            = UserArg;
	transfer_data_t    * transfer_data     = NULL;
	transfer_control_t * transfer_control  = NULL;
	msg_register_id_t    msg_register_id   = MSG_REGISTER_ID_NONE;

	GlobusGFSName(local_stro);
	GlobusGFSHpssDebugEnter();

	/* Make sure we got our UserArg. */
	globus_assert(UserArg);

	/* Initialize the monitor */
	local_monitor_init(Operation, TransferInfo, &monitor);

	if (!markers_restart_supported())
	{
		globus_off_t range_offset = 0;
		globus_off_t range_length = 0;

		globus_range_list_at(TransferInfo->range_list, 0, &range_offset, &range_length);
		if (globus_range_list_size(TransferInfo->range_list) != 1 || (range_offset != 0 || range_length != -1))
		{
			result = GlobusGFSErrorGeneric("Partial STOR is not supported");
			goto cleanup;
		}
	}


#ifdef UDA_CHECKSUM_SUPPORT
	/* Clear our any old checksum information. */
	result = checksum_clear_file_sum(TransferInfo->pathname);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;
#endif /* UDA_CHECKSUM_SUPPORT */

	/* Register to receive messages. */
	result = msg_register(module->Msg,
	                      MSG_COMP_ID_TRANSFER_CONTROL_PIO|
	                      MSG_COMP_ID_TRANSFER_DATA_PIO|
	                      MSG_COMP_ID_TRANSFER_CONTROL,
	                      MSG_COMP_ID_NONE,
	                      local_msg_recv,
	                      &monitor,
	                      &msg_register_id);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Initialize the control side. */
	result = transfer_control_stor_init(module->Msg,
	                                    module->Session,
	                                    Operation,
	                                    TransferInfo,
	                                    &transfer_control);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Initialize the data side. */
	result = transfer_data_stor_init(module->Msg,
	                                 Operation,
	                                 TransferInfo,
	                                 &transfer_data);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

    /* Inform the server that we are starting. */
	globus_gridftp_server_begin_transfer(Operation, 0, NULL);

	/* Start the data side. (sync) */
	result = transfer_data_run(transfer_data);

	/* Pass this message to the control side. */
	transfer_control_data_complete(transfer_control, result);

	/* Wait for the control side to complete. */
	globus_mutex_lock(&monitor.Lock);
	{
		if (monitor.Complete == GLOBUS_FALSE)
			globus_cond_wait(&monitor.Cond, &monitor.Lock);
	}
	globus_mutex_unlock(&monitor.Lock);

	/* Save off the result. */
	result = monitor.Result;

cleanup:
	/* Unregister to receive messages. */
	msg_unregister(module->Msg, msg_register_id);

	/* Destroy the data side. */
	transfer_data_destroy(transfer_data);

	/* Destroy the control side. */
	transfer_control_destroy(transfer_control);

	/* Destroy the monitor */
	local_monitor_destroy(&monitor);

	/* Let the server know we are finished. */
	globus_gridftp_server_finished_transfer(Operation, result);

	GlobusGFSHpssDebugExit();
}

void
local_session_end(void * Arg)
{
	module_t * module = Arg;

	if (module)
	{
		/* Desstroy the sesion. */
		session_destroy(module->Session);

		/* Destroy the message handle. */
		msg_destroy(module->Msg);

		/* Destroy the commands handle. */
		commands_destroy(module->Commands);

		/* Destroy the module. */
		globus_free(module);
	}
}

void
local_session_start(globus_gfs_operation_t      Operation,
                    globus_gfs_session_info_t * SessionInfo)
{
	char             * username       = NULL;
	char             * home_directory = NULL;
	globus_result_t    result         = GLOBUS_SUCCESS;
	module_t         * module         = NULL;
	sec_cred_t         user_cred;

	GlobusGFSName(local_session_start);
	GlobusGFSHpssDebugEnter();

	/* Allocate the module handle. */
	module = (module_t *)globus_calloc(1, sizeof(module_t));
	if (!module)
	{
		result = GlobusGFSErrorMemory("module_t");
		goto cleanup;
	}

	/* Initialize the session handle. */
	result = session_init(SessionInfo, &module->Session);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Authenticate this session to HPSS. */
	result = session_authenticate(module->Session);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Initialize the locally implemented FTP commands. */
	result = commands_init(Operation, &module->Commands);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Initialize the message handle. */
	result = msg_init(&module->Msg);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Get the username. */
	username = session_get_username(module->Session);

	/*
	 * Pulling the HPSS directory from the user's credential will support
	 * sites that use HPSS LDAP.
	 */
	result = hpss_GetThreadUcred(&user_cred);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	/* Copy out the user's home directory. */
	home_directory = globus_libc_strdup(user_cred.Directory);
	if (home_directory == NULL)
	{
		result = GlobusGFSErrorMemory("home_directory");
		goto cleanup;
	}

cleanup:
	if (result != GLOBUS_SUCCESS)
	{
		local_session_end(module);

		/* Free up home directory. */
		if (home_directory != NULL)
			globus_free(home_directory);
		home_directory = "HOME DIRECTORY";
	}

	/*
	 * Inform the server that we are done. If we do not pass in a username, the
	 * server will use the name we mapped to with GSI. If we do not pass in a
	 * home directory, the server will (1) look it up if we are root or
	 * (2) leave it as the unprivileged user's home directory.
	 *
	 * As far as I can tell, the server keeps a pointer to home_directory and frees
	 * it when it is done.
	 */
	globus_gridftp_server_finished_session_start(Operation,
                                                 result,
                                                 module,
                                                 username,
                                                 home_directory);

	GlobusGFSHpssDebugExit();
}

void
local_commands(globus_gfs_operation_t      Operation,
               globus_gfs_command_info_t * CommandInfo,
               void                      * UserArg)
{
	module_t * module = UserArg;

	globus_assert(UserArg);

	commands_handler(module->Commands, Operation, CommandInfo, module->Session, module->Msg);
}

#define STAT_ENTRIES_PER_REPLY 100

/*
 * Stat just the one object pointed to by StatInfo.
 */
void
local_stat(globus_gfs_operation_t   Operation,
           globus_gfs_stat_info_t * StatInfo,
           void                   * Arg)
{
	int                 retval         = 0;
	int                 dir_fd         = -1;
	int                 gfs_stat_count = 0;
	char              * entry_path     = NULL;
	hpss_dirent_t       dirent;
	globus_result_t     result = GLOBUS_SUCCESS;
	globus_gfs_stat_t   gfs_stat_array[STAT_ENTRIES_PER_REPLY];

	GlobusGFSName(__func__);
	GlobusGFSHpssDebugEnter();

	result = misc_gfs_stat(StatInfo->pathname, StatInfo->use_symlink_info, gfs_stat_array);
	if (result != GLOBUS_SUCCESS)
		goto cleanup;

	if (StatInfo->file_only || !S_ISDIR(gfs_stat_array[0].mode))
	{
		/* Inform the server that we are done. */
		globus_gridftp_server_finished_stat(Operation, GLOBUS_SUCCESS, gfs_stat_array, 1);

		/* Destroy the gfs_stat_array. */
		misc_destroy_gfs_stat(gfs_stat_array);

		GlobusGFSHpssDebugExit();
		return;
	}

	/* Open the directory. */
	dir_fd = hpss_Opendir(StatInfo->pathname);
	if (dir_fd < 0)
	{
		result = GlobusGFSErrorSystemError("hpss_Opendir", -dir_fd);
		goto cleanup;
	}

	while (TRUE)
	{
		/* Read the next entry. */
		retval = hpss_Readdir(dir_fd, &dirent);
		if (retval != 0)
		{
			result = GlobusGFSErrorSystemError("hpss_Readdir", -retval);
			break;
		}

		/* Check if we are done. */
		if (dirent.d_namelen == 0)
			break;

		result = misc_build_path(StatInfo->pathname, dirent.d_name, &entry_path);
		if (result != GLOBUS_SUCCESS)
			break;

		result = misc_gfs_stat(entry_path, StatInfo->use_symlink_info, &gfs_stat_array[gfs_stat_count++]);

		free(entry_path);
		if (result != GLOBUS_SUCCESS)
			break;

		if (gfs_stat_count == STAT_ENTRIES_PER_REPLY)
		{
			globus_gridftp_server_finished_stat_partial(Operation, GLOBUS_SUCCESS, gfs_stat_array, gfs_stat_count);
			misc_destroy_gfs_stat_array(gfs_stat_array, gfs_stat_count);
			gfs_stat_count = 0;
		}
	}

	hpss_Closedir(dir_fd);

cleanup:
	if (result != GLOBUS_SUCCESS)
	{
		/* Inform the server that we completed with an error. */
		globus_gridftp_server_finished_stat(Operation, result, NULL, 0);
		GlobusGFSHpssDebugExitWithError();
		return;
	}

	globus_gridftp_server_finished_stat(Operation, GLOBUS_SUCCESS, gfs_stat_array, gfs_stat_count);
	GlobusGFSHpssDebugExit();
	return;
}

globus_gfs_storage_iface_t local_dsi_iface = 
{
	0,                   /* Descriptor       */
	local_session_start, /* init_func        */
	local_session_end,   /* destroy_func     */
	NULL,                /* list_func        */
	local_retr,          /* send_func        */
	local_stor,          /* recv_func        */
	NULL,                /* trev_func        */
	NULL,                /* active_func      */
	NULL,                /* passive_func     */
	NULL,                /* data_destroy     */
	local_commands,      /* command_func     */
	local_stat,          /* stat_func        */
	NULL,                /* set_cred_func    */
	NULL,                /* buffer_send_func */
	NULL,                /* realpath_func    */
};
