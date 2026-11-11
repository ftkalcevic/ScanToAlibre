// ViewProjection.h : the view's projection from world points to screen points,
// fitted from samples, so a click can be turned into a line through the scene.
//
// Alibre's IADAddOnCommandSite::WorldToScreen gives screen points one at a time.
// Fitting a 3 x 4 projection (u, v from x, y, z) to a few of them works for both
// orthographic and perspective views, and checks itself by its residual.

#pragma once

#include <vector>

#include "Geometry.h"
#include "Mesh.h"

namespace scancore
{
    struct ScreenPoint
    {
        double u = 0.0;
        double v = 0.0;
    };

    class ViewProjection
    {
    public:
        // Needs at least six world points not all in one plane, and their screen
        // points. Returns false if the fit can't be made.
        bool Fit(const std::vector<Point3>& world, const std::vector<ScreenPoint>& screen);

        ScreenPoint Project(const Point3& p) const;

        // The largest distance, in screen units, between a sample and its projection.
        double LargestError() const { return m_largestError; }

        // The line of world points that project to (u, v), its direction unit
        // length and pointing into the screen (IntoScreen).
        Ray LineThrough(double u, double v) const;

        // The unit direction into the screen at a world point, for a screen whose
        // u runs right and v runs down (as Windows' does), in right-handed world
        // axes: the direction along which the point's screen position doesn't
        // change, signed so that right x down points along it.
        Point3 IntoScreen(const Point3& at) const;

    private:
        // Fitted in normalised coordinates: world (p - centre) / scale, screen
        // (u - cu) / su and (v - cv) / sv.
        double m_m[3][4] = {};
        Point3 m_centre;
        double m_scale = 1.0;
        double m_cu = 0.0, m_cv = 0.0, m_su = 1.0, m_sv = 1.0;
        double m_largestError = 0.0;
    };

    // Six to eight points around a box (its corners), to sample the projection with.
    std::vector<Point3> ProjectionSamplePoints(const Box3& box);
}
