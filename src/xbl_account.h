#ifndef XBL_ACCOUNT_H
#define XBL_ACCOUNT_H

#include <windows.h>

/* One Xbox LIVE account record is 0x6C bytes. On a HARD DRIVE the accounts are NOT
 * in a FATX header - they live in the raw-disk config/security area at the start of
 * the disk: one account per sector, sectors 12-19 (8 slots). Each of those sectors is
 * a standard config sector (header 0x79132568, two 0x1 dwords, data..., checksum,
 * footer 0xAA550000) and the 0x6C account record sits at offset 0x0C inside it.
 * The records are signed with hardcoded keys (roamable, NOT console-locked), so a
 * record copied from one Xbox is valid on another - exactly like a memory-card
 * account transfer. */
#define XBL_ACCOUNT_LEN         0x6C
#define XBL_ACCOUNT_MAX         8
#define XBL_SECTOR_SIZE         512
#define XBL_ACCOUNT_SECTOR0     12      /* first account sector on partition0 */
#define XBL_ACCOUNT_IN_SECTOR   0x0C    /* account record offset within its config sector */
#define XBL_CONFIG_HEADER       0x79132568u
#define XBL_CONFIG_FOOTER       0xAA550000u
#define XBL_CONFIG_CHECKSUM_OFF 0x1F8
#define XBL_CONFIG_FOOTER_OFF   0x1FC
#define XBL_SUPERBLOCK_LEN      0x1000  /* used only by the FATX diagnostic helpers */

typedef struct {
    BOOL present;
    char gamertag[17];   /* 15 chars + null */
    char xuidHex[17];    /* 16 hex chars + null */
    unsigned char raw[XBL_ACCOUNT_LEN];
} XblSlot;

typedef struct {
    int partition;       /* partition number the accounts were read from (1/2) */
    int presentCount;
    XblSlot slots[XBL_ACCOUNT_MAX];
} XblAccountSet;

/* Reads len bytes from the whole raw disk (\Device\Harddisk0\partition0) starting at
 * byteOffset. byteOffset and len must be multiples of 512 (sector size). On hard
 * drives the Xbox LIVE accounts live here in the config/security area, NOT in a FATX
 * header. Returns TRUE on success. */
BOOL xblReadRawDisk(unsigned long long byteOffset, unsigned char *out, unsigned int len);

/* Writes len bytes to the raw disk at byteOffset (sector-aligned). */
BOOL xblWriteRawDisk(unsigned long long byteOffset, const unsigned char *data, unsigned int len);

/* Reads the account slots from a specific partition's FATX superblock. Returns
 * FALSE if the partition can't be opened or lacks a valid FATX header. */
BOOL xblReadPartition(int partition, XblAccountSet *out);

/* Reads the 8 account slots from the raw-disk config area (sectors 12-19). out->slots
 * holds the raw 0x6C record and parsed gamertag/xuid for each present slot. Returns
 * TRUE if the config area could be read (slots may still be empty). */
BOOL xblReadAccounts(XblAccountSet *out);

/* Writes one 0x6C-byte account record into account slot (0-7 -> sector 12+slot) in
 * the raw-disk config area, wrapping it in a valid config sector (header/footer and
 * a recomputed checksum). Returns TRUE on success. */
BOOL xblWriteAccount(int slot, const unsigned char *record);

/* Finds a slot to restore into: the slot already holding xuidHex (overwrite), else
 * the first empty slot. Returns -1 if the set is full. */
int xblPickRestoreSlot(const XblAccountSet *set, const char *xuidHex);

/* Parses the gamertag/xuid out of a raw 0x6C record (for display/upload). */
void xblParseRecord(const unsigned char *record, char *gamertagOut, size_t gtSz,
                    char *xuidHexOut, size_t xuidSz);

#endif
