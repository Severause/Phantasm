// Phantasm - native self-tests: MirrorCameraCore (the live views zoom toward the point under the pointer).
//
// Author: Severause

#include "MirrorCameraCore.h"
#include "TestHarness.h"

#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    namespace
    {
        namespace MC = Phantasm::MirrorCameraCore;

        // Where world point p shows in the view (u, v from the top left) for a camera orbiting target at distance.
        void Project(const MC::Basis& b, MC::V3 target, float distance, MC::V3 p, float fovYDeg, float aspect,
                     float& u, float& v)
        {
            const MC::V3 eye  = MC::Sub(target, MC::Scale(b.forward, distance));
            const MC::V3 rel  = MC::Sub(p, eye);
            const float  z    = MC::Dot(rel, b.forward);
            const float  tanH = std::tan(fovYDeg * 0.5f * 3.14159265358979f / 180.0f);
            u = (MC::Dot(rel, b.right) / (z * tanH * aspect) + 1.0f) * 0.5f;
            v = (1.0f - MC::Dot(rel, b.up) / (z * tanH)) * 0.5f;
        }

        bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
    }

    void RunMirrorCameraCoreTests()
    {
        Check(Near(MC::NextZoom(1.0f, 0.0f), 1.0f) && MC::NextZoom(1.0f, -100000.0f) == MC::kZoomMin &&
                  MC::NextZoom(1.0f, 100000.0f) == MC::kZoomMax,
              "mirror camera: the zoom stays in range");

        const MC::Basis b = MC::OrbitBasis(30.0f, 20.0f);
        Check(Near(MC::Dot(b.forward, b.right), 0.0f) && Near(MC::Dot(b.forward, b.up), 0.0f) &&
                  Near(MC::Dot(b.right, b.up), 0.0f) && b.up.z > 0.0f,
              "mirror camera: the orbit basis is orthogonal and upright");

        // Zooming in keeps the point under the pointer where it is.
        const MC::V3 centre{ 10.0f, -20.0f, 100.0f };
        const float  fov = 35.0f, aspect = 0.6f, base = 280.0f, z0 = 1.0f;
        const float  z1     = MC::NextZoom(z0, -300.0f);
        const MC::V3 offset = MC::PointerOffset(b, 0.7f, 0.2f, fov, aspect, base * z0);
        const MC::V3 point  = MC::Add(centre, offset);
        const MC::V3 pan    = MC::PanAfterZoom({}, offset, z0, z1, 1000.0f);
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        Project(b, centre, base * z0, point, fov, aspect, u0, v0);
        Project(b, MC::Add(centre, pan), base * z1, point, fov, aspect, u1, v1);
        Check(Near(u0, 0.7f) && Near(v0, 0.2f) && Near(u1, 0.7f) && Near(v1, 0.2f),
              "mirror camera: zooming in keeps the point under the pointer still");

        const MC::V3 still = MC::PanAfterZoom({}, MC::PointerOffset(b, 0.5f, 0.5f, fov, aspect, base), 1.0f, 0.6f, 1000.0f);
        Check(Near(MC::Length(still), 0.0f), "mirror camera: zooming at the middle does not pan");

        const MC::V3 half = MC::PanAfterZoom({ 40.0f, 0.0f, 0.0f }, {}, 0.5f, 0.75f, 1000.0f);
        const MC::V3 home = MC::PanAfterZoom({ 40.0f, 0.0f, 0.0f }, {}, 0.5f, 1.2f, 1000.0f);
        Check(Near(half.x, 20.0f) && Near(MC::Length(home), 0.0f),
              "mirror camera: zooming out eases the pan back, centred at the default framing");

        const MC::V3 capped = MC::PanAfterZoom({}, { 500.0f, 0.0f, 0.0f }, 1.0f, 0.45f, 160.0f);
        Check(Near(MC::Length(capped), 160.0f), "mirror camera: the pan is capped");

        // Non-finite input never reaches the camera.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        Check(MC::NextZoom(1.3f, nan) == 1.3f && MC::NextZoom(1.3f, inf) == 1.3f && MC::NextZoom(1.3f, -inf) == 1.3f,
              "mirror camera: a non-finite wheel step leaves the zoom");
        {
            float yaw = 30.0f, pitch = 10.0f;
            Check(!MC::Drag(yaw, pitch, nan, 0.0f) && !MC::Drag(yaw, pitch, 0.0f, inf) && yaw == 30.0f && pitch == 10.0f,
                  "mirror camera: a non-finite drag leaves the orbit");
            Check(MC::Drag(yaw, pitch, 20.0f, 20.0f) && Near(yaw, 10.0f) && Near(pitch, 20.0f),
                  "mirror camera: a drag turns 1 degree and tilts half a degree a pixel");
            Check(MC::Drag(yaw, pitch, 0.0f, 1000.0f) && pitch == 80.0f, "mirror camera: the pitch is clamped");
        }
        {
            float yaw = 170.0f, pitch = 0.0f;
            MC::Drag(yaw, pitch, -20.0f, 0.0f);
            Check(Near(yaw, -170.0f), "mirror camera: the yaw wraps at 180");
            Check(Near(MC::YawTurnDeg(170.0f, yaw), 20.0f), "mirror camera: a turn across the wrap is the short way");
            for (int i = 0; i < 100000; ++i) MC::Drag(yaw, pitch, 1.0e6f, 0.0f);
            Check(std::isfinite(yaw) && yaw >= -180.0f && yaw <= 180.0f, "mirror camera: a huge drag keeps the yaw in range");
        }
        {
            Check(Near(MC::FacingYawDeg(0.0f), 180.0f), "mirror camera: a figure facing north is seen from the north");
            bool facing = true;
            for (const float h : { 0.0f, 0.7f, 1.5707963f, 3.0f, -2.2f, 5.9f }) {
                const MC::Basis fb = MC::OrbitBasis(MC::FacingYawDeg(h), 0.0f);
                // The camera sits at -forward from the target: in front of the figure, where it faces.
                facing = facing && Near(-fb.forward.x, std::sin(h)) && Near(-fb.forward.y, std::cos(h)) &&
                         Near(fb.forward.z, 0.0f);
            }
            Check(facing, "mirror camera: the facing yaw puts the camera in front of the figure at any heading");
        }
        {
            const MC::V3 mid = MC::PointerOffset(b, nan, inf, fov, aspect, base);
            Check(Near(MC::Length(mid), 0.0f), "mirror camera: a non-finite pointer zooms at the middle");
            const MC::V3 kept = MC::PanAfterZoom({ 5.0f, 6.0f, 7.0f }, { nan, 0.0f, 0.0f }, 1.0f, 0.5f, 160.0f);
            Check(kept.x == 5.0f && kept.y == 6.0f && kept.z == 7.0f, "mirror camera: a non-finite pan keeps the old one");
        }

        // The picture is the game's, not its mirror image.
        {
            const MC::Basis north = MC::OrbitBasis(0.0f, 0.0f);   // the camera south of the target, looking north
            Check(Near(north.forward.y, 1.0f) && Near(north.right.x, 1.0f) && Near(north.up.z, 1.0f),
                  "mirror camera: looking north, east is on the picture's right");
            const MC::V3 east = MC::PointerOffset(north, 0.9f, 0.5f, fov, aspect, base);
            Check(east.x > 0.0f && Near(east.y, 0.0f), "mirror camera: zooming at the picture's right goes east");

            // A triangle whose right-hand-rule normal faces the camera runs counterclockwise on the image.
            bool winding = true;
            const MC::V3 tris[3][3] = { { { 0, 0, 0 }, { 30, 5, 0 }, { 4, -6, 40 } },
                                        { { -10, 3, 12 }, { 8, -20, 15 }, { 2, 9, -25 } },
                                        { { 5, 5, 5 }, { -15, 12, 30 }, { 20, 18, -8 } } };
            for (const float yaw : { 0.0f, 37.0f, 120.0f, -75.0f, 180.0f }) {
                for (const float pitch : { 0.0f, 25.0f, -40.0f }) {
                    const MC::Basis tb = MC::OrbitBasis(yaw, pitch);
                    for (const auto& t : tris) {
                        const MC::V3 e1 = MC::Sub(t[1], t[0]), e2 = MC::Sub(t[2], t[0]);
                        const MC::V3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
                        const bool   front = MC::Dot(n, tb.forward) < 0.0f;   // the normal points back at the camera
                        float        u[3], v[3];
                        for (int i = 0; i < 3; ++i) Project(tb, centre, 600.0f, MC::Add(centre, t[i]), fov, aspect, u[i], v[i]);
                        // Signed area with y up (v runs down the image): positive is counterclockwise as seen.
                        const float area = (u[1] - u[0]) * (v[0] - v[2]) - (u[2] - u[0]) * (v[0] - v[1]);
                        winding = winding && ((area > 0.0f) == (front == MC::kFrontCounterClockwise));
                    }
                }
            }
            Check(winding, "mirror camera: a front face runs counterclockwise on the image");

            // A drag right turns the figure's front to the right, with the pointer.
            float yaw = 20.0f, pitch = 0.0f;
            const MC::Basis before = MC::OrbitBasis(yaw, pitch);
            const MC::V3    front  = MC::Sub(centre, MC::Scale(before.forward, 30.0f));
            MC::Drag(yaw, pitch, 15.0f, 0.0f);
            float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
            Project(before, centre, base, front, fov, aspect, u0, v0);
            Project(MC::OrbitBasis(yaw, pitch), centre, base, front, fov, aspect, u1, v1);
            Check(Near(u0, 0.5f) && u1 > 0.55f, "mirror camera: a drag right turns the figure with the pointer");
        }
    }
}
