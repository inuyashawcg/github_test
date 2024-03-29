# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD: releng/12.0/tests/sys/cddl/zfs/tests/threadsappend/threadsappend_test.sh 329867 2018-02-23 16:31:00Z asomers $

#
# Copyright 2012 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#


atf_test_case threadsappend_001_pos cleanup
threadsappend_001_pos_head()
{
	atf_set "descr" "Ensure multiple threads performing write appends to the sameZFS file succeed"
}
threadsappend_001_pos_body()
{
	export PATH=$(atf_get_srcdir):$PATH
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/threadsappend.cfg

	verify_disk_count "$DISKS" 1
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/threadsappend_001_pos.ksh || atf_fail "Testcase failed"
}
threadsappend_001_pos_cleanup()
{
	. $(atf_get_srcdir)/../../include/default.cfg
	. $(atf_get_srcdir)/threadsappend.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case threadsappend_001_pos
}
