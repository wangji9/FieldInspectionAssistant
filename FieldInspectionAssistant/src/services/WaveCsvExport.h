#pragma once
#include <QtCore>
#include <functional>
// Streams raw WAV samples to CSV; never substitutes the display ring for a full recording.
QVariantMap exportWaveCsv(const QString &source,const QString &target,const QString &scope,
                         double windowSeconds,qint64 pinnedFrames,const QVariantMap &provenance,
                         const std::function<bool()> &cancelled);
