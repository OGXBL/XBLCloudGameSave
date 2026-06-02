#ifndef XBMC_PROFILES_H
#define XBMC_PROFILES_H

#include <windows.h>

#define XBMC_MAX_VARIATIONS 16

typedef struct {
    char profileKey[40];    /* sanitized key; "" for default / non-XBMC */
    char profileLabel[64];  /* human-readable profile name; "" for default */
    char path[MAX_PATH];    /* full UDATA path for this variation */
} XbmcVariation;

/* Enumerates the UDATA variations to back up. baseUdataPath is the live UDATA
 * folder (e.g. "E:\\UDATA").
 *
 * When XBMC4Gamers per-profile saves are NOT in use, returns exactly one entry
 * with an empty profileKey and path == baseUdataPath — identical to the previous
 * single-folder behavior, so non-XBMC dashboards sync exactly as before.
 *
 * When per-profile saves ARE in use (detected by parsing XBMC's own
 * udata_settings.xml on the drive and/or sibling "UDATA.<profile>" folders),
 * returns one entry per profile: out[0] is the currently-active UDATA folder
 * (named from <last_profile> in udata_settings.xml), followed by each stashed
 * "UDATA.<profile>" folder.
 *
 * Always returns >= 1. */
int xbmcEnumerateVariations(const char *baseUdataPath, XbmcVariation *out, int maxOut);

#endif
