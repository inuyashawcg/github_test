#!/usr/local/bin/ksh93 -p
#
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

# $FreeBSD: releng/12.0/tests/sys/cddl/zfs/tests/cli_user/misc/zpool_scrub_001_neg.ksh 329867 2018-02-23 16:31:00Z asomers $

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_scrub_001_neg.ksh	1.1	07/10/09 SMI"
#

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/cli_user/cli_user.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID: zpool_scrub_001_neg
#
# DESCRIPTION:
#
# zpool scrub returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to start a scrub on a pool
# 2. Verify the command fails
#
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-07-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "zpool scrub returns an error when run as a user"

log_mustnot run_unprivileged "$ZPOOL scrub $TESTPOOL"
log_mustnot run_unprivileged "$ZPOOL scrub -s $TESTPOOL"

log_pass "zpool scrub returns an error when run as a user"
