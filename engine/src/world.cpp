#include "world.h"

#include "polygon.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace gge {

namespace {

constexpr double kPi = 3.14159265358979323846;
// 한 번의 MoveCircle이 허용하는 최대 걸음 수 환산 이동량(radius/2 걸음 기준). 초과하면 거부한다.
constexpr float kMaxMoveSteps = 1024.0f;
// 입력 크기가 이 값 미만이면 입력 없음으로 본다(아날로그 스틱의 잔떨림 제거).
constexpr float kInputDeadzone = 0.2f;
// 카메라 컷 이후 입력 방향이 컷 직후 방향에서 이 각도(60도)보다 더 꺾이면 방향 기준을 새 방으로 옮긴다. cos(60도).
constexpr float kLatchReleaseCos = 0.5f;
// 시선의 (수평 성분 길이 / 전체 길이)가 이 값 이하(거의 수직으로 내려보는 카메라, 약 0.06도)이면 "앞" 방향이
// 정해지지 않거나 불안정하다. 좌표 크기와 무관하도록 절대값이 아니라 비율로 판정한다.
constexpr float kMinHorizontalLookRatio = 1e-3f;
// 폴링되지 않은 트리거 이벤트를 쌓아 두는 최대 개수. 넘으면 가장 오래된 것부터 버린다.
constexpr size_t kMaxQueuedTriggerEvents = 1024;

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

    float lookX = camera.target[0] - camera.eye[0], lookZ = camera.target[2] - camera.eye[2];
    float horizontalLen = std::hypot(lookX, lookZ);
    if (horizontalLen <= kMinHorizontalLookRatio * std::hypot(horizontalLen, camera.target[1] - camera.eye[1])) {
        throw std::runtime_error("room camera must not look straight up or down");
    }

    Room room;
    room.forward[0] = lookX / horizontalLen;
    room.forward[1] = lookZ / horizontalLen;
    room.polygon.assign(xz, xz + 2 * vertexCount);
    room.camera = camera;
    _rooms.push_back(std::move(room));
    return static_cast<int>(_rooms.size());
}

int World::AddBoxCollider(float minX, float minZ, float maxX, float maxZ)
{
    for (float v : {minX, minZ, maxX, maxZ}) {
        if (!std::isfinite(v)) {
            throw std::runtime_error("box collider has a non-finite coordinate");
        }
    }
    if (!(minX < maxX && minZ < maxZ)) {
        throw std::runtime_error("box collider needs min < max on both axes");
    }
    _boxes.push_back({minX, minZ, maxX, maxZ});
    _boxEnabled.push_back(1);
    _activeBoxes.push_back(_boxes.back());
    return static_cast<int>(_boxes.size());
}

void World::SetColliderEnabled(int colliderId, bool enabled)
{
    if (colliderId < 1 || static_cast<size_t>(colliderId) > _boxes.size()) {
        throw std::runtime_error("unknown collider id " + std::to_string(colliderId));
    }
    _boxEnabled[static_cast<size_t>(colliderId) - 1] = enabled ? 1 : 0;
    _activeBoxes.clear();
    for (size_t i = 0; i < _boxes.size(); ++i) {
        if (_boxEnabled[i] != 0) {
            _activeBoxes.push_back(_boxes[i]);
        }
    }
}

bool World::IsColliderEnabled(int colliderId) const
{
    if (colliderId < 1 || static_cast<size_t>(colliderId) > _boxes.size()) {
        throw std::runtime_error("unknown collider id " + std::to_string(colliderId));
    }
    return _boxEnabled[static_cast<size_t>(colliderId) - 1] != 0;
}

Vec2 World::MoveCircle(float x, float z, float dx, float dz, float radius) const
{
    for (float v : {x, z, dx, dz, radius}) {
        if (!std::isfinite(v)) {
            throw std::runtime_error("move has a non-finite argument");
        }
    }
    if (!(radius > 0.0f)) {
        throw std::runtime_error("move radius must be positive");
    }
    // 걸음 수가 이동량에 비례하므로, 비정상적으로 큰 이동(예: 단위 실수)이 프레임을 멈추지 않게 거부한다.
    if (std::sqrt(dx * dx + dz * dz) > kMaxMoveSteps * radius * 0.5f) {
        throw std::runtime_error("move distance is too large for the given radius");
    }
    return gge::MoveCircle(_activeBoxes, {x, z}, {dx, dz}, radius);
}

int World::FindRoom(float x, float z) const
{
    for (size_t i = 0; i < _rooms.size(); ++i) {
        if (PolygonContains(_rooms[i].polygon, x, z)) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

int World::UpdateActiveRoom(float x, float z)
{
    if (_activeRoom != 0 && PolygonContains(RoomById(_activeRoom).polygon, x, z)) {
        return _activeRoom;  // 여전히 현재 방 안: 겹침 경계에서도 전환하지 않는다.
    }
    int found = FindRoom(x, z);
    if (found != 0) {
        _activeRoom = found;
    }
    // found == 0: 어떤 방에도 속하지 않으므로 마지막 활성 방(없으면 0)을 유지한다.
    return _activeRoom;
}

Vec2 World::MoveInput(float inputX, float inputY, float speed, float dt)
{
    for (float v : {inputX, inputY, speed, dt}) {
        if (!std::isfinite(v)) {
            throw std::runtime_error("move input has a non-finite argument");
        }
    }
    if (speed < 0.0f || dt < 0.0f) {
        throw std::runtime_error("move speed and dt must not be negative");
    }
    if (_activeRoom == 0) {
        throw std::runtime_error("no active room (call UpdateActiveRoom first)");
    }

    float magnitude = std::hypot(inputX, inputY);
    if (magnitude < kInputDeadzone) {
        _inputRoom = _activeRoom;  // 입력 없음: 다음 입력은 지금의 활성 방 카메라 기준으로 시작한다.
        _hasCutDirection = false;
        return {0.0f, 0.0f};
    }
    float dirX = inputX / magnitude, dirY = inputY / magnitude;
    if (_inputRoom == 0) {
        _inputRoom = _activeRoom;  // 첫 입력(놓은 적 없이 시작하거나 리셋 직후): 현재 방 기준.
    }
    if (_inputRoom == _activeRoom) {
        _hasCutDirection = false;  // 컷 상태가 아님
    } else if (!_hasCutDirection) {
        _cutDirection[0] = dirX;  // 컷 직후 첫 입력: 이 방향을 기준으로 이후 꺾임을 잰다.
        _cutDirection[1] = dirY;
        _hasCutDirection = true;
    } else if (dirX * _cutDirection[0] + dirY * _cutDirection[1] < kLatchReleaseCos) {
        _inputRoom = _activeRoom;  // 크게 꺾었다: 새 방 화면 기준으로 조작한다.
        _hasCutDirection = false;
    }
    // 그 외: 입력 중이고 크게 꺾지 않았으므로 _inputRoom(시작 시 방)을 그대로 쓴다.

    const Room& room = RoomById(_inputRoom);
    float forwardX = room.forward[0], forwardZ = room.forward[1];
    float rightX = -forwardZ, rightZ = forwardX;  // forward x up(0,1,0)

    // 데드존을 빼고 다시 펴서 경계에서 0부터 시작하게 한다(천천히 걷기 가능). 크기는 1로 제한.
    float strength = (std::min(magnitude, 1.0f) - kInputDeadzone) / (1.0f - kInputDeadzone);
    float scale = strength * speed * dt;
    Vec2 move = {(rightX * dirX + forwardX * dirY) * scale, (rightZ * dirX + forwardZ * dirY) * scale};
    if (!std::isfinite(move.x) || !std::isfinite(move.z)) {
        throw std::runtime_error("move input result overflows (speed * dt too large)");  // 유한한 인자의 곱이 float 범위를 넘은 경우
    }
    return move;
}

void World::ResetInputFrame()
{
    _inputRoom = 0;
    _hasCutDirection = false;
}

int World::AddTrigger(const float* xz, int vertexCount, int tag)
{
    ValidatePolygon(xz, vertexCount);
    Trigger trigger;
    trigger.polygon.assign(xz, xz + 2 * vertexCount);
    trigger.tag = tag;
    _triggers.push_back(std::move(trigger));
    return static_cast<int>(_triggers.size());
}

void World::SetTriggerEnabled(int triggerId, bool enabled) { TriggerById(triggerId).enabled = enabled; }

int World::UpdateTriggers(float x, float z)
{
    if (!std::isfinite(x) || !std::isfinite(z)) {
        throw std::runtime_error("trigger update has a non-finite position");
    }
    // 먼저 모든 트리거의 새 상태를 계산해 두고(이벤트를 쌓는 동안 상태가 섞이지 않게), Exit -> Enter 순서로 쌓는다.
    std::vector<bool> now(_triggers.size());
    for (size_t i = 0; i < _triggers.size(); ++i) {
        now[i] = _triggers[i].enabled && PolygonContains(_triggers[i].polygon, x, z);
    }
    int produced = 0;
    for (size_t i = 0; i < _triggers.size(); ++i) {
        if (_triggers[i].inside && !now[i]) {
            PushEvent(_triggers[i], static_cast<int>(i) + 1, kTriggerExit);
            ++produced;
        }
    }
    for (size_t i = 0; i < _triggers.size(); ++i) {
        if (!_triggers[i].inside && now[i]) {
            PushEvent(_triggers[i], static_cast<int>(i) + 1, kTriggerEnter);
            ++produced;
        }
    }
    for (size_t i = 0; i < _triggers.size(); ++i) {
        _triggers[i].inside = now[i];
    }
    return produced;
}

bool World::PollTriggerEvent(TriggerEvent& out)
{
    if (_events.empty()) {
        return false;
    }
    out = _events.front();
    _events.pop_front();
    return true;
}

bool World::IsInsideTrigger(int triggerId) const { return TriggerById(triggerId).inside; }

void World::PushEvent(const Trigger& trigger, int triggerId, int kind)
{
    if (_events.size() >= kMaxQueuedTriggerEvents) {
        _events.pop_front();  // 가득 참: 폴링되지 않은 가장 오래된 이벤트를 버린다
    }
    _events.push_back({triggerId, kind, trigger.tag});
}

World::Trigger& World::TriggerById(int triggerId)
{
    if (triggerId < 1 || triggerId > static_cast<int>(_triggers.size())) {
        throw std::runtime_error("unknown trigger id " + std::to_string(triggerId));
    }
    return _triggers[triggerId - 1];
}

const World::Trigger& World::TriggerById(int triggerId) const
{
    return const_cast<World*>(this)->TriggerById(triggerId);
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
