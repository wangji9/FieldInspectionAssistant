#pragma once

#include <QVariantMap>

// Only metrics derived from supported raw exports or the local capture catalog.
QVariantMap projectEvidenceMetrics(const QString &kind, const QString &archivedPath,
                                   const QString &originalPath, const QString &recordingCatalog);
