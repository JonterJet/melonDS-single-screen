// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_RECOGNITION_H
#define STUDIO_RECOGNITION_H
#include "StudioDocument.h"
struct StudioMatch
{
    int candidate = -1, state = -1;
    double confidence = 0;
    double scores[StudioStateCount]{};
    bool ambiguous = false;
    int pendingMs = 0;
};
class StudioRecognition
{
public:
    static double similarity(const QImage& frame, const StudioReference& reference);
    StudioMatch update(const QImage (&frames)[2], const StudioDocument& document, qint64 nowMs);
    void reset();
private:
    int pending = -2, stable = -1;
    qint64 since = 0, previous = -1, unstableSince = -1;
};
#endif
