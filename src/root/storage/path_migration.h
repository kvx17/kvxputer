#ifndef __KVX_PATH_MIGRATION_H__
#define __KVX_PATH_MIGRATION_H__

/** Run once after LittleFS/SD mount; migrates legacy Bruce paths to three-root layout. */
void kvxRunPathMigration();

#endif
