#pragma once
#include <QtCore>
// Keep complete recent question/answer turns; never cut a message in the middle.
QVariantList recentConversationTurns(const QVariantList &messages,int characterBudget);
