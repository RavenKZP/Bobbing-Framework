#pragma once

#define M_PI 3.14159265358979323846f

using BoundaryBox = std::array<RE::NiPoint3, 8>;

namespace Utils {
    std::string FormIDToString(RE::FormID formID);
    RE::FormID ParseForm(const std::string& str);

    RE::NiPoint3 GetPosition(const RE::TESObjectREFR* obj);
    BoundaryBox GetBoundingBox(const RE::TESObjectREFR* a_obj);

    float RandomFloat(float min, float max);
}

namespace Noise {
    inline float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

    inline float Lerp(float a, float b, float t) { return a + t * (b - a); }

    inline float Hash(int x) {
        x = (x << 13) ^ x;
        return (1.0f - ((x * (x * x * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }

    inline float Perlin1D(float x) {
        int x0 = (int)std::floor(x);
        int x1 = x0 + 1;

        float t = x - (float)x0;

        float n0 = Hash(x0);
        float n1 = Hash(x1);

        float u = Fade(t);

        return Lerp(n0, n1, u);  // range ~ [-1,1]
    }
}

namespace Math {

    float Round(float value, int n);
    float Ceil(float value, int n);

    namespace LinAlg {
        namespace R3 {
            void rotateX(RE::NiPoint3& v, float angle);

            // Function to rotate a vector around the y-axis
            void rotateY(RE::NiPoint3& v, float angle);

            // Function to rotate a vector around the z-axis
            void rotateZ(RE::NiPoint3& v, float angle);

            void rotate(RE::NiPoint3& v, float angleX, float angleY, float angleZ);
        };

        class Geometry {
            std::vector<RE::NiPoint3> positions;
            std::vector<uint16_t> indexes;
            const RE::TESObjectREFR* obj;

            void FetchVertices(const RE::BSGeometry* o3d, RE::BSGraphics::TriShape* triShape);

        public:
            static RE::NiPoint3 Rotate(const RE::NiPoint3& A, const RE::NiPoint3& angles);
            static RE::NiPoint3 Rotate(const RE::NiPoint3& A, const RE::NiMatrix3& angles);

            ~Geometry() = default;
            explicit Geometry(const RE::TESObjectREFR* obj);
            [[nodiscard]] std::pair<RE::NiPoint3, RE::NiPoint3> GetBoundingBox() const;
        };

        std::array<RE::NiPoint3, 3> GetClosest3Vertices(const std::array<RE::NiPoint3, 8>& a_bounding_box,
                                                        const RE::NiPoint3& outside_point);
        std::array<RE::NiPoint3, 3> GetClosest3Vertices(const std::array<RE::NiPoint3, 4>& a_bounded_plane,
                                                        const RE::NiPoint3& outside_point);
        RE::NiPoint3 CalculateNormalOfPlane(const RE::NiPoint3& span1, const RE::NiPoint3& span2);
        RE::NiPoint3 closestPointOnPlane(const RE::NiPoint3& a_point_on_plane, const RE::NiPoint3& a_point_not_on_plane,
                                         const RE::NiPoint3& v_normal);
        RE::NiPoint3 intersectLine(const std::array<RE::NiPoint3, 3>& vertices,
                                   const RE::NiPoint3& outside_plane_point);
    };
};