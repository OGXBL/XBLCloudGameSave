#ifndef XBL_ACCOUNT_H
#define XBL_ACCOUNT_H

#include <windows.h>

/* One Xbox LIVE account record is 0x6C bytes (see Original-Xbox-LIVE-Account).
 *
 * Where it lives depends on the device:
 * - Memory card / PC tools: FATX volume header at 0x50 + slot*0x6C.
 * - Hard drive (retail + modded): raw disk partition0, one account per config
 *   sector 12-19, record at offset 0x0C inside each 512-byte sector. This is
 *   what Insignia/XOnline use on HDD — NOT the FATX superblock. */
#define XBL_ACCOUNT_LEN         0x6C
#define XBL_ACCOUNT_MAX         8
#define XBL_SECTOR_SIZE         512
#define XBL_FATX_MAGIC          0x58544146u /* "FATX" little-endian */
#define XBL_ACCOUNT_BASE        0x50
#define XBL_SUPERBLOCK_IO       0x400
#define XBL_CONFIG_DISK_PART    0
#define XBL_CONFIG_FIRST_SECTOR 12
#define XBL_CONFIG_RECORD_OFF   0x0C
#define XBL_CONFIG_HEADER       0x79132568u
#define XBL_CONFIG_FOOTER       0xAA550000u

typedef struct {
    BOOL present;        /* slot has a plausible gamertag + XUID */
    BOOL verified;       /* passes full XOnline signature check */
    char gamertag[17];   /* 15 chars + null */
    char xuidHex[17];    /* 16 hex chars + null */
    unsigned char raw[XBL_ACCOUNT_LEN];
} XblSlot;

typedef struct {
    int partition;       /* 0 = config sectors; 1+ = FATX partition number */
    BOOL useConfigSectors;
    int presentCount;
    int verifiedCount;
    XblSlot slots[XBL_ACCOUNT_MAX];
} XblAccountSet;

BOOL xblReadPartition(int partition, XblAccountSet *out);
BOOL xblReadAccounts(XblAccountSet *out);
BOOL xblWriteAccount(int partition, int slot, const unsigned char *record);
int xblPickRestoreSlot(const XblAccountSet *set, const char *xuidHex);
/* Case-insensitive compare of two hex XUID strings (TRUE if equal). */
BOOL xblXuidEqual(const char *a, const char *b);
void xblParseRecord(const unsigned char *record, char *gamertagOut, size_t gtSz,
                    char *xuidHexOut, size_t xuidSz);
BOOL xblVerifyAccount(const unsigned char *record);

/* Writes a diagnostic text file (always attempt after account scan). */
BOOL xblWriteProbeFile(const char *path, const XblAccountSet *scan);

#endif
