/*
 * Shadow nxdk's lwipopts.h: include it once, then undef LWIP_DEBUG so lwIP does not
 * call LWIP_PLATFORM_DIAG -> debugPrint (the same framebuffer the QR code is drawn into).
 *
 * Path assumes nxdk lives at <project>/nxdk (this file is at <project>/lwip_override).
 * If NXDK_DIR differs, edit this include path.
 */
#ifndef LWIP_LWIPOPTS_H
#include "../nxdk/lib/net/nforceif/include/lwipopts.h"
#undef LWIP_DEBUG
#endif
