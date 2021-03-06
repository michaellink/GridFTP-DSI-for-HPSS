##########################################################################
# University of Illinois/NCSA Open Source License
#
# Copyright � 2014, NCSA.  All rights reserved.
#
# Developed by:
#
# Storage Enabling Technologies (SET)
#
# Nation Center for Supercomputing Applications (NCSA)
#
# http://www.ncsa.illinois.edu
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the .Software.),
# to deal with the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
#    + Redistributions of source code must retain the above copyright notice,
#      this list of conditions and the following disclaimers.
#
#    + Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimers in the
#      documentation and/or other materials provided with the distribution.
#
#    + Neither the names of SET, NCSA
#      nor the names of its contributors may be used to endorse or promote
#      products derived from this Software without specific prior written
#      permission.
#
# THE SOFTWARE IS PROVIDED .AS IS., WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS WITH THE SOFTWARE.
############################################################################

HPSS_ROOT=$(HPSS_LOCATION)

#
# Include the HPSS Makefile get get build information. The HPSS Makefile
# expects LOCAL_ROOT to be set.
#
LOCAL_ROOT=$(HPSS_ROOT)
include $(HPSS_LOCATION)/Makefile.macros

# Build targets of the form <dir>_<primary> where dir is where to install it:
#   Ex. bin, man, lib, pkglib, include
# and primary is what type of target it is:
#   Ex. PROGRAMS, MANS, LTLIBRARIES, SCRIPTS, DATA, LIBRARIES, HEADERS
# Note: LTLIBRARIES is a libtool library
# Note: The names of these libraries are important, the gridftp server appends
# our dsi name (hpss_control or hpss_data) onto libglobus_gridftp_server_ when loading 
# our DSI.
lib_LTLIBRARIES = libglobus_gridftp_server_hpss_real_local.la  

SOURCES = gridftp_dsi_hpss_transfer_control.c \
	      gridftp_dsi_hpss_transfer_control.h \
	      gridftp_dsi_hpss_transfer_data.c    \
	      gridftp_dsi_hpss_transfer_data.h    \
	      gridftp_dsi_hpss_pio_control.c      \
	      gridftp_dsi_hpss_pio_control.h      \
	      gridftp_dsi_hpss_data_ranges.c      \
	      gridftp_dsi_hpss_data_ranges.h      \
	      gridftp_dsi_hpss_range_list.c       \
	      gridftp_dsi_hpss_range_list.h       \
	      gridftp_dsi_hpss_checksum.c         \
	      gridftp_dsi_hpss_checksum.h         \
	      gridftp_dsi_hpss_commands.c         \
	      gridftp_dsi_hpss_commands.h         \
	      gridftp_dsi_hpss_pio_data.c         \
	      gridftp_dsi_hpss_pio_data.h         \
	      gridftp_dsi_hpss_data_hub.h         \
	      gridftp_dsi_hpss_gridftp.c          \
	      gridftp_dsi_hpss_gridftp.h          \
	      gridftp_dsi_hpss_session.c          \
	      gridftp_dsi_hpss_session.h          \
	      gridftp_dsi_hpss_buffer.c           \
	      gridftp_dsi_hpss_buffer.h           \
	      gridftp_dsi_hpss_config.c           \
	      gridftp_dsi_hpss_config.h           \
	      gridftp_dsi_hpss_local.c            \
	      gridftp_dsi_hpss_misc.c             \
	      gridftp_dsi_hpss_misc.h             \
	      gridftp_dsi_hpss_list.c             \
	      gridftp_dsi_hpss_list.h             \
	      gridftp_dsi_hpss_dl.c               \
	      gridftp_dsi_hpss_dl.h               \
	      gridftp_dsi_hpss_markers.c          \
	      gridftp_dsi_hpss_markers.h          \
	      gridftp_dsi_hpss_msg.c              \
	      gridftp_dsi_hpss_msg.h

libglobus_gridftp_server_hpss_real_local_la_SOURCES=$(SOURCES)


CPPFLAGS=-Wall -DLINUX $(GLOBUS_CPPFLAGS) -I$(HPSS_ROOT)/include
LDFLAGS=-L$(HPSS_ROOT)/lib -Wl,-rpath,$(HPSS_ROOT)/lib $(GLOBUS_LDFLAGS)

#
# IU and NERSC report that using krb fails because libhpssgss.so doesn't link against
# libhpsskrbauth.so. NERSC reported that libhpssunixauth.so was necessary in their
# environment. It is necessary to link these here; the XDR/libtirpc issue causes the
# runtime dynamic linking to fail without this.
#
libglobus_gridftp_server_hpss_real_local_la_LIBADD=-lglobus_gridftp_server -lhpss -lhpsskrb5auth -lhpssunixauth

# Uncommeting this line would cause libtool to use gcc to link the libraries
# instead of g++ when at least one file ends in .cc
#CXXLINK = $(LIBTOOL) --tag=CC --mode=link $(CCLD) $(AM_CCFLAGS) $(CCFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@

