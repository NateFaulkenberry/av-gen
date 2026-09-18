// Phase 0 Embree spike: validate Embree 4 on Apple Silicon (arm64 / NEON) OUTSIDE av-gen.
// Every arm has a control (ADR-182). Prints PASS/FAIL per check and writes a PPM image.
#include <embree4/rtcore.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok, const std::string& detail = {}) {
    (ok ? g_pass : g_fail)++;
    std::printf("%-46s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.empty() ? "" : "  ", detail.c_str());
}

struct Vec3 { float x, y, z; };
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec3 norm(Vec3 a) { float l = std::sqrt(dot(a,a)); return {a.x/l, a.y/l, a.z/l}; }

int main() {
    std::printf("=== Embree spike: Apple Silicon validation ===\n");

    RTCDevice device = rtcNewDevice("verbose=0");
    if (!device) { std::printf("FATAL: rtcNewDevice returned null\n"); return 1; }
    check("rtcNewDevice", true);

    // Capability probe: what ISA did Embree actually select on this machine?
    const int neon = rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_NATIVE_RAY4_SUPPORTED);
    const int ver  = (int)rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_VERSION);
    std::printf("  embree version=%d  native_ray4=%d\n", ver, neon);
    check("embree version >= 40000", ver >= 40000, "version=" + std::to_string(ver));

    RTCScene scene = rtcNewScene(device);
    RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);

    // Two triangles in the z = -2 and z = -5 planes, both covering the view centre.
    // The near one MUST win: that is the control for "the BVH actually sorts by depth".
    auto* verts = (float*)rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0,
                      RTC_FORMAT_FLOAT3, 3 * sizeof(float), 6);
    const float v[6][3] = {
        {-1,-1,-2}, {1,-1,-2}, {0,1,-2},      // near triangle (geom-local 0,1,2)
        {-6,-3,-5}, {6,-3,-5}, {0,5,-5},      // far triangle  (3,4,5), deliberately much larger
    };
    for (int i = 0; i < 6; ++i) for (int c = 0; c < 3; ++c) verts[i*3+c] = v[i][c];

    auto* idx = (unsigned*)rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0,
                    RTC_FORMAT_UINT3, 3 * sizeof(unsigned), 2);
    idx[0]=0; idx[1]=1; idx[2]=2;
    idx[3]=3; idx[4]=4; idx[5]=5;

    rtcCommitGeometry(geom);
    const unsigned geomId = rtcAttachGeometry(scene, geom);
    rtcReleaseGeometry(geom);
    rtcCommitScene(scene);
    check("rtcCommitScene (BVH build)", true, "geomId=" + std::to_string(geomId));

    auto trace = [&](Vec3 o, Vec3 d) {
        RTCRayHit rh{};
        rh.ray.org_x=o.x; rh.ray.org_y=o.y; rh.ray.org_z=o.z;
        rh.ray.dir_x=d.x; rh.ray.dir_y=d.y; rh.ray.dir_z=d.z;
        rh.ray.tnear=0.0f; rh.ray.tfar=1e30f; rh.ray.mask=-1;
        rh.hit.geomID=RTC_INVALID_GEOMETRY_ID; rh.hit.primID=RTC_INVALID_GEOMETRY_ID;
        rtcIntersect1(scene, &rh);
        return rh;
    };

    // ARM: ray down -Z through the centre. Expect HIT, prim 0 (the NEAR triangle), t == 2.
    {
        auto rh = trace({0,0,0}, {0,0,-1});
        check("centre ray hits", rh.hit.geomID != RTC_INVALID_GEOMETRY_ID);
        check("centre ray picks NEAR triangle (primID 0)", rh.hit.primID == 0,
              "primID=" + std::to_string((int)rh.hit.primID));
        check("centre ray t == 2.0", std::fabs(rh.ray.tfar - 2.0f) < 1e-5f,
              "t=" + std::to_string(rh.ray.tfar));
        // Geometric normal should point back at the camera along +Z.
        Vec3 ng = norm({rh.hit.Ng_x, rh.hit.Ng_y, rh.hit.Ng_z});
        check("geometric normal is axis-aligned +/-Z", std::fabs(std::fabs(ng.z) - 1.0f) < 1e-5f,
              "Ng=(" + std::to_string(ng.x) + "," + std::to_string(ng.y) + "," + std::to_string(ng.z) + ")");
    }

    // CONTROL 1: same origin, ray pointing the OTHER way. Must MISS.
    {
        auto rh = trace({0,0,0}, {0,0,1});
        check("CONTROL backward ray misses", rh.hit.geomID == RTC_INVALID_GEOMETRY_ID,
              "geomID=" + std::to_string((int)rh.hit.geomID));
    }

    // CONTROL 2: offset ray that clears the near triangle but hits the larger far one.
    // Proves prim 1 is reachable at all, i.e. the near-triangle result above was a real choice.
    {
        auto rh = trace({0,2.5f,0}, {0,0,-1});
        check("CONTROL offset ray reaches FAR triangle (primID 1)",
              rh.hit.geomID != RTC_INVALID_GEOMETRY_ID && rh.hit.primID == 1,
              "primID=" + std::to_string((int)rh.hit.primID));
    }

    // CONTROL 3: ray far outside both triangles. Must MISS.
    {
        auto rh = trace({10,10,0}, {0,0,-1});
        check("CONTROL far-outside ray misses", rh.hit.geomID == RTC_INVALID_GEOMETRY_ID);
    }

    // Barycentrics: a ray aimed at the near triangle's vertex 0 should give (u,v) ~ (0,0).
    {
        auto rh = trace({-1,-1,0}, {0,0,-1});
        bool hit = rh.hit.geomID != RTC_INVALID_GEOMETRY_ID;
        check("vertex-0 ray barycentric u,v ~ (0,0)",
              hit && rh.hit.u < 1e-3f && rh.hit.v < 1e-3f,
              hit ? "u=" + std::to_string(rh.hit.u) + " v=" + std::to_string(rh.hit.v) : "MISS");
    }

    // Occlusion query (shadow ray) + its control.
    {
        RTCRay r{}; r.org_x=0; r.org_y=0; r.org_z=0; r.dir_x=0; r.dir_y=0; r.dir_z=-1;
        r.tnear=0; r.tfar=1e30f; r.mask=-1;
        rtcOccluded1(scene, &r);
        check("rtcOccluded1 reports occluded", r.tfar < 0.0f);

        RTCRay r2{}; r2.org_x=0; r2.org_y=0; r2.org_z=0; r2.dir_x=0; r2.dir_y=0; r2.dir_z=-1;
        r2.tnear=0; r2.tfar=1.0f; r2.mask=-1;   // tfar stops SHORT of the triangle at z=-2
        rtcOccluded1(scene, &r2);
        check("CONTROL short shadow ray NOT occluded", r2.tfar >= 0.0f);
    }

    // Render an image so there is a visual artifact, not just numbers.
    const int W = 320, H = 200;
    std::vector<unsigned char> px(W * H * 3, 0);
    int hits = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float sx = (2.0f * (x + 0.5f) / W - 1.0f) * (float(W) / H);
            const float sy = 1.0f - 2.0f * (y + 0.5f) / H;
            auto rh = trace({0, 0, 1}, norm({sx, sy, -2.0f}));
            unsigned char* p = &px[(y * W + x) * 3];
            if (rh.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
                ++hits;
                Vec3 ng = norm({rh.hit.Ng_x, rh.hit.Ng_y, rh.hit.Ng_z});
                const float shade = std::fabs(ng.z);
                if (rh.hit.primID == 0) { p[0] = (unsigned char)(230 * shade); p[1] = (unsigned char)(90 * shade); p[2] = 40; }
                else                    { p[0] = 30; p[1] = (unsigned char)(120 * shade); p[2] = (unsigned char)(220 * shade); }
            } else { p[0] = 12; p[1] = 12; p[2] = 18; }
        }
    }
    const double cov = 100.0 * hits / (W * H);
    // Bands, not floors (ADR-182): the two triangles cover a predictable slice of this frame.
    check("image coverage in band 40%..85%", cov > 40.0 && cov < 85.0,
          "coverage=" + std::to_string(cov) + "%");

    // A black frame must not read as a pass.
    long sum = 0; for (auto c : px) sum += c;
    check("image is not black", sum > 0, "sum=" + std::to_string(sum));

    // Visual control: the numbers above passed in a version of this scene where the far
    // triangle was exactly occluded and never appeared. Count both, and require both.
    int nearPx = 0, farPx = 0;
    for (size_t i = 0; i < px.size(); i += 3) {
        if (px[i] > 100 && px[i+2] < 100) ++nearPx;        // orange
        else if (px[i+2] > 100 && px[i] < 100) ++farPx;    // blue
    }
    check("BOTH triangles visible in the frame", nearPx > 500 && farPx > 500,
          "near=" + std::to_string(nearPx) + "px far=" + std::to_string(farPx) + "px");

    FILE* f = std::fopen("spike.ppm", "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
    std::printf("  wrote spike.ppm (%dx%d), coverage %.2f%%\n", W, H, cov);

    rtcReleaseScene(scene);
    rtcReleaseDevice(device);

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
