/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

This folder is for the baseboard for the board specific files which use Intel
Reference Validation Platform (RVP) for developing the EC and other peripherals
which can be hooked on EC or RVP.

This baseboard follows the Intel Modular Embedded Controller Card (MECC)
specification for pinout and these pin definitions remain same on all the
RVPs. Chrome MECC spec is standardized for Icelake and successor RVPs hence
this baseboard code is applicable to Icelake and its successors only.

Following hardware features are supported on MECC header by RVP and can be
validated by software by MECC.

MECC version 0.9 features
1. Power to MECC is provide by RVP (battery + DC Jack + Type C)
2. Power control pins for Intel SOC are added
3. Servo V2 header need to be added by MECC
4. Google H1 chip need to be added by MECC (optional for EC vendors)
5. 2 Type-C port support (SRC/SNK/BC1.2/MUX/Rerimer)
6. 6 Temperature sensors
7. 4 ADC
8. 4 I2C Channels
9. 1 Fan control

MECC version 1.0 features
1. Power to MECC is provide by RVP (battery + DC Jack + Type C)
2. Power control pins for Intel SOC are added
3. Servo V2 header need to be added by MECC
4. Google H1 chip need to be added by MECC (optional for EC vendors)
5. 4 Type-C port support (SRC/SNK/BC1.2/MUX/Rerimer) as Add In Card (AIC)
   on RVP
6. Optional 2 Type-C port routed to MECC for integrated TCPC support
7. 6 I2C Channels
8. 2 SMLINK Channels
9. 2 I3C channels
