#pragma once

#include <Arduino.h>

// Normalize for search: lowercase; '-', '_', and space treated as the same.
String qsNormalize(const String &s);

// Rank a catalog/file candidate against a normalized query.
// Higher is better. Returns 0 when there is no match.
// exact label/id=300, prefix=200, substring label/id=100, group/alias=50.
int qsScoreMatch(
    const String &normQuery, const String &label, const String &id, const String &group = String()
);

// True if normQuery matches any static alias that maps to id or group.
bool qsAliasHits(const String &normQuery, const String &id, const String &group);
