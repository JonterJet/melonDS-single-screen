// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_RENDERING_H
#define STUDIO_RENDERING_H
#include "StudioDocument.h"
#include <QPainter>
#include <array>
struct StudioPresentation
{
    int sizing = -1; // -1 leaves the user's existing emulator layout intact.
    QVector<StudioElement> overlays;
};
std::array<float, 30> studioOverlayVertices(const StudioElement& element);
void paintStudioOverlays(QPainter& painter, const QImage (&screens)[2], const QVector<StudioElement>& elements);
#endif
