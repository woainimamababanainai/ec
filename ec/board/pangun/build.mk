# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7
CHIP_VARIANT:=npcx7m6fc
# We do not use BASEBOARD, just use one board file.
# BASEBOARD:=zork

board-y=board.o usb_pd_policy.o thermalInfo.o
