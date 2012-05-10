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

#ifndef GLOBUS_GRIDFTP_SERVER_HPSS_COMMON_H
#define GLOBUS_GRIDFTP_SERVER_HPSS_COMMON_H

/*
 * System includes.
 */
#include <grp.h>

/*
 * Globus includes.
 */
#include <globus_gridftp_server.h>
#include <globus_common.h>
#include <globus_debug.h>

/*
 * This is used to define the debug print statements.
 */
GlobusDebugDeclare(GLOBUS_GRIDFTP_SERVER_HPSS);

#define GlobusGFSHpssDebugPrintf(level, message)                             \
    GlobusDebugPrintf(GLOBUS_GRIDFTP_SERVER_HPSS, level, message)

#define GlobusGFSHpssDebugEnter()                                            \
    GlobusGFSHpssDebugPrintf(                                                \
        GLOBUS_GFS_DEBUG_TRACE,                                              \
        ("[%s] Entering\n", _gfs_name))

#define GlobusGFSHpssDebugExit()                                             \
    GlobusGFSHpssDebugPrintf(                                                \
        GLOBUS_GFS_DEBUG_TRACE,                                              \
        ("[%s] Exiting\n", _gfs_name))

#define GlobusGFSHpssDebugExitWithError()                                    \
    GlobusGFSHpssDebugPrintf(                                                \
        GLOBUS_GFS_DEBUG_TRACE,                                              \
        ("[%s] Exiting with error\n", _gfs_name))



globus_result_t
globus_l_gfs_hpss_common_stat(char              *  Path,
                              globus_bool_t        FileOnly,
                              globus_bool_t        UseSymlinkInfo,
                              globus_bool_t        IncludePathStat,
                              globus_gfs_stat_t ** StatBufArray,
                              int               *  StatBufCount);

void
globus_l_gfs_hpss_common_destroy_stat_array(
    globus_gfs_stat_t * StatBufArray,
    int                 StatCount);

/*
 * Passwd file translations.
 */
globus_result_t
globus_l_gfs_hpss_common_username_to_home(char *  UserName, 
                                          char ** HomeDirectory);

globus_result_t
globus_l_gfs_hpss_common_username_to_uid(char * UserName,
                                         int  * Uid);

globus_result_t
globus_l_gfs_hpss_common_uid_to_username(int     Uid,
                                         char ** UserName);


globus_result_t
globus_l_gfs_hpss_common_groupname_to_gid(char  * GroupName,
                                          gid_t * Gid);
#ifdef NOT

/*
 * Helper to release memory associated with errors.
 */
void
globus_l_gfs_hpss_common_destroy_result(globus_result_t Result);

#endif /* NOT */
char *
globus_l_gfs_hpss_common_strndup(char * String, int Length);

#endif /* GLOBUS_GRIDFTP_SERVER_HPSS_COMMON_H */