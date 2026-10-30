#include "world.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace gge {

namespace {

constexpr int kMinPolygonVertices = 3;
constexpr double kPi = 3.14159265358979323846;
// 경계 위 판정과 퇴화(면적 0, 한 직선 위) 판정에 쓰는 허용 오차. 월드 단위(미터급 좌표)에 맞춘 값.
constexpr double kEpsilon = 1e-6;

struct Pt {
    double x, z;
};

Pt PointAt(const float* xz, int i) { return {xz[2 * i], xz[2 * i + 1]}; }

double Cross(Pt o, Pt a, Pt b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); }

double Dot(Pt o, Pt a, Pt b) { return (a.x - o.x) * (b.x - o.x) + (a.z - o.z) * (b.z - o.z); }

// p가 선분 ab 위(허용 오차 안)에 있는가.
bool OnSegment(Pt a, Pt b, Pt p)
{
    double lenSq = Dot(a, b, b);
    if (lenSq <= 0.0) {
        return std::hypot(p.x - a.x, p.z - a.z) <= kEpsilon;
    }
    double t = Dot(a, p, b) / lenSq;
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    return std::hypot(p.x - (a.x + t * (b.x - a.x)), p.z - (a.z + t * (b.z - a.z))) <= kEpsilon;
}

// 선분 ab와 cd가 (끝점 접촉 포함) 만나는가.
bool SegmentsTouch(Pt a, Pt b, Pt c, Pt d)
{
    double d1 = Cross(a, b, c), d2 = Cross(a, b, d), d3 = Cross(c, d, a), d4 = Cross(c, d, b);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }
    return OnSegment(a, b, c) || OnSegment(a, b, d) || OnSegment(c, d, a) || OnSegment(c, d, b);
}

// 다각형이 면적을 갖는 단순 다각형인지 검사하고 아니면 사유와 함께 예외를 던진다.
void ValidatePolygon(const float* xz, int n)
{
    if (!xz || n < kMinPolygonVertices) {
        throw std::runtime_error("room polygon needs at least 3 vertices");
    }
    for (int i = 0; i < 2 * n; ++i) {
        if (!std::isfinite(xz[i])) {
            throw std::runtime_error("room polygon has a non-finite coordinate");
        }
    }

    double area2 = 0.0;
    for (int i = 0; i < n; ++i) {
        Pt a = PointAt(xz, i), b = PointAt(xz, (i + 1) % n);
        if (std::hypot(b.x - a.x, b.z - a.z) <= kEpsilon) {
            throw std::runtime_error("room polygon has a zero-length edge");
        }
        area2 += a.x * b.z - b.x * a.z;
    }
    if (std::fabs(area2) <= kEpsilon) {
        throw std::runtime_error("room polygon has zero area");
    }

    for (int i = 0; i < n; ++i) {
        Pt a = PointAt(xz, i), b = PointAt(xz, (i + 1) % n);
        for (int j = i + 1; j < n; ++j) {
            Pt c = PointAt(xz, j), d = PointAt(xz, (j + 1) % n);
            bool adjacent = (j == i + 1) || (i == 0 && j == n - 1);
            if (adjacent) {
                // 이웃한 두 변은 공유 꼭짓점에서 만나는 것이 정상이다. 같은 직선 위에서 되돌아가는(뾰족한 돌기) 경우만 거부한다.
                Pt shared = (j == i + 1) ? b : a;
                Pt farA = (j == i + 1) ? a : b;
                Pt farB = (j == i + 1) ? d : c;
                if (std::fabs(Cross(shared, farA, farB)) <= kEpsilon && Dot(shared, farA, farB) > 0.0) {
                    throw std::runtime_error("room polygon edges overlap (self-intersecting)");
                }
            } else if (SegmentsTouch(a, b, c, d)) {
                throw std::runtime_error("room polygon edges cross (self-intersecting)");
            }
        }
    }
}

// (x,z)가 다각형 안이거나 경계 위인가. 경계는 포함으로 봐서 맞닿은 방 사이에 틈이 생기지 않게 한다.
bool Contains(const std::vector<float>& polygon, double x, double z)
{
    int n = static_cast<int>(polygon.size() / 2);
    Pt p{x, z};
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        Pt a = PointAt(polygon.data(), i), b = PointAt(polygon.data(), j);
        if (OnSegment(a, b, p)) {
            return true;
        }
        // 짝-홀 규칙: p에서 +x 방향 반직선이 변을 가로지르는 횟수의 홀짝.
        if ((a.z > z) != (b.z > z) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

}  // namespace

int World::AddRoom(const float* xz, int vertexCount, const RoomCamera& camera)
{
    ValidatePolygon(xz, vertexCount);
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(camera.eye[i]) || !std::isfinite(camera.target[i])) {
            throw std::runtime_error("room camera has a non-finite coordinate");
        }
    }
    if (camera.eye[0] == camera.target[0] && camera.eye[1] == camera.target[1] && camera.eye[2] == camera.target[2]) {
        throw std::runtime_error("room camera eye and target must differ");
    }
    if (!(camera.fovY > 0.0f && camera.fovY < kPi)) {
        throw std::runtime_error("room camera fov must be in (0, pi) radians");
    }

    Room room;
    room.polygon.assign(xz, xz + 2 * vertexCount);
    room.camera = camera;
    _rooms.push_back(std::move(room));
    return static_cast<int>(_rooms.size());
}

int World::FindRoom(float x, float z) const
{
    for (size_t i = 0; i < _rooms.size(); ++i) {
        if (Contains(_rooms[i].polygon, x, z)) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

int World::UpdateActiveRoom(float x, float z)
{
    if (_activeRoom != 0 && Contains(RoomById(_activeRoom).polygon, x, z)) {
        return _activeRoom;  // 여전히 현재 방 안: 겹침 경계에서도 전환하지 않는다.
    }
    int found = FindRoom(x, z);
    if (found != 0) {
        _activeRoom = found;
    }
    // found == 0: 어떤 방에도 속하지 않으므로 마지막 활성 방(없으면 0)을 유지한다.
    return _activeRoom;
}

const RoomCamera& World::GetRoomCamera(int roomId) const { return RoomById(roomId).camera; }

const World::Room& World::RoomById(int roomId) const
{
    if (roomId < 1 || roomId > static_cast<int>(_rooms.size())) {
        throw std::runtime_error("unknown room id " + std::to_string(roomId));
    }
    return _rooms[roomId - 1];
}

}  // namespace gge
