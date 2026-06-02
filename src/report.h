#ifndef REPORT_H
#define REPORT_H

#include <windows.h>

#include "scan_udata.h"

/* Writes a human-readable text report of the scan to path. */
BOOL writeReport(const char *path, const ScanResult *result);

#endif
