// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioRecognition.h"
#include <algorithm>
#include <cmath>

// Fixed-location template comparison, sampled on a 32x24 grid. Confidence is
// normalized RGB similarity, not a calibrated probability or an AI prediction.
double StudioRecognition::similarity(const QImage& frame, const StudioReference& r)
{
    if (frame.size() != QSize(256, 192) || r.image.size() != r.region.size()
        || !frame.rect().contains(r.region) || r.region.isEmpty()) return 0;
    qint64 error = 0;
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x)
    {
        int rx = std::min(r.region.width() - 1, (2*x + 1)*r.region.width()/64);
        int ry = std::min(r.region.height() - 1, (2*y + 1)*r.region.height()/48);
        QRgb a = frame.pixel(r.region.x() + rx, r.region.y() + ry), b = r.image.pixel(rx, ry);
        error += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
    }
    return 1.0 - double(error)/(32*24*3*255);
}
void StudioRecognition::reset() { pending = -2; stable = -1; previous = -1; since = 0; unstableSince = -1; }
StudioMatch StudioRecognition::update(const QImage (&frames)[2], const StudioDocument& d, qint64 nowMs)
{
    StudioMatch m;
    double winner = -1, runner = -1;
    for (int state = StudioMarioFirst; state < StudioStateCount; ++state)
    {
        double accepted = -1;
        for (const auto& r : d.references[state]) if (r.enabled)
        {
            if (r.screen < 0 || r.screen > 1 || frames[r.screen].size() != QSize(256, 192)
                || r.image.size() != r.region.size() || !frames[r.screen].rect().contains(r.region)) continue;
            double score = similarity(frames[r.screen], r);
            m.scores[state] = std::max(m.scores[state], score);
            if (!frames[r.screen].isNull() && score >= r.threshold) accepted = std::max(accepted, score);
        }
        if (accepted > winner) { runner = winner; winner = accepted; m.candidate = state; }
        else runner = std::max(runner, accepted);
        m.confidence = std::max(m.confidence, m.scores[state]);
    }
    if (winner < 0) m.candidate = -1;
    else if (runner >= 0 && winner - runner <= d.ambiguityMargin)
    { m.candidate = -1; m.ambiguous = true; }
    // Missing/stale samples must not count towards continuous confirmation.
    if (previous >= 0 && (nowMs < previous || nowMs - previous > 500)) { pending = -2; stable = -1; unstableSince = -1; }
    if (m.candidate != pending) { pending = m.candidate; since = nowMs; }
    m.pendingMs = int(std::max<qint64>(0, nowMs - since));
    if (stable >= 0 && m.candidate != stable)
    {
        if (unstableSince < 0) unstableSince = nowMs;
        if (nowMs - unstableSince >= d.confirmationMs) stable = -1;
    }
    else unstableSince = -1;
    if (m.pendingMs >= d.confirmationMs) { stable = pending; unstableSince = -1; }
    m.state = stable;
    previous = nowMs;
    return m;
}
