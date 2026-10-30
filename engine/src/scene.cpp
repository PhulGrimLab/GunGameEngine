#include "scene.h"

#include <cmath>

#include "error.h"
#include "texture_loader.h"

namespace gge {

using namespace DirectX;

namespace {

// 클립 시간을 [0, duration)으로 감는다. C# AnimationPlayer.Wrap과 같은 식이다(부동소수점 오차로 duration과 같아지면 0).
float Wrap(float time, float duration)
{
    if (duration <= 0)
        return 0;
    float wrapped = time - duration * std::floor(time / duration);
    return wrapped >= duration ? 0 : wrapped;
}

// 어떤 항목에서 난 오류인지 붙여 다시 던진다.
template <class F>
void Wrapped(const std::string& what, F&& f)
{
    try {
        f();
    } catch (const std::exception& e) {
        throw std::runtime_error(what + ": " + e.what());
    }
}

}  // namespace

Scene::Scene(Renderer& renderer, SceneData data) : _renderer(renderer), _data(std::move(data))
{
    try {
        Build();
    } catch (...) {
        Destroy();  // 중간까지 만든 것을 렌더러에 남기지 않는다
        throw;
    }
}

Scene::~Scene() { Destroy(); }

void Scene::Build()
{
    // 리소스
    for (size_t i = 0; i < _data.textures.size(); ++i) {
        const std::string what = "texture " + std::to_string(i);
        Wrapped(what, [&] {
            ImageData image = LoadImageMemory(_data.textures[i].data(), _data.textures[i].size(), what);
            _textureIds.push_back(_renderer.CreateTexture(image.rgba.data(), image.width, image.height));
        });
    }
    for (size_t i = 0; i < _data.meshes.size(); ++i) {
        SceneMesh& mesh = _data.meshes[i];
        Wrapped("mesh " + std::to_string(i), [&] {
            _meshIds.push_back(_renderer.CreateMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size() / kVertexFloats),
                                                    mesh.indices.data(), static_cast<int>(mesh.indices.size()),
                                                    mesh.skinned ? mesh.joints.data() : nullptr, mesh.skinned ? mesh.weights.data() : nullptr));
            _skeletonIds.push_back(0);
            _clipIds.emplace_back();
            if (!mesh.skinned)
                return;
            _skeletonIds.back() = _renderer.CreateSkeleton(mesh.skeleton);
            for (SceneClip& clip : mesh.clips)
                _clipIds.back().push_back(_renderer.CreateClip(clip.clip));
        });
    }

    // 오브젝트 그래프
    _objectIds.reserve(_data.nodes.size());
    for (const SceneNodeData& node : _data.nodes) {
        Wrapped("node '" + node.name + "'", [&] {
            const int mesh = node.mesh < 0 ? 0 : _meshIds[node.mesh];
            const int texture = node.texture < 0 ? 0 : _textureIds[node.texture];
            const int parent = node.parent < 0 ? 0 : _objectIds[node.parent];
            const int object = _renderer.CreateObject(mesh, texture, parent);
            _objectIds.push_back(object);
            if (parent == 0)
                _rootObjects.push_back(object);
            _renderer.SetObjectTransform(object, node.position, node.rotation, node.scale);
            _renderer.SetObjectColor(object, node.color);
            _renderer.SetObjectAlpha(object, node.alpha);
            _renderer.SetObjectCastsShadow(object, node.castsShadow);

            if (node.mesh < 0 || !_data.meshes[node.mesh].skinned)
                return;  // 정적 메시는 클립이 있어도 쓰지 않는다(에디터와 같다)
            _renderer.SetObjectSkeleton(object, _skeletonIds[node.mesh]);
            if (node.clip.empty())
                return;
            const std::vector<SceneClip>& clips = _data.meshes[node.mesh].clips;
            for (size_t c = 0; c < clips.size(); ++c) {
                if (clips[c].name != node.clip)
                    continue;
                const int clipId = _clipIds[node.mesh][c];
                _renderer.SetObjectPose(object, clipId, 0, 0, 0, 0);
                _animated.push_back({object, clipId, clips[c].clip.duration, 0, node.speed, node.playing});
                return;
            }
            throw std::runtime_error("clip '" + node.clip + "' was not found in its mesh");
        });
    }

    // 조명
    const SceneLightingData& light = _data.lighting;
    Wrapped("lighting", [&] {
        _renderer.SetAmbient(light.ambient);
        _renderer.SetDirectionalLight(light.sunDirection, light.sunColor);
        for (const ScenePointLight& p : light.points)
            _lightIds.push_back(_renderer.CreatePointLight(p.position, p.color, p.range));
        _renderer.SetShadow(light.shadowEnabled, light.shadowPlaneY, light.shadowOpacity);
    });

    // 게임플레이 월드
    for (const SceneRoomData& room : _data.rooms)
        Wrapped("room '" + room.name + "'", [&] { _world.AddRoom(room.polygon.data(), static_cast<int>(room.polygon.size() / 2), room.camera); });
    for (const SceneColliderData& c : _data.colliders)
        Wrapped("collider", [&] { _world.AddBoxCollider(c.minX, c.minZ, c.maxX, c.maxZ); });
    for (const SceneTriggerData& t : _data.triggers)
        Wrapped("trigger '" + t.name + "'", [&] { _world.AddTrigger(t.polygon.data(), static_cast<int>(t.polygon.size() / 2), t.tag); });

    _player = _data.player;
    if (!_player.present)
        return;
    if (_data.rooms.empty())
        throw std::runtime_error("the scene has no rooms (at least one room is needed for the camera)");
    const SceneNodeData& node = _data.nodes[_player.node];
    _position = _player.hasSpawn ? Vec2{_player.spawn[0], _player.spawn[1]} : Vec2{node.position.x, node.position.z};
    _yaw = node.rotation.y;
    _baseHeight = node.position.y;
    _baseRotation = node.rotation;
    if (_world.UpdateActiveRoom(_position.x, _position.z) == 0)
        throw std::runtime_error("the player spawn (" + std::to_string(_position.x) + ", " + std::to_string(_position.z) + ") is outside every room");
    UpdateTriggers();  // 영역 안에서 시작하면 첫 이벤트로 Enter가 나온다
    ApplyPlayer();
}

void Scene::Destroy()
{
    // 소멸자와 생성 실패 경로에서 불린다: 개별 해제가 실패해도 나머지를 계속 정리한다(예외를 밖으로 내보내지 않는다).
    auto quiet = [](auto&& f) {
        try {
            f();
        } catch (const std::exception&) {
            // 이미 해제됐거나 렌더러가 먼저 정리된 경우: 할 일이 없다
        }
    };
    for (int id : _rootObjects)
        quiet([&] { _renderer.DestroyObject(id); });  // 자손까지 함께 지워진다
    for (int id : _lightIds)
        quiet([&] { _renderer.DestroyPointLight(id); });
    for (const std::vector<int>& clips : _clipIds)
        for (int id : clips)
            quiet([&] { _renderer.DestroyClip(id); });
    for (int id : _skeletonIds)
        if (id != 0)
            quiet([&] { _renderer.DestroySkeleton(id); });
    for (int id : _meshIds)
        quiet([&] { _renderer.DestroyMesh(id); });
    for (int id : _textureIds)
        quiet([&] { _renderer.DestroyTexture(id); });
    _rootObjects.clear();
    _lightIds.clear();
    _clipIds.clear();
    _skeletonIds.clear();
    _meshIds.clear();
    _textureIds.clear();
    _objectIds.clear();
    _animated.clear();
}

void Scene::Step(float inputX, float inputY, float dt)
{
    if (!std::isfinite(dt) || dt < 0)
        throw std::runtime_error("dt must be a finite non-negative number");

    if (_player.present) {
        const Vec2 intended = _world.MoveInput(inputX, inputY, _player.speed, dt);
        _position = _world.MoveCircle(_position.x, _position.z, intended.x, intended.z, _player.radius);
        if (intended.x != 0 || intended.z != 0)
            _yaw = std::atan2(intended.x, intended.z);  // 의도한 방향(벽에 막혀도 그쪽을 본다). 모델 앞쪽 = +Z 가정
        _world.UpdateActiveRoom(_position.x, _position.z);  // 방을 넘었으면 카메라가 바뀐다
        UpdateTriggers();
        ApplyPlayer();
    }

    for (Animated& a : _animated) {
        if (!a.playing)
            continue;
        const float delta = dt * a.speed;
        if (!std::isfinite(delta))
            continue;  // 속도가 비정상이면 이 프레임의 진행은 무시한다(시간이 NaN으로 굳으면 복구되지 않는다)
        a.time = Wrap(a.time + delta, a.duration);
        _renderer.SetObjectPose(a.object, a.clip, a.time, 0, 0, 0);
    }
}

void Scene::UpdateTriggers()
{
    _world.UpdateTriggers(_position.x, _position.z);
    TriggerEvent e;
    while (_world.PollTriggerEvent(e))
        _events.push_back({e.kind, e.tag, _data.triggers[static_cast<size_t>(e.triggerId) - 1].name});
}

void Scene::ApplyPlayer()
{
    const SceneNodeData& node = _data.nodes[_player.node];
    _renderer.SetObjectTransform(_objectIds[_player.node], {_position.x, _baseHeight, _position.z},
                                 {_baseRotation.x, _yaw, _baseRotation.z}, node.scale);
    const RoomCamera& camera = _world.GetRoomCamera(_world.ActiveRoom());
    _renderer.SetCamera({camera.eye[0], camera.eye[1], camera.eye[2]}, {camera.target[0], camera.target[1], camera.target[2]}, camera.fovY);
}

bool Scene::PollEvent(SceneEvent& out)
{
    if (_events.empty())
        return false;
    out = std::move(_events.front());
    _events.pop_front();
    return true;
}

const std::string& Scene::ActiveRoomName() const
{
    static const std::string none;
    const int room = _world.ActiveRoom();
    return room > 0 ? _data.rooms[static_cast<size_t>(room) - 1].name : none;
}

int Scene::FindObject(const std::string& nodeName) const
{
    for (size_t i = 0; i < _data.nodes.size() && i < _objectIds.size(); ++i)
        if (_data.nodes[i].name == nodeName)
            return _objectIds[i];
    return 0;
}

}  // namespace gge
