#include "xbl_account.h"

#include <stdio.h>
#include <string.h>
#include <xboxkrnl/xboxkrnl.h>

/* Field offsets inside a 0x6C account record (see Original-Xbox-LIVE-Account). */
#define XBL_OFF_XUID      0x00 /* uint64 */
#define XBL_OFF_GAMERTAG  0x0C /* 16 bytes, null-terminated ASCII */

void xblParseRecord(const unsigned char *record, char *gamertagOut, size_t gtSz,
                    char *xuidHexOut, size_t xuidSz)
{
    if (gamertagOut && gtSz > 0) {
        size_t n = 0;
        for (; n < 15 && n < gtSz - 1; n++) {
            unsigned char c = record[XBL_OFF_GAMERTAG + n];
            if (c == 0) {
                break;
            }
            gamertagOut[n] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        gamertagOut[n] = '\0';
    }
    if (xuidHexOut && xuidSz >= 17) {
        /* XUID is a little-endian uint64; print as 16 hex chars (high byte first). */
        unsigned long long xuid = 0;
        for (int i = 0; i < 8; i++) {
            xuid |= ((unsigned long long)record[XBL_OFF_XUID + i]) << (8 * i);
        }
        snprintf(xuidHexOut, xuidSz, "%016llX", xuid);
    }
}

static BOOL xblRecordPresent(const unsigned char *record)
{
    /* A real account must start with a printable gamertag character (the on-disk
     * format requires a non-empty, null-terminated gamertag). This rejects unused
     * superblock space, which is filled with 0xFF (or 0x00) - those would otherwise
     * look like a bogus account (gamertag "???????????????", XUID FFFFFFFFFFFFFFFF). */
    unsigned char g0 = record[XBL_OFF_GAMERTAG];
    if (g0 < 0x20 || g0 > 0x7E) {
        return FALSE;
    }
    /* Reject an all-0xFF / all-0x00 XUID just in case. */
    BOOL allFF = TRUE;
    BOOL allZero = TRUE;
    for (int i = 0; i < 8; i++) {
        if (record[XBL_OFF_XUID + i] != 0xFF) allFF = FALSE;
        if (record[XBL_OFF_XUID + i] != 0x00) allZero = FALSE;
    }
    if (allFF || allZero) {
        return FALSE;
    }
    return TRUE;
}

static HANDLE xblOpenRawDisk(BOOL forWrite)
{
    ANSI_STRING str;
    RtlInitAnsiString(&str, "\\Device\\Harddisk0\\partition0");

    OBJECT_ATTRIBUTES obj;
    InitializeObjectAttributes(&obj, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);

    HANDLE h = INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK iosb;
    ACCESS_MASK access = GENERIC_READ | SYNCHRONIZE;
    if (forWrite) {
        access |= GENERIC_WRITE;
    }
    NTSTATUS status = NtOpenFile(&h, access, &obj, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(status)) {
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

BOOL xblReadRawDisk(unsigned long long byteOffset, unsigned char *out, unsigned int len)
{
    if (!out || len == 0) {
        return FALSE;
    }
    HANDLE h = xblOpenRawDisk(FALSE);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)byteOffset;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtReadFile(h, NULL, NULL, NULL, &iosb, out, len, &offset);
    NtClose(h);
    return NT_SUCCESS(status);
}

BOOL xblWriteRawDisk(unsigned long long byteOffset, const unsigned char *data, unsigned int len)
{
    if (!data || len == 0) {
        return FALSE;
    }
    HANDLE h = xblOpenRawDisk(TRUE);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)byteOffset;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)data, len, &offset);
    NtClose(h);
    return NT_SUCCESS(status);
}

/* Same additive checksum the kernel/nxdk use for config sectors (computed over the
 * whole 512-byte sector with the checksum field zeroed; the stored value is ~result). */
static unsigned int xblConfigChecksum(const unsigned char *sector)
{
    const unsigned int *data = (const unsigned int *)sector;
    unsigned int sum = 0;
    unsigned int carry = 0;
    for (int i = XBL_SECTOR_SIZE >> 2; i; --i) {
        carry += (*data > (*data + sum));
        sum += *data;
        data++;
    }
    return (carry > (carry + sum)) + carry + sum;
}

static unsigned int xblRdU32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

static void xblWrU32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

BOOL xblReadAccounts(XblAccountSet *out)
{
    if (!out) {
        return FALSE;
    }
    memset(out, 0, sizeof(*out));
    out->partition = 0; /* raw disk (partition0) */

    static unsigned char sectors[XBL_ACCOUNT_MAX * XBL_SECTOR_SIZE];
    if (!xblReadRawDisk((unsigned long long)XBL_ACCOUNT_SECTOR0 * XBL_SECTOR_SIZE, sectors,
                        sizeof(sectors))) {
        return FALSE;
    }

    for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
        const unsigned char *sec = sectors + i * XBL_SECTOR_SIZE;
        const unsigned char *rec = sec + XBL_ACCOUNT_IN_SECTOR;
        memcpy(out->slots[i].raw, rec, XBL_ACCOUNT_LEN);
        /* A slot holds an account only if the config sector is initialised (valid
         * header) and the record has a printable gamertag. */
        BOOL hasHeader = (xblRdU32(sec) == XBL_CONFIG_HEADER);
        out->slots[i].present = hasHeader && xblRecordPresent(rec);
        xblParseRecord(rec, out->slots[i].gamertag, sizeof(out->slots[i].gamertag),
                       out->slots[i].xuidHex, sizeof(out->slots[i].xuidHex));
        if (out->slots[i].present) {
            out->presentCount++;
        }
    }
    return TRUE;
}

BOOL xblWriteAccount(int slot, const unsigned char *record)
{
    if (slot < 0 || slot >= XBL_ACCOUNT_MAX || !record) {
        return FALSE;
    }

    unsigned long long sectorOffset =
        (unsigned long long)(XBL_ACCOUNT_SECTOR0 + slot) * XBL_SECTOR_SIZE;

    /* Read the existing sector so we preserve any unrelated bytes in the data area. */
    static unsigned char sec[XBL_SECTOR_SIZE];
    if (!xblReadRawDisk(sectorOffset, sec, XBL_SECTOR_SIZE)) {
        memset(sec, 0, sizeof(sec));
    }

    /* Wrap the account in a valid config sector frame. */
    xblWrU32(sec + 0x00, XBL_CONFIG_HEADER);
    xblWrU32(sec + 0x04, 1);
    xblWrU32(sec + 0x08, 1);
    memcpy(sec + XBL_ACCOUNT_IN_SECTOR, record, XBL_ACCOUNT_LEN);
    xblWrU32(sec + XBL_CONFIG_FOOTER_OFF, XBL_CONFIG_FOOTER);

    /* Recompute the config-sector checksum (field zeroed first; stored as ~result). */
    xblWrU32(sec + XBL_CONFIG_CHECKSUM_OFF, 0);
    unsigned int cs = ~xblConfigChecksum(sec);
    xblWrU32(sec + XBL_CONFIG_CHECKSUM_OFF, cs);

    return xblWriteRawDisk(sectorOffset, sec, XBL_SECTOR_SIZE);
}

int xblPickRestoreSlot(const XblAccountSet *set, const char *xuidHex)
{
    if (!set) {
        return -1;
    }
    /* Overwrite an existing copy of the same account if present. */
    if (xuidHex && xuidHex[0]) {
        for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
            if (set->slots[i].present && _stricmp(set->slots[i].xuidHex, xuidHex) == 0) {
                return i;
            }
        }
    }
    /* Otherwise the first empty slot. */
    for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
        if (!set->slots[i].present) {
            return i;
        }
    }
    return -1;
}
