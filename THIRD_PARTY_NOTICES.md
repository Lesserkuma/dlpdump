# Third-Party Notices

This project contains or interfaces with files derived from external Nintendo DS
homebrew projects and libraries.

| File | License | Purpose | Local changes |
|---|---|---|---|
| `arm7/source/mwl_mgmt_ext.c` | ZPL-2.1, Copyright fincs, devkitPro | Calico-derived management-frame helpers for DS Download Play association, based on Calico `source/dev/mwl/mwl_mgmt_frame.16.c`. | Project-specific wrappers, rates and association fields were added. |
| `arm7/source/mwl_rx_ext.c` | ZPL-2.1, Copyright fincs, devkitPro | Calico-derived RX task replacement, based on Calico `source/dev/mwl/mwl_rx.c`. | DS Download Play beacon forwarding, parent-command detection and statistics hooks were added. |
| `arm7/include/mwl_private_common.h` | ZPL-2.1, Copyright fincs, devkitPro | Calico private MWL declarations, based on Calico `source/dev/mwl/common.h`. | Only the private declarations required by the local RX/MGMT overrides are retained. |
| `arm7/source/arm7_boot.c` | Pico-Loader zlib-style notice, Copyright (c) 2025 LNH team | ARM7 bootstrap orchestration before handoff, including DS-mode preparation and handoff-ready event publication. | Boot stub install and handoff sequencing were adapted for DS Download Play payload launch control. |
| `arm7/include/arm7_internal.h` | Pico-Loader zlib-style notice, Copyright (c) 2025 LNH team | ARM7 internal interface exposing Pico-Loader-derived compatibility and boot helper wiring for local bootstrap flow. | Public/internal API surface was adapted to support the project-specific DLP handoff pipeline. |
| `arm7/source/boot_stub.s` | Pico-Loader zlib-style notice, Copyright (c) 2025 LNH team | ARM7 handover stub for launching saved DS Download Play payloads. | Pico-Loader-derived boot handoff knowledge was rewritten for project-specific IPC status words and fixed memory copying. |
| `arm7/source/twl_compat.c` | Pico-Loader zlib-style notice, Copyright (c) 2025 LNH team | TWL-to-NTR compatibility setup before boot handover, based on Pico-Loader `DSMode.cpp`. | The codec, touch/sound and volume-fix sequences were reduced to the compatibility path needed here. |
| `arm9/source/boot.c` | Pico-Loader zlib-style notice, Copyright (c) 2025 LNH team | ARM9 boot orchestration and handover into saved payloads, using Pico-Loader-derived cache/IPCSYNC/TWL-to-NTR ordering. | File loading, validation, fixed memory preparation and UI integration are project-specific. |

## Calico v1.2.0

Upstream copyright notice: Copyright fincs, devkitPro
Zope Public License (ZPL) Version 2.1

A copyright notice accompanies this license document that identifies
the copyright holders.

This license has been certified as open source. It has also been
designated as GPL compatible by the Free Software Foundation (FSF).

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

 1. Redistributions in source code must retain the accompanying
    copyright notice, this list of conditions, and the following
    disclaimer.

 2. Redistributions in binary form must reproduce the accompanying
    copyright notice, this list of conditions, and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.

 3. Names of the copyright holders must not be used to endorse or
    promote products derived from this software without prior written
    permission from the copyright holders.

 4. The right to distribute this software or to use it for any purpose
    does not give you the right to use Servicemarks (sm) or Trademarks
    (tm) of the copyright holders. Use of them is covered by separate
    agreement with the copyright holders.

 5. If any files are modified, you must cause the modified files to
    carry prominent notices stating that you changed the files and the
    date of any change.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Pico Loader v1.6.0

Copyright (c) 2025 LNH team

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
