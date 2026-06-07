#ifndef SAVE_DATES_H
#define SAVE_DATES_H

#include <windows.h>

/* Persistent "true modification date" tracker.
 *
 * Filesystem last-write times are NOT reliable for deciding which copy of a
 * save is newer: moving/copying a save folder (FTP, swapping HDDs, restoring
 * with another tool, etc.) makes FATX bump every timestamp to "now", so an old
 * save suddenly looks newer than the cloud copy and the date-based sync refuses
 * to pull the genuinely-correct version.
 *
 * The content fingerprint, on the other hand, does NOT change when files are
 * merely moved. So we record (titleId, profile) -> (fingerprint, modUnix) the
 * first time we see/upload a save, in a small sidecar file. On later runs, if
 * the fingerprint still matches, we report the recorded date instead of the
 * (possibly inflated) filesystem date. Only a real content change (new
 * fingerprint) is allowed to advance the date. */

/* Load the tracker from disk (call once at startup). Missing file is fine. */
void saveDatesLoad(const char *path);

/* If we have a recorded date for this (titleId, profile) AND the stored
 * fingerprint matches the supplied one (i.e. the content has NOT changed since
 * we last recorded it), return that recorded Unix time. Otherwise return 0,
 * meaning "no trustworthy stored date — fall back to the filesystem mtime". */
unsigned long long saveDatesLookup(const char *titleId, const char *profile,
                                   const char *fingerprint);

/* Upsert the record for (titleId, profile). Stores the fingerprint and date.
 * Use the true date you want pinned to this content (e.g. the cloud's
 * first-upload date when known, or the filesystem mtime for genuinely new or
 * changed content). */
void saveDatesRecord(const char *titleId, const char *profile, const char *fingerprint,
                     unsigned long long modUnix);

/* Persist all records back to disk. Returns TRUE on success. */
BOOL saveDatesFlush(const char *path);

#endif
