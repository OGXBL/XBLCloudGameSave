#include "xbl_account.h"

#include "eeprom_export.h"

#include <stdio.h>
#include <string.h>
#include <xboxkrnl/xboxkrnl.h>

#include <mbedtls/des.h>
#include <mbedtls/md.h>

/* Xbox applies odd parity to each byte of the 24-byte 3DES key before use
 * (XCryptDESKeyParity). Without this, signature verification always fails. */
static void xblApplyDesKeyParity(unsigned char key[24])
{
    for (int i = 0; i < 24; i += 8) {
        mbedtls_des_key_set_parity(key + i);
    }
}

/* Field offsets inside a 0x6C account record (see Original-Xbox-LIVE-Account). */
#define XBL_OFF_XUID      0x00 /* uint64 */
#define XBL_OFF_RESERVED  0x08 /* uint32, must be zero */
#define XBL_OFF_GAMERTAG  0x0C /* 16 bytes, null-terminated ASCII */
#define XBL_OFF_FLAGS     0x1C /* uint32, top nibble reserved */
#define XBL_OFF_CONFOUNDER 0x50 /* 0x14 bytes; first 0x10 are TripleDES-encrypted */
#define XBL_OFF_SIGNATURE 0x64 /* 8 bytes: first 8 of HMAC-SHA1 over [0..0x64) */

/* Hardcoded "roamable" signing material (NOT the console HDD key), reversed from XOnline
 * in feudalnate/Original-Xbox-LIVE-Account. These make an account valid on any console. */
static const unsigned char XBL_SEED_DATA[16] = {
    0xA7, 0x14, 0x21, 0x3D, 0x94, 0x46, 0x1E, 0x05, 0x97, 0x6D, 0xE8, 0x35, 0x21, 0x2A, 0xE5, 0x7C};
static const unsigned char XBL_SEED_KEY_A[16] = {
    0x2B, 0xB8, 0xD9, 0xEF, 0xD2, 0x04, 0x6D, 0x9D, 0x1F, 0x39, 0xB1, 0x5B, 0x46, 0x58, 0x01, 0xD7};
static const unsigned char XBL_SEED_KEY_B[16] = {
    0x1E, 0x05, 0xD7, 0x3A, 0xA4, 0x20, 0x6A, 0x7B, 0xA0, 0x5B, 0xCD, 0xDF, 0xAD, 0x26, 0xD3, 0xDE};
static const unsigned char XBL_AUTH_KEY[16] = {
    0x62, 0xBD, 0x92, 0xB6, 0x4F, 0x45, 0x84, 0x70, 0xD3, 0xFF, 0x4F, 0x22, 0x3C, 0x6E, 0xE7, 0xEA};
static const unsigned char XBL_IV[8] = {0x7B, 0x35, 0xA8, 0xB7, 0x27, 0xED, 0x43, 0x7A};

static int xblHmacSha1(const unsigned char *key, size_t keyLen, const unsigned char *data,
                       size_t dataLen, unsigned char out[20])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!info) {
        return -1;
    }
    return mbedtls_md_hmac(info, key, keyLen, data, dataLen, out);
}

static int xblBuild3desKey(unsigned char key[24])
{
    unsigned char hashA[20];
    unsigned char hashB[20];
    if (xblHmacSha1(XBL_SEED_KEY_A, 16, XBL_SEED_DATA, 16, hashA) != 0) {
        return -1;
    }
    if (xblHmacSha1(XBL_SEED_KEY_B, 16, XBL_SEED_DATA, 16, hashB) != 0) {
        return -1;
    }
    memcpy(key, hashA, 4);
    memcpy(key + 4, hashB, 20);
    xblApplyDesKeyParity(key);
    return 0;
}

BOOL xblVerifyAccount(const unsigned char *record)
{
    if (!record) {
        return FALSE;
    }

    unsigned char acc[XBL_ACCOUNT_LEN];
    memcpy(acc, record, XBL_ACCOUNT_LEN);

    unsigned char key[24];
    if (xblBuild3desKey(key) != 0) {
        return FALSE;
    }

    mbedtls_des3_context ctx;
    mbedtls_des3_init(&ctx);
    if (mbedtls_des3_set3key_dec(&ctx, key) != 0) {
        mbedtls_des3_free(&ctx);
        return FALSE;
    }
    unsigned char iv[8];
    memcpy(iv, XBL_IV, 8);
    int rc = mbedtls_des3_crypt_cbc(&ctx, MBEDTLS_DES_DECRYPT, 0x10, iv, acc + XBL_OFF_CONFOUNDER,
                                    acc + XBL_OFF_CONFOUNDER);
    mbedtls_des3_free(&ctx);
    if (rc != 0) {
        return FALSE;
    }

    unsigned char hash[20];
    if (xblHmacSha1(XBL_AUTH_KEY, 16, acc, XBL_OFF_SIGNATURE, hash) != 0) {
        return FALSE;
    }
    if (memcmp(hash, acc + XBL_OFF_SIGNATURE, 8) != 0) {
        return FALSE;
    }

    if (acc[XBL_OFF_FLAGS + 3] & 0xF0) {
        return FALSE;
    }
    if (acc[XBL_OFF_RESERVED] | acc[XBL_OFF_RESERVED + 1] | acc[XBL_OFF_RESERVED + 2] |
        acc[XBL_OFF_RESERVED + 3]) {
        return FALSE;
    }
    if (acc[0x1B] != 0) {
        return FALSE;
    }
    if (acc[0x37] != 0) {
        return FALSE;
    }
    if (acc[0x4F] != 0) {
        return FALSE;
    }
    return TRUE;
}

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
        unsigned long long xuid = 0;
        for (int i = 0; i < 8; i++) {
            xuid |= ((unsigned long long)record[XBL_OFF_XUID + i]) << (8 * i);
        }
        snprintf(xuidHexOut, xuidSz, "%016llX", xuid);
    }
}

static BOOL xblRecordPresent(const unsigned char *record)
{
    unsigned char g0 = record[XBL_OFF_GAMERTAG];
    if (g0 < 0x20 || g0 > 0x7E) {
        return FALSE;
    }
    BOOL allFF = TRUE;
    BOOL allZero = TRUE;
    for (int i = 0; i < 8; i++) {
        if (record[XBL_OFF_XUID + i] != 0xFF) {
            allFF = FALSE;
        }
        if (record[XBL_OFF_XUID + i] != 0x00) {
            allZero = FALSE;
        }
    }
    if (allFF || allZero) {
        return FALSE;
    }
    return TRUE;
}

static HANDLE xblOpenPartition(int partition, BOOL forWrite)
{
    if (partition == XBL_CONFIG_DISK_PART) {
        static const char *part0Paths[] = {
            "\\Device\\Harddisk0\\partition0",
            "\\Device\\Harddisk0\\Partition0",
            "\\Device\\Harddisk0\\Partition0\\",
        };
        for (int i = 0; i < (int)(sizeof(part0Paths) / sizeof(part0Paths[0])); i++) {
            ANSI_STRING str;
            RtlInitAnsiString(&str, part0Paths[i]);
            OBJECT_ATTRIBUTES obj;
            InitializeObjectAttributes(&obj, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);
            HANDLE h = INVALID_HANDLE_VALUE;
            IO_STATUS_BLOCK iosb;
            ACCESS_MASK access = GENERIC_READ | SYNCHRONIZE;
            if (forWrite) {
                access |= GENERIC_WRITE;
            }
            /* Always open for synchronous IO; a non-synchronous handle makes
             * NtReadFile return STATUS_PENDING and our synchronous read reads garbage. */
            NTSTATUS status = NtOpenFile(&h, access, &obj, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         FILE_SYNCHRONOUS_IO_ALERT);
            if (NT_SUCCESS(status)) {
                return h;
            }
        }
        return INVALID_HANDLE_VALUE;
    }

    char pathBuf[64];
    snprintf(pathBuf, sizeof(pathBuf), "\\Device\\Harddisk0\\Partition%d", partition);

    ANSI_STRING str;
    RtlInitAnsiString(&str, pathBuf);

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

static BOOL xblReadPartitionBytes(int partition, unsigned long long offset, unsigned char *out,
                                  unsigned int len, BOOL forWrite)
{
    if (!out || len == 0) {
        return FALSE;
    }
    HANDLE h = xblOpenPartition(partition, forWrite);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    LARGE_INTEGER off;
    off.QuadPart = (LONGLONG)offset;
    IO_STATUS_BLOCK iosb;
    memset(&iosb, 0, sizeof(iosb));
    NTSTATUS status = NtReadFile(h, NULL, NULL, NULL, &iosb, out, len, &off);
    NtClose(h);
    /* Match nxdk's proven configsector.c: only the status matters. The Xbox kernel
     * does not reliably populate iosb.Information for raw partition0 reads, so do
     * NOT gate success on it (doing so makes every read look like a failure). */
    return NT_SUCCESS(status);
}

static BOOL xblWritePartitionBytes(int partition, unsigned long long offset, const unsigned char *data,
                                   unsigned int len)
{
    if (!data || len == 0) {
        return FALSE;
    }
    HANDLE h = xblOpenPartition(partition, TRUE);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    LARGE_INTEGER off;
    off.QuadPart = (LONGLONG)offset;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)data, len, &off);
    NtClose(h);
    return NT_SUCCESS(status);
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

static unsigned int xblConfigChecksum(const unsigned char *sector, unsigned int size)
{
    const unsigned int *data = (const unsigned int *)sector;
    unsigned int sum = 0;
    unsigned int carry = 0;
    unsigned int count = size >> 2;

    for (unsigned int i = 0; i < count; i++) {
        unsigned int word = data[i];
        carry += (word > (word + sum)) ? 1u : 0u;
        sum += word;
    }
    return (carry > (carry + sum) ? 1u : 0u) + carry + sum;
}

static void xblFillSlotFromRecord(XblSlot *slot, const unsigned char *rec)
{
    memcpy(slot->raw, rec, XBL_ACCOUNT_LEN);
    BOOL verified = xblVerifyAccount(rec);
    BOOL looksUsed = verified || xblRecordPresent(rec);
    slot->present = looksUsed;
    slot->verified = verified;
    xblParseRecord(rec, slot->gamertag, sizeof(slot->gamertag), slot->xuidHex, sizeof(slot->xuidHex));
}

static BOOL xblExtractAccountFromSector(const unsigned char *sector, unsigned char *recOut)
{
    static const int offsets[] = {XBL_CONFIG_RECORD_OFF, 0x00, XBL_ACCOUNT_BASE, 0x18};
    for (int i = 0; i < (int)(sizeof(offsets) / sizeof(offsets[0])); i++) {
        int off = offsets[i];
        if (off < 0 || off + XBL_ACCOUNT_LEN > XBL_SECTOR_SIZE) {
            continue;
        }
        const unsigned char *cand = sector + off;
        if (xblVerifyAccount(cand) || xblRecordPresent(cand)) {
            memcpy(recOut, cand, XBL_ACCOUNT_LEN);
            return TRUE;
        }
    }
    for (int off = 0; off <= (int)XBL_SECTOR_SIZE - (int)XBL_ACCOUNT_LEN; off += 4) {
        const unsigned char *cand = sector + off;
        if (xblVerifyAccount(cand)) {
            memcpy(recOut, cand, XBL_ACCOUNT_LEN);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL xblReadDiskSector(int sectorNum, unsigned char *sectorOut)
{
    if (!sectorOut || sectorNum < 0) {
        return FALSE;
    }
    unsigned long long off = (unsigned long long)sectorNum * XBL_SECTOR_SIZE;
    return xblReadPartitionBytes(XBL_CONFIG_DISK_PART, off, sectorOut, XBL_SECTOR_SIZE, FALSE);
}

static void xblProbeSector(int sectorNum, char *text, size_t textSz, int *pos)
{
    static unsigned char sector[XBL_SECTOR_SIZE];
    static unsigned char rec[XBL_ACCOUNT_LEN];

    if (!text || !pos || textSz == 0) {
        return;
    }

    if (!xblReadDiskSector(sectorNum, sector)) {
        *pos += snprintf(text + *pos, textSz - (size_t)*pos, "sector %d: READ FAILED\r\n", sectorNum);
        return;
    }

    *pos += snprintf(text + *pos, textSz - (size_t)*pos,
                     "sector %d: hdr=0x%08X footer=0x%08X\r\n", sectorNum, xblRdU32(sector),
                     xblRdU32(sector + XBL_SECTOR_SIZE - 4));

    if (xblExtractAccountFromSector(sector, rec)) {
        char gt[17];
        char xuid[17];
        xblParseRecord(rec, gt, sizeof(gt), xuid, sizeof(xuid));
        *pos += snprintf(text + *pos, textSz - (size_t)*pos,
                         "  account: %s xuid=%s verified=%s\r\n", gt, xuid,
                         xblVerifyAccount(rec) ? "yes" : "no");
    } else {
        *pos += snprintf(text + *pos, textSz - (size_t)*pos,
                         "  bytes@0x0C: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                         sector[0x0C], sector[0x0D], sector[0x0E], sector[0x0F], sector[0x10],
                         sector[0x11], sector[0x12], sector[0x13]);
    }
}

static BOOL xblReadConfigAccounts(XblAccountSet *out)
{
    if (!out) {
        return FALSE;
    }

    memset(out, 0, sizeof(*out));
    out->partition = XBL_CONFIG_DISK_PART;
    out->useConfigSectors = TRUE;

    static unsigned char sector[XBL_SECTOR_SIZE];
    static unsigned char rec[XBL_ACCOUNT_LEN];
    BOOL opened = FALSE;

    for (int slot = 0; slot < XBL_ACCOUNT_MAX; slot++) {
        int secNum = XBL_CONFIG_FIRST_SECTOR + slot;
        if (!xblReadDiskSector(secNum, sector)) {
            continue;
        }
        opened = TRUE;
        if (!xblExtractAccountFromSector(sector, rec)) {
            continue;
        }
        xblFillSlotFromRecord(&out->slots[slot], rec);
        if (out->slots[slot].present) {
            out->presentCount++;
        }
        if (out->slots[slot].verified) {
            out->verifiedCount++;
        }
    }

    if (opened && out->presentCount == 0) {
        for (int secNum = 8; secNum <= 27; secNum++) {
            int slot = secNum - XBL_CONFIG_FIRST_SECTOR;
            if (slot >= 0 && slot < XBL_ACCOUNT_MAX && out->slots[slot].present) {
                continue;
            }
            if (!xblReadDiskSector(secNum, sector)) {
                continue;
            }
            if (!xblExtractAccountFromSector(sector, rec)) {
                continue;
            }
            int dest = slot;
            if (dest < 0 || dest >= XBL_ACCOUNT_MAX) {
                for (dest = 0; dest < XBL_ACCOUNT_MAX; dest++) {
                    if (!out->slots[dest].present) {
                        break;
                    }
                }
                if (dest >= XBL_ACCOUNT_MAX) {
                    continue;
                }
            }
            xblFillSlotFromRecord(&out->slots[dest], rec);
            if (out->slots[dest].present) {
                out->presentCount++;
            }
            if (out->slots[dest].verified) {
                out->verifiedCount++;
            }
        }
    }

    return opened;
}

static BOOL xblSectorIsZero(const unsigned char *sector, unsigned int len)
{
    for (unsigned int i = 0; i < len; i++) {
        if (sector[i] != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL xblWriteConfigAccount(int slot, const unsigned char *record)
{
    if (slot < 0 || slot >= XBL_ACCOUNT_MAX || !record) {
        return FALSE;
    }

    static unsigned char sector[XBL_SECTOR_SIZE];

    /* Safety: never write blind. If we cannot even read the target sector we
     * have no idea what is there, so refuse rather than risk clobbering it. */
    if (!xblReadDiskSector(XBL_CONFIG_FIRST_SECTOR + slot, sector)) {
        return FALSE;
    }

    if (xblRdU32(sector) != XBL_CONFIG_HEADER) {
        /* The sector does not carry our config frame. Only (re)frame it when it
         * is genuinely empty (all zeros). If it holds unknown non-zero data,
         * abort: zeroing+reframing it would destroy whatever lives there (this
         * is the failure mode that can wipe accounts/other config on consoles
         * whose layout differs from what we assume). */
        if (!xblSectorIsZero(sector, XBL_SECTOR_SIZE)) {
            return FALSE;
        }
        memset(sector, 0, sizeof(sector));
        xblWrU32(sector, XBL_CONFIG_HEADER);
        xblWrU32(sector + 4, 1);
        xblWrU32(sector + 8, 1);
        xblWrU32(sector + XBL_SECTOR_SIZE - 4, XBL_CONFIG_FOOTER);
    } else if (xblRecordPresent(sector + XBL_CONFIG_RECORD_OFF) &&
               memcmp(sector + XBL_CONFIG_RECORD_OFF, record, XBL_ACCOUNT_LEN) != 0) {
        /* This tool only ADDS accounts to empty slots. The slot already holds a
         * different account record — refuse, so we can never delete/replace it. */
        return FALSE;
    }

    memcpy(sector + XBL_CONFIG_RECORD_OFF, record, XBL_ACCOUNT_LEN);
    xblWrU32(sector + 504, 0);
    xblWrU32(sector + 504, ~xblConfigChecksum(sector, XBL_SECTOR_SIZE));

    unsigned long long off = (unsigned long long)(XBL_CONFIG_FIRST_SECTOR + slot) * XBL_SECTOR_SIZE;
    return xblWritePartitionBytes(XBL_CONFIG_DISK_PART, off, sector, XBL_SECTOR_SIZE);
}

BOOL xblReadPartition(int partition, XblAccountSet *out)
{
    if (!out || partition < 1) {
        return FALSE;
    }

    static unsigned char header[XBL_SUPERBLOCK_IO];
    if (!xblReadPartitionBytes(partition, 0, header, XBL_SUPERBLOCK_IO, FALSE)) {
        return FALSE;
    }
    if (xblRdU32(header) != XBL_FATX_MAGIC) {
        return FALSE;
    }

    memset(out, 0, sizeof(*out));
    out->partition = partition;
    out->useConfigSectors = FALSE;
    for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
        const unsigned char *rec = header + XBL_ACCOUNT_BASE + i * XBL_ACCOUNT_LEN;
        xblFillSlotFromRecord(&out->slots[i], rec);
        if (out->slots[i].present) {
            out->presentCount++;
        }
        if (out->slots[i].verified) {
            out->verifiedCount++;
        }
    }
    return TRUE;
}

BOOL xblReadAccounts(XblAccountSet *out)
{
    if (!out) {
        return FALSE;
    }

    XblAccountSet cfg;
    BOOL cfgOk = xblReadConfigAccounts(&cfg);
    if (cfgOk && cfg.presentCount > 0) {
        memcpy(out, &cfg, sizeof(*out));
        return TRUE;
    }

    XblAccountSet best;
    memset(&best, 0, sizeof(best));
    BOOL sawFatx = FALSE;

    for (int part = 1; part <= 7; part++) {
        XblAccountSet cur;
        if (!xblReadPartition(part, &cur)) {
            continue;
        }
        sawFatx = TRUE;
        if (cur.presentCount > best.presentCount) {
            best = cur;
        }
    }

    if (best.presentCount > 0) {
        memcpy(out, &best, sizeof(*out));
        return TRUE;
    }

    if (cfgOk) {
        memcpy(out, &cfg, sizeof(*out));
        return TRUE;
    }

    if (sawFatx) {
        return xblReadPartition(1, out);
    }

    return FALSE;
}

BOOL xblWriteAccount(int partition, int slot, const unsigned char *record)
{
    if (slot < 0 || slot >= XBL_ACCOUNT_MAX || !record) {
        return FALSE;
    }

    if (partition == XBL_CONFIG_DISK_PART) {
        return xblWriteConfigAccount(slot, record);
    }

    if (partition < 1) {
        return FALSE;
    }

    static unsigned char header[XBL_SUPERBLOCK_IO];
    if (!xblReadPartitionBytes(partition, 0, header, XBL_SUPERBLOCK_IO, TRUE)) {
        return FALSE;
    }
    if (xblRdU32(header) != XBL_FATX_MAGIC) {
        /* Not a FATX superblock we recognize — never write back 0x400 bytes of
         * data we don't understand; that can brick the whole partition. */
        return FALSE;
    }

    /* Safety: only update an empty slot or one that already holds the same XUID.
     * Refuse to overwrite a different, populated account record. */
    unsigned char *dst = header + XBL_ACCOUNT_BASE + slot * XBL_ACCOUNT_LEN;
    if (xblRecordPresent(dst)) {
        char curXuid[17];
        char curGt[17];
        char newXuid[17];
        char newGt[17];
        xblParseRecord(dst, curGt, sizeof(curGt), curXuid, sizeof(curXuid));
        xblParseRecord(record, newGt, sizeof(newGt), newXuid, sizeof(newXuid));
        if (!xblXuidEqual(curXuid, newXuid)) {
            return FALSE;
        }
    }

    memcpy(dst, record, XBL_ACCOUNT_LEN);
    return xblWritePartitionBytes(partition, 0, header, XBL_SUPERBLOCK_IO);
}

BOOL xblXuidEqual(const char *a, const char *b)
{
    if (!a || !b) {
        return FALSE;
    }
    while (*a && *b) {
        if (*a != *b && (*a ^ *b) != 0x20) {
            return FALSE;
        }
        a++;
        b++;
    }
    return *a == *b;
}

int xblPickRestoreSlot(const XblAccountSet *set, const char *xuidHex)
{
    if (!set) {
        return -1;
    }
    if (xuidHex && xuidHex[0]) {
        for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
            if (set->slots[i].present && xblXuidEqual(set->slots[i].xuidHex, xuidHex)) {
                return i;
            }
        }
    }
    for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
        if (!set->slots[i].present) {
            return i;
        }
    }
    return -1;
}

BOOL xblWriteProbeFile(const char *path, const XblAccountSet *scan)
{
    if (!path || !path[0]) {
        return FALSE;
    }

    static char text[2048];
    int pos = snprintf(text, sizeof(text), "Xbox LIVE account probe\r\n");

    if (scan) {
        pos += snprintf(text + pos, sizeof(text) - pos, "scan_ok=yes present=%d verified=%d part=%d config=%s\r\n",
                        scan->presentCount, scan->verifiedCount, scan->partition,
                        scan->useConfigSectors ? "yes" : "no");
        for (int i = 0; i < XBL_ACCOUNT_MAX; i++) {
            if (scan->slots[i].present) {
                pos += snprintf(text + pos, sizeof(text) - pos, "slot %d: %s (%s)\r\n", i,
                                scan->slots[i].gamertag,
                                scan->slots[i].verified ? "verified" : "unverified");
            }
        }
    } else {
        pos += snprintf(text + pos, sizeof(text) - pos, "scan_ok=no\r\n");
    }

    xblProbeSector(12, text, sizeof(text), &pos);
    xblProbeSector(13, text, sizeof(text), &pos);

    return writeFileBytes(path, text, (DWORD)pos);
}


