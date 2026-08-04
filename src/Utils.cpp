#include "Utils.h"
#include "Hooks.h"

#include <tgmath.h>
#include <shared_mutex>

namespace Math {
    float Round(const float value, const int n) {
        const float factor = std::powf(10.0f, n);
        return std::round(value * factor) / factor;
    }

    float Ceil(const float value, const int n) {
        const float factor = std::powf(10.0f, n);
        return std::ceil(value * factor) / factor;
    }

    std::array<RE::NiPoint3, 3> LinAlg::GetClosest3Vertices(const std::array<RE::NiPoint3, 8>& a_bounding_box,
                                                            const RE::NiPoint3& outside_point) {
        std::array<RE::NiPoint3, 3> result;

        std::vector<std::pair<float, size_t>> distances;
        for (size_t i = 0; i < a_bounding_box.size(); ++i) {
            float dist = outside_point.GetDistance(a_bounding_box[i]);
            distances.emplace_back(dist, i);
        }
        std::ranges::sort(distances);
        for (size_t i = 0; i < 3 && i < distances.size(); ++i) {
            const size_t index = distances[i].second;
            result[i] = a_bounding_box[index];
        }

        return result;
    }

    std::array<RE::NiPoint3, 3> LinAlg::GetClosest3Vertices(const std::array<RE::NiPoint3, 4>& a_bounded_plane,
                                                            const RE::NiPoint3& outside_point) {
        std::array<RE::NiPoint3, 3> result;

        std::vector<std::pair<float, size_t>> distances;
        for (size_t i = 0; i < a_bounded_plane.size(); ++i) {
            float dist = outside_point.GetDistance(a_bounded_plane[i]);
            distances.emplace_back(dist, i);
        }
        std::ranges::sort(distances);
        for (size_t i = 0; i < 3 && i < distances.size(); ++i) {
            const size_t index = distances[i].second;
            result[i] = a_bounded_plane[index];
        }

        return result;
    }

    RE::NiPoint3 LinAlg::CalculateNormalOfPlane(const RE::NiPoint3& span1, const RE::NiPoint3& span2) {
        const auto crossed = span1.Cross(span2);
        const auto length = crossed.Length();
        constexpr float EPSILON = 1e-10f;
        if (fabs(length) < EPSILON) {
            return {0, 0, 0};
        }
        return crossed / length;
    }

    RE::NiPoint3 LinAlg::closestPointOnPlane(const RE::NiPoint3& a_point_on_plane,
                                             const RE::NiPoint3& a_point_not_on_plane, const RE::NiPoint3& v_normal) {
        const auto distance = (a_point_not_on_plane - a_point_on_plane).Dot(v_normal);
        return a_point_not_on_plane - v_normal * distance;
    }

    RE::NiPoint3 LinAlg::intersectLine(const std::array<RE::NiPoint3, 3>& vertices,
                                       const RE::NiPoint3& outside_plane_point) {
        const RE::NiPoint3 edge0 = vertices[1] - vertices[0];  // AtoB
        const RE::NiPoint3 edge1 = vertices[2] - vertices[1];  // BtoC
        const RE::NiPoint3 edge2 = vertices[0] - vertices[2];  // CtoA

        const float mags[3] = {edge0.Length(), edge1.Length(), edge2.Length()};

        size_t maxIndex = 0;
        if (mags[1] > mags[maxIndex]) maxIndex = 1;
        if (mags[2] > mags[maxIndex]) maxIndex = 2;

        //[[maybe_unused]] const auto& hypotenuse = edges[index];
        const auto index1 = (maxIndex + 1) % 3;
        const auto index2 = (maxIndex + 2) % 3;

        const auto& orthogonal_vertex = vertices[index2];  // B

        for (const auto a_index : {maxIndex, index1}) {
            const auto& hypotenuse_vertex = vertices[a_index];  // C or A depending on closed loop orientation

            const auto temp = orthogonal_vertex - hypotenuse_vertex;
            const auto temp_length = temp.Length();
            if (temp_length == 0.f) continue;
            const auto temp_unit = temp / temp_length;

            const auto& other_hypotenuse_vertex = a_index == index1 ? vertices[maxIndex] : vertices[index1];
            const auto temp2 = other_hypotenuse_vertex - orthogonal_vertex;
            const auto temp2_length = temp2.Length();
            if (temp2_length == 0.f) continue;
            const auto temp2_unit = temp2 / temp2_length;

            const auto theta_max = atan(temp2_length / temp_length);
            const auto distance_vector = outside_plane_point - hypotenuse_vertex;
            const auto distance_vector_length = distance_vector.Length();
            const auto distance_vector_unit = distance_vector / distance_vector_length;

            if (const auto theta = acos(distance_vector_unit.Dot(temp_unit)); 0.f <= theta && theta <= theta_max) {
                if (temp2_unit.Dot(distance_vector_unit) > 0) {
                    const auto a_span_size = tan(theta) * temp_length;
                    if (const auto intersect = temp + temp2_unit * a_span_size;
                        intersect.Length() > distance_vector_length) {
                        return outside_plane_point;  // it is inside the triangle
                    }
                    const auto normal_distance = (outside_plane_point - orthogonal_vertex).Dot(temp2_unit);
                    return temp2_unit * normal_distance + orthogonal_vertex;
                }
            }
        }

        return orthogonal_vertex;
    }

    UINT GetBufferLength(RE::ID3D11Buffer* reBuffer) {
        const auto buffer = reinterpret_cast<ID3D11Buffer*>(reBuffer);
        D3D11_BUFFER_DESC bufferDesc = {};
        buffer->GetDesc(&bufferDesc);
        return bufferDesc.ByteWidth;
    }

    void EachGeometry(const RE::TESObjectREFR* obj,
                      const std::function<void(RE::BSGeometry* o3d, RE::BSGraphics::TriShape*)>& callback) {
        if (!obj) {
            return;
        }
        if (const auto d3d = obj->Get3D()) {
            RE::BSVisit::TraverseScenegraphGeometries(d3d,
                                                      [&](RE::BSGeometry* a_geometry) -> RE::BSVisit::BSVisitControl {
                                                          const auto& model = a_geometry->GetGeometryRuntimeData();

                                                          if (const auto triShape = model.rendererData) {
                                                              callback(a_geometry, triShape);
                                                          }

                                                          return RE::BSVisit::BSVisitControl::kContinue;
                                                      });
        }
    }

    void LinAlg::Geometry::FetchVertices(const RE::BSGeometry* o3d, RE::BSGraphics::TriShape* triShape) {
        if (const uint8_t* vertexData = triShape->rawVertexData) {
            const uint32_t stride = triShape->vertexDesc.GetSize();
            const auto numPoints = GetBufferLength(triShape->vertexBuffer);
            const auto numPositions = numPoints / stride;
            positions.reserve(positions.size() + numPositions);
            for (uint32_t i = 0; i < numPoints; i += stride) {
                const uint8_t* currentVertex = vertexData + i;

                const auto position =
                    reinterpret_cast<const float*>(currentVertex + triShape->vertexDesc.GetAttributeOffset(
                                                                       RE::BSGraphics::Vertex::Attribute::VA_POSITION));

                auto pos = RE::NiPoint3{position[0], position[1], position[2]};
                pos = o3d->local * pos;
                positions.push_back(pos);
            }
        }
    }

    RE::NiPoint3 LinAlg::Geometry::Rotate(const RE::NiPoint3& A, const RE::NiPoint3& angles) {
        RE::NiMatrix3 R;
        R.SetEulerAnglesXYZ(angles);
        return R * A;
    }

    RE::NiPoint3 LinAlg::Geometry::Rotate(const RE::NiPoint3& A, const RE::NiMatrix3& angles) {
        return angles * A;
    }

    LinAlg::Geometry::Geometry(const RE::TESObjectREFR* obj) {
        this->obj = obj;
        EachGeometry(obj, [this](const RE::BSGeometry* o3d, RE::BSGraphics::TriShape* triShape) -> void {
            FetchVertices(o3d, triShape);
            // FetchIndexes(triShape);
        });

        if (positions.empty()) {
            auto from = obj->GetBoundMin();
            auto to = obj->GetBoundMax();

            if ((to - from).Length() < 1) {
                from = {-5, -5, -5};
                to = {5, 5, 5};
            }
            positions.emplace_back(from.x, from.y, from.z);
            positions.emplace_back(to.x, from.y, from.z);
            positions.emplace_back(to.x, to.y, from.z);
            positions.emplace_back(from.x, to.y, from.z);

            positions.emplace_back(from.x, from.y, to.z);
            positions.emplace_back(to.x, from.y, to.z);
            positions.emplace_back(to.x, to.y, to.z);
            positions.emplace_back(from.x, to.y, to.z);
        }
    }

    std::pair<RE::NiPoint3, RE::NiPoint3> LinAlg::Geometry::GetBoundingBox() const {
        auto min = RE::NiPoint3{0, 0, 0};
        auto max = RE::NiPoint3{0, 0, 0};

        for (auto i = 0; i < positions.size(); i++) {
            // const auto p1 = Rotate(positions[i] * scale, angle);
            const auto p1 = positions[i] * obj->GetScale();

            if (p1.x < min.x) {
                min.x = p1.x;
            }
            if (p1.x > max.x) {
                max.x = p1.x;
            }
            if (p1.y < min.y) {
                min.y = p1.y;
            }
            if (p1.y > max.y) {
                max.y = p1.y;
            }
            if (p1.z < min.z) {
                min.z = p1.z;
            }
            if (p1.z > max.z) {
                max.z = p1.z;
            }
        }

        return std::pair(min, max);
    }
}  // namespace Math

namespace Utils {

    std::string FormIDToString(RE::FormID formID) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return "0x0~Unknown";
        }

        uint8_t highByte = (formID >> 24) & 0xFF;

        if (highByte == 0xFE) {  // ESL
            uint16_t eslIndex = (formID >> 12) & 0xFFF;
            uint16_t localID = formID & 0xFFF;

            for (auto* file : dataHandler->files) {
                if (file && file->IsLight() && file->smallFileCompileIndex == eslIndex) {
                    return std::format("0x{:03X}~{}", localID, file->fileName);
                }
            }
        } else {  // regular plugin
            uint32_t localID = formID & 0xFFFFFF;

            for (auto* file : dataHandler->files) {
                if (file && file->compileIndex == highByte) {
                    return std::format("0x{:06X}~{}", localID, file->fileName);
                }
            }
        }
        return std::format("0x{:08X}~Unknown", formID);
    }

    RE::FormID ParseForm(const std::string& str) {
        auto pos = str.find('~');
        if (pos == std::string::npos) {
            return 0;
        }

        std::string idPart = str.substr(0, pos);
        std::string modName = str.substr(pos + 1);

        RE::FormID localID = std::stoul(idPart, nullptr, 16);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return 0;
        }

        const RE::TESFile* mod = dataHandler->LookupModByName(modName);
        if (!mod) {
            logger::error("Can't find mod: {}", modName);
            return 0;
        }

        // ESL (light plugin)
        if (mod->IsLight()) {
            localID &= 0xFFF;  // only lower 12 bits
        }

        RE::TESForm* form = dataHandler->LookupForm(localID, modName);
        // fallback to manual ID construction if form lookup fails
        // this is a case for unloaded references
        if (!form) {
            auto modIndex =
                mod->IsLight() ? (0xFE << 12) | (mod->smallFileCompileIndex & 0xFFF) : (mod->compileIndex & 0xFF);
            auto fullID = (modIndex << 24) | localID;
            return fullID;
        }

        return form->GetFormID();
    }


    BoundaryBox GetBoundingBox(const RE::TESObjectREFR* a_obj) {
        /*
        static std::shared_mutex GeometryCacheMutex;
        static std::unordered_map<RE::FormID, std::pair<RE::NiPoint3, RE::NiPoint3>> minMaxCache;

        const auto center = GetPosition(a_obj);

        const auto baseObj = a_obj->GetBaseObject();
        RE::FormID modelFormID = baseObj ? baseObj->GetFormID() : a_obj->GetFormID();

        std::pair<RE::NiPoint3, RE::NiPoint3> min_max;

        std::shared_lock readLock(GeometryCacheMutex);
        auto itCache = minMaxCache.find(modelFormID);
        if (itCache != minMaxCache.end()) {
            min_max = itCache->second;
        } else {
            readLock.unlock();
            const Math::LinAlg::Geometry geometry(a_obj);
            std::unique_lock writeLock(GeometryCacheMutex);
            min_max = geometry.GetBoundingBox();
            minMaxCache[modelFormID] = min_max;
            writeLock.unlock();
        }
        auto [min, max] = min_max;
        */
        auto center = a_obj->GetPosition();
        auto min = a_obj->GetBoundMin();
        auto max = a_obj->GetBoundMax();

        RE::NiPoint3 obj_angle = a_obj->GetAngle();

        if (auto obj3D = a_obj->Get3D()) {
            auto modelAngle = obj3D->local.rotate;
            center = obj3D->world.translate;

            min = center + min;
            max = center + max;

            const auto v1 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, min.y, min.z) - center, modelAngle) + center;
            const auto v2 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, min.y, min.z) - center, modelAngle) + center;
            const auto v3 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, max.y, min.z) - center, modelAngle) + center;
            const auto v4 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, max.y, min.z) - center, modelAngle) + center;

            const auto v5 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, min.y, max.z) - center, modelAngle) + center;
            const auto v6 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, min.y, max.z) - center, modelAngle) + center;
            const auto v7 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, max.y, max.z) - center, modelAngle) + center;
            const auto v8 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, max.y, max.z) - center, modelAngle) + center;

            return {v1, v2, v3, v4, v5, v6, v7, v8};
        } else {

            min = center + min;
            max = center + max;

            const auto v1 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, min.y, min.z) - center, obj_angle) + center;
            const auto v2 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, min.y, min.z) - center, obj_angle) + center;
            const auto v3 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, max.y, min.z) - center, obj_angle) + center;
            const auto v4 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, max.y, min.z) - center, obj_angle) + center;

            const auto v5 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, min.y, max.z) - center, obj_angle) + center;
            const auto v6 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, min.y, max.z) - center, obj_angle) + center;
            const auto v7 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(max.x, max.y, max.z) - center, obj_angle) + center;
            const auto v8 =
                Math::LinAlg::Geometry::Rotate(RE::NiPoint3(min.x, max.y, max.z) - center, obj_angle) + center;

            return {v1, v2, v3, v4, v5, v6, v7, v8};
        }
    }

    static RE::bhkRigidBody* GetRigidBody(const RE::TESObjectREFR* refr) {
        if (refr) {
            return nullptr;
        }
        const auto object3D = refr->Get3D();
        if (!object3D) {
            return nullptr;
        }
        if (const auto body = object3D->GetCollisionObject()) {
            return body->GetRigidBody();
        }
        return nullptr;
    }

    RE::NiPoint3 GetPosition(const RE::TESObjectREFR* obj) {
        if (!obj) {
            return {0, 0, 0};
        }
        const auto body = GetRigidBody(obj);
        if (!body) return obj->GetPosition();
        RE::hkVector4 havockPosition;
        body->GetPosition(havockPosition);
        float components[4];
        _mm_store_ps(components, havockPosition.quad);
        RE::NiPoint3 newPosition = {components[0], components[1], components[2]};
        constexpr float havockToSkyrimConversionRate = 69.9915f;
        newPosition *= havockToSkyrimConversionRate;
        return newPosition;
    }

    float RandomFloat(float min, float max) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }

}  // namespace Utils
