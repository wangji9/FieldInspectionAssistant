#pragma once
#include <QtCore>
namespace GraphExtraction {
QVariantMap validate(const QVariantMap &output,const QString &source,const QVariantMap &ontology,bool evolve);
QVariantMap extend(const QVariantMap &ontology,const QVariantList &relations);
QVariantMap parse(const QString &answer);
}
