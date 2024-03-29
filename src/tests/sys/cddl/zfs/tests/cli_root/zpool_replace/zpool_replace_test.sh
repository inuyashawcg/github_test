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

# $FreeBSD: releng/12.0/tests/sys/cddl/zfs/tests/cli_root/zpool_replace/zpool_replace_test.sh 329867 2018-02-23 16:31:00Z asomers $

#
# Copyright 2012 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#


atf_test_case zpool_replace_001_neg cleanup
zpool_replace_001_neg_head()
{
	atf_set "descr" "Executing 'zpool replace' with bad options fails"
	atf_set "require.progs"  zpool
}
zpool_replace_001_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_replace.cfg

	verify_disk_count "$DISKS" 2
	ksh93 $(atf_get_srcdir)/setup.ksh || atf_fail "Setup failed"
	ksh93 $(atf_get_srcdir)/zpool_replace_001_neg.ksh || atf_fail "Testcase failed"
}
zpool_replace_001_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_replace.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}

atf_test_case zpool_replace_002_neg cleanup
zpool_replace_002_neg_head()
{
	atf_set "descr" "'zpool replace' should fail if the new device is too small"
	atf_set "require.progs"  zpool
}
zpool_replace_002_neg_body()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_replace.cfg

	ksh93 $(atf_get_srcdir)/zpool_replace_002_neg.ksh || atf_fail "Testcase failed"
}
zpool_replace_002_neg_cleanup()
{
	. $(atf_get_srcdir)/../../../include/default.cfg
	. $(atf_get_srcdir)/zpool_replace.cfg

	ksh93 $(atf_get_srcdir)/cleanup.ksh || atf_fail "Cleanup failed"
}


atf_init_test_cases()
{

	atf_add_test_case zpool_replace_001_neg
	atf_add_test_case zpool_replace_002_neg
}
