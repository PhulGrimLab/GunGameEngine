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
    _bakedNodes = _data.nodes;
    _bakedCount = _data.nodes.size();
    _objectIds.reserve(_data.nodes.size());
    _animOfNode.assign(_data.nodes.size(), -1);
    for (size_t nodeIndex = 0; nodeIndex < _bakedCount; ++nodeIndex) {
        const SceneNodeData& node = _data.nodes[nodeIndex];
        Wrapped("node '" + node.name + "'", [&] {
            const int parent = node.parent < 0 ? 0 : _objectIds[node.parent];
            std::optional<NodeAnim> anim;
            const int object = CreateNodeObject(node, nodeIndex, parent, anim);
            _objectIds.push_back(object);
            if (parent == 0)
                _rootObjects.push_back(object);
            _nodeByName.try_emplace(node.name, nodeIndex);   // 같은 이름이면 가장 낮은 번호가 첫 노드
            if (anim) {
                _animOfNode[nodeIndex] = static_cast<int>(_anims.size());
                _anims.push_back(std::move(*anim));
            }
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

int Scene::CreateNodeObject(const SceneNodeData& node, size_t slot, int parentObject, std::optional<NodeAnim>& anim)
{
    anim.reset();
    const int mesh = node.mesh < 0 ? 0 : _meshIds[node.mesh];
    const int texture = node.texture < 0 ? 0 : _textureIds[node.texture];
    const int object = _renderer.CreateObject(mesh, texture, parentObject);
    try {
        _renderer.SetObjectTransform(object, node.position, node.rotation, node.scale);
        _renderer.SetObjectColor(object, node.color);
        _renderer.SetObjectAlpha(object, node.alpha);
        _renderer.SetObjectCastsShadow(object, node.castsShadow);

        if (node.mesh < 0 || !_data.meshes[node.mesh].skinned)
            return object;  // 정적 메시는 클립이 있어도 쓰지 않는다(에디터와 같다)
        _renderer.SetObjectSkeleton(object, _skeletonIds[node.mesh]);

        // 스킨 노드마다 재생 상태를 둔다: 씬이 정한 초기 클립(있다면)으로 시작하고, 스크립트가 나중에 다른 클립으로 바꿀 수 있다.
        NodeAnim a;
        a.node = slot;
        a.object = object;
        a.mesh = node.mesh;
        if (!node.clip.empty()) {
            const std::vector<SceneClip>& clips = _data.meshes[node.mesh].clips;
            size_t c = 0;
            while (c < clips.size() && clips[c].name != node.clip)
                ++c;
            if (c == clips.size())
                throw std::runtime_error("clip '" + node.clip + "' was not found in its mesh");
            a.player.Play({_clipIds[node.mesh][c], clips[c].clip.duration}, 0, false, true);
            a.player.speed = node.speed;
            a.player.playing = node.playing;
        }
        ApplyPose(a);
        anim = std::move(a);
        return object;
    } catch (...) {
        try {
            _renderer.DestroyObject(object);
        } catch (const std::exception&) {
        }
        throw;
    }
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
    for (const auto& [name, instance] : _instances)
        quiet([&] { _renderer.DestroyObject(instance.rootObject); });  // 동적 오브젝트(부모가 구운 노드면 아래에서 한 번 더 지워지려다 무시된다)
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
    _anims.clear();
    _animOfNode.clear();
    _instances.clear();
    _nodeByName.clear();
    _freeSlots.clear();
    _liveDynamic = 0;
}

void Scene::Step(float inputX, float inputY, float dt)
{
    if (!std::isfinite(dt) || dt < 0)
        throw std::runtime_error("dt must be a finite non-negative number");
    _spawnsThisStep = 0;

    if (_player.present) {
        const Vec2 intended = _world.MoveInput(inputX, inputY, _player.speed, dt);
        const Vec2 before = _position;
        _position = _world.MoveCircle(_position.x, _position.z, intended.x, intended.z, _player.radius);
        _playerSpeed = dt > 0 ? std::hypot(_position.x - before.x, _position.z - before.z) / dt : 0.0f;   // 충돌/슬라이딩 반영 후 실제 이동
        if (intended.x != 0 || intended.z != 0)
            _yaw = std::atan2(intended.x, intended.z);  // 의도한 방향(벽에 막혀도 그쪽을 본다). 모델 앞쪽 = +Z 가정
        _world.UpdateActiveRoom(_position.x, _position.z);  // 방을 넘었으면 카메라가 바뀐다
        UpdateTriggers();
        ApplyPlayer();
    }

    for (NodeAnim& a : _anims) {
        if (!a.player.playing || !a.player.HasClip())
            continue;
        a.player.Update(dt);
        ApplyPose(a);
    }
}

void Scene::ApplyPose(const NodeAnim& anim)
{
    const ClipPlayer::Pose p = anim.player.CurrentPose();
    _renderer.SetObjectPose(anim.object, p.clipA, p.timeA, p.clipB, p.timeB, p.blend);
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

int Scene::FindNode(const std::string& name) const
{
    const auto it = _nodeByName.find(name);
    return it == _nodeByName.end() ? -1 : static_cast<int>(it->second);
}

int Scene::FindObject(const std::string& nodeName) const
{
    const int index = FindNode(nodeName);
    return index < 0 ? 0 : _objectIds[static_cast<size_t>(index)];
}

int Scene::SetTriggerEnabled(const std::string& name, bool enabled)
{
    int count = 0;
    for (size_t i = 0; i < _data.triggers.size(); ++i)
        if (_data.triggers[i].name == name) {
            _world.SetTriggerEnabled(static_cast<int>(i) + 1, enabled);
            ++count;
        }
    return count;
}

int Scene::SetColliderEnabled(const std::string& name, bool enabled)
{
    int count = 0;
    for (size_t i = 0; i < _data.colliders.size(); ++i)
        if (_data.colliders[i].name == name) {
            _world.SetColliderEnabled(static_cast<int>(i) + 1, enabled);   // 월드의 콜라이더 ID는 씬의 순서 + 1
            ++count;
        }
    return count;
}

bool Scene::IsInsideTrigger(const std::string& name) const
{
    bool found = false, inside = false;
    for (size_t i = 0; i < _data.triggers.size(); ++i)
        if (_data.triggers[i].name == name) {
            found = true;
            inside = inside || _world.IsInsideTrigger(static_cast<int>(i) + 1);
        }
    if (!found)
        throw std::runtime_error("no trigger named '" + name + "'");
    return inside;
}

bool Scene::GetNodePosition(const std::string& nodeName, DirectX::XMFLOAT3& out) const
{
    const int index = FindNode(nodeName);
    if (index < 0)
        return false;
    out = _data.nodes[static_cast<size_t>(index)].position;
    return true;
}

size_t Scene::EditableNodeIndex(const std::string& nodeName) const
{
    const int index = FindNode(nodeName);
    if (index < 0)
        throw std::runtime_error("no node named '" + nodeName + "'");
    if (_player.present && index == _player.node)
        throw std::runtime_error("node '" + nodeName + "' is the player; its position, rotation and scale follow the player (use teleport_player to move it)");
    return static_cast<size_t>(index);
}

namespace {
void RequireFiniteVector(const DirectX::XMFLOAT3& v, const char* what)
{
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        throw std::runtime_error(std::string("node ") + what + " must be finite");
}
}  // namespace

void Scene::SetNodePosition(const std::string& nodeName, DirectX::XMFLOAT3 position)
{
    const size_t index = EditableNodeIndex(nodeName);
    RequireFiniteVector(position, "position");
    SceneNodeData& node = _data.nodes[index];
    _renderer.SetObjectTransform(_objectIds[index], position, node.rotation, node.scale);
    node.position = position;   // 렌더러가 받아들인 뒤에만 기록한다
}

bool Scene::GetNodeRotation(const std::string& nodeName, DirectX::XMFLOAT3& out) const
{
    const int index = FindNode(nodeName);
    if (index < 0)
        return false;
    out = _data.nodes[static_cast<size_t>(index)].rotation;
    return true;
}

void Scene::SetNodeRotation(const std::string& nodeName, DirectX::XMFLOAT3 rotation)
{
    const size_t index = EditableNodeIndex(nodeName);
    RequireFiniteVector(rotation, "rotation");
    SceneNodeData& node = _data.nodes[index];
    _renderer.SetObjectTransform(_objectIds[index], node.position, rotation, node.scale);
    node.rotation = rotation;
}

bool Scene::GetNodeScale(const std::string& nodeName, DirectX::XMFLOAT3& out) const
{
    const int index = FindNode(nodeName);
    if (index < 0)
        return false;
    out = _data.nodes[static_cast<size_t>(index)].scale;
    return true;
}

void Scene::SetNodeScale(const std::string& nodeName, DirectX::XMFLOAT3 scale)
{
    const size_t index = EditableNodeIndex(nodeName);
    RequireFiniteVector(scale, "scale");
    SceneNodeData& node = _data.nodes[index];
    _renderer.SetObjectTransform(_objectIds[index], node.position, node.rotation, scale);
    node.scale = scale;
}

bool Scene::GetNodeVisible(const std::string& nodeName, bool& out) const
{
    const int index = FindNode(nodeName);
    if (index < 0)
        return false;
    out = _renderer.IsObjectVisible(_objectIds[static_cast<size_t>(index)]);
    return true;
}

void Scene::SetNodeVisible(const std::string& nodeName, bool visible)
{
    const int index = FindNode(nodeName);
    if (index < 0)
        throw std::runtime_error("no node named '" + nodeName + "'");
    _renderer.SetObjectVisible(_objectIds[static_cast<size_t>(index)], visible);   // 플레이어 노드도 숨길 수 있다(컷신/퇴장)
}

// ---- 클립 재생 ----
namespace {

constexpr float kMaxFadeSeconds = 60.0f;       // 크로스페이드 길이 상한(오타로 인한 영원한 블렌드 방지)
constexpr size_t kMaxListedClipNames = 20;     // "없는 클립" 오류 문구에 싣는 이름 수 상한

// 시간을 delta만큼 진행한다: 반복이면 [0, duration)으로 감고, 한 번 재생이면 [0, duration]에서 멈춘다(길이 0이면 0). C# AnimationPlayer.Advance와 같다.
float Advance(float time, float delta, float duration, bool loop)
{
    if (loop)
        return Wrap(time + delta, duration);
    if (duration <= 0)
        return 0;
    return std::min(std::max(time + delta, 0.0f), duration);
}

}  // namespace

void ClipPlayer::Play(const Ref& clip, float fadeSeconds, bool restart, bool loop)
{
    if (_hasCurrent && _current == clip && !restart)
        return;
    if (_hasCurrent && _current != clip && fadeSeconds > 0) {
        _hasFrom = true;
        _from = _current;
        _fromLoop = _currentLoop;
        _fromTime = _currentTime;
        _fadeElapsed = 0;
        _fadeDuration = fadeSeconds;
    } else {
        _hasFrom = false;
    }
    _current = clip;
    _hasCurrent = true;
    _currentLoop = loop;
    _currentTime = 0;
}

void ClipPlayer::Clear()
{
    _hasCurrent = false;
    _currentLoop = true;
    _hasFrom = false;
    _currentTime = 0;
}

void ClipPlayer::Update(float dt)
{
    if (!playing || !_hasCurrent)
        return;
    const float delta = dt * speed;
    if (!std::isfinite(delta))
        return;  // 속도가 비정상이면 이 프레임의 진행은 무시한다(시간이 NaN으로 굳으면 복구되지 않는다)
    _currentTime = Advance(_currentTime, delta, _current.duration, _currentLoop);
    if (_hasFrom) {
        _fromTime = Advance(_fromTime, delta, _from.duration, _fromLoop);
        _fadeElapsed += dt;
        if (_fadeElapsed >= _fadeDuration)
            _hasFrom = false;
    }
}

ClipPlayer::Pose ClipPlayer::CurrentPose() const
{
    if (!_hasCurrent)
        return {};
    if (_hasFrom)
        return {_from.id, _fromTime, _current.id, _currentTime, std::min(std::max(_fadeElapsed / _fadeDuration, 0.0f), 1.0f)};
    return {_current.id, _currentTime, 0, 0, 0};
}

bool ClipPlayer::Finished() const
{
    return !_currentLoop && _hasCurrent && (speed >= 0 ? _currentTime >= _current.duration : _currentTime <= 0);
}

size_t Scene::AnimIndex(const std::string& nodeName) const
{
    const int index = FindNode(nodeName);
    if (index < 0)
        throw std::runtime_error("no node named '" + nodeName + "'");
    const int anim = static_cast<size_t>(index) < _animOfNode.size() ? _animOfNode[static_cast<size_t>(index)] : -1;
    if (anim < 0)
        throw std::runtime_error("node '" + nodeName + "' has no skeleton animation (it is not a skinned model)");
    return static_cast<size_t>(anim);
}

void Scene::PlayClip(const std::string& nodeName, const std::string& clipName, float fadeSeconds, bool restart, bool loop)
{
    NodeAnim& anim = _anims[AnimIndex(nodeName)];
    if (!std::isfinite(fadeSeconds) || fadeSeconds < 0 || fadeSeconds > kMaxFadeSeconds)
        throw std::runtime_error("fade seconds must be finite and between 0 and " + std::to_string(static_cast<int>(kMaxFadeSeconds)));
    const std::vector<SceneClip>& clips = _data.meshes[static_cast<size_t>(anim.mesh)].clips;
    size_t c = 0;
    while (c < clips.size() && clips[c].name != clipName)
        ++c;
    if (c == clips.size()) {
        std::string available;
        for (size_t i = 0; i < clips.size() && i < kMaxListedClipNames; ++i)
            available += (i ? ", " : "") + clips[i].name;
        if (clips.size() > kMaxListedClipNames)
            available += ", ... (" + std::to_string(kMaxListedClipNames) + " of " + std::to_string(clips.size()) + " clips shown)";
        throw std::runtime_error("no clip named '" + clipName + "' in node '" + nodeName + "' (" +
                                 (clips.empty() ? std::string("the model has no clips") : "available: " + available) + ")");
    }
    anim.player.Play({_clipIds[static_cast<size_t>(anim.mesh)][c], clips[c].clip.duration}, fadeSeconds, restart, loop);
    ApplyPose(anim);   // 일시정지 중이어도 바뀐 포즈가 바로 보인다
}

void Scene::StopClip(const std::string& nodeName)
{
    NodeAnim& anim = _anims[AnimIndex(nodeName)];
    anim.player.Clear();
    ApplyPose(anim);
}

void Scene::SetClipSpeed(const std::string& nodeName, float speed)
{
    NodeAnim& anim = _anims[AnimIndex(nodeName)];
    if (!std::isfinite(speed))
        throw std::runtime_error("clip speed must be finite");
    anim.player.speed = speed;
}

void Scene::SetClipPlaying(const std::string& nodeName, bool playing) { _anims[AnimIndex(nodeName)].player.playing = playing; }

Scene::NodeAnimation Scene::GetNodeAnimation(const std::string& nodeName) const
{
    const NodeAnim& anim = _anims[AnimIndex(nodeName)];
    const ClipPlayer& p = anim.player;
    NodeAnimation state;
    if (p.HasClip()) {
        const std::vector<SceneClip>& clips = _data.meshes[static_cast<size_t>(anim.mesh)].clips;
        for (size_t c = 0; c < clips.size(); ++c)
            if (_clipIds[static_cast<size_t>(anim.mesh)][c] == p.Current().id) {
                state.clip = clips[c].name;
                break;
            }
    }
    state.time = p.Time();
    state.playing = p.playing;
    state.speed = p.speed;
    state.fading = p.Fading();
    state.loop = p.Loop();
    state.finished = p.Finished();
    return state;
}

void Scene::TeleportPlayer(float x, float z)
{
    if (!_player.present)
        throw std::runtime_error("the scene has no player to teleport");
    if (!std::isfinite(x) || !std::isfinite(z))
        throw std::runtime_error("teleport position must be finite");
    if (_world.FindRoom(x, z) == 0)
        throw std::runtime_error("teleport position (" + std::to_string(x) + ", " + std::to_string(z) + ") is outside every room");
    _position = {x, z};
    _playerSpeed = 0;   // 순간이동은 이동 속도가 아니다
    _world.ResetInputFrame();
    _world.UpdateActiveRoom(x, z);
    UpdateTriggers();
    ApplyPlayer();
}

// ---- 동적 오브젝트 ----
void Scene::SpawnObject(const std::string& templateName, const std::string& instanceName, XMFLOAT3 position)
{
    // 1) 검사: 여기서 던지면 아무것도 바뀌지 않는다(렌더러 오브젝트는 아직 없다).
    if (instanceName.empty())
        throw std::runtime_error("the instance name must not be empty");
    if (instanceName.find('/') != std::string::npos)
        throw std::runtime_error("the instance name '" + instanceName + "' must not contain '/' (it separates an instance from its children)");
    if (instanceName.size() > kMaxInstanceNameBytes)
        throw std::runtime_error("the instance name is too long (max " + std::to_string(kMaxInstanceNameBytes) + " bytes)");
    RequireFiniteVector(position, "spawn position");

    const int found = FindNode(templateName);
    if (found < 0)
        throw std::runtime_error("no node named '" + templateName + "'");
    const size_t tpl = static_cast<size_t>(found);
    if (tpl >= _bakedCount)
        throw std::runtime_error("'" + templateName + "' is a spawned object; spawn from a node of the scene file");
    if (_spawnsThisStep >= kMaxSpawnsPerStep)
        throw std::runtime_error("too many spawns in one frame (max " + std::to_string(kMaxSpawnsPerStep) + ")");

    // 서브트리: 템플릿과 자손(구운 노드는 부모가 항상 앞에 온다). localOf = 구운 번호 -> 서브트리 안의 순서.
    std::vector<size_t> subtree{tpl};
    std::vector<int> localOf(_bakedCount, -1), depth(_bakedCount, 0);
    localOf[tpl] = 0;
    depth[tpl] = 1;
    for (size_t i = tpl + 1; i < _bakedCount; ++i) {
        const int parent = _bakedNodes[i].parent;
        if (parent < 0 || depth[static_cast<size_t>(parent)] == 0)
            continue;
        depth[i] = depth[static_cast<size_t>(parent)] + 1;
        localOf[i] = static_cast<int>(subtree.size());
        subtree.push_back(i);
    }
    for (size_t i : subtree) {
        if (_player.present && i == static_cast<size_t>(_player.node))
            throw std::runtime_error("node '" + templateName + "' is or contains the player and cannot be spawned");
        if (depth[i] > kMaxCloneDepth)
            throw std::runtime_error("node '" + templateName + "' is nested too deeply to spawn (max depth " + std::to_string(kMaxCloneDepth) + ")");
    }
    if (_liveDynamic + static_cast<int>(subtree.size()) > kMaxDynamicNodes)
        throw std::runtime_error("too many spawned nodes (max " + std::to_string(kMaxDynamicNodes) + ", " + std::to_string(_liveDynamic) +
                                 " alive, this spawn needs " + std::to_string(subtree.size()) + ")");

    std::vector<std::string> names;
    names.reserve(subtree.size());
    names.push_back(instanceName);
    for (size_t k = 1; k < subtree.size(); ++k)
        names.push_back(instanceName + "/" + _bakedNodes[subtree[k]].name);
    for (const std::string& name : names)
        if (_nodeByName.count(name))
            throw std::runtime_error("a node named '" + name + "' already exists");

    // 2) 렌더러 오브젝트를 만든다. 중간에 실패하면 이미 만든 루트를 지워(자손 연쇄) 되돌린다. 멤버 상태는 아직 건드리지 않았다.
    const int baseParent = _bakedNodes[tpl].parent < 0 ? 0 : _objectIds[static_cast<size_t>(_bakedNodes[tpl].parent)];
    std::vector<int> objects(subtree.size(), 0);
    std::vector<std::optional<NodeAnim>> anims(subtree.size());
    for (size_t k = 0; k < subtree.size(); ++k) {
        SceneNodeData node = _bakedNodes[subtree[k]];
        node.name = names[k];
        if (k == 0)
            node.position = position;
        const int parentObject = k == 0 ? baseParent : objects[static_cast<size_t>(localOf[static_cast<size_t>(node.parent)])];
        try {
            objects[k] = CreateNodeObject(node, 0, parentObject, anims[k]);
            if (_failSpawnAfter >= 0 && static_cast<int>(k) + 1 == _failSpawnAfter)
                throw std::runtime_error("injected spawn failure");
        } catch (...) {
            if (k > 0 || objects[0] != 0) {
                try {
                    _renderer.DestroyObject(objects[0]);
                } catch (const std::exception&) {
                }
            }
            throw;
        }
    }

    // 3) 반영: 여기부터는 실패하지 않는 연산뿐이다(슬롯 배정, 맵/목록 갱신).
    std::vector<size_t> slots(subtree.size());
    for (size_t k = 0; k < subtree.size(); ++k) {
        size_t slot;
        if (!_freeSlots.empty()) {
            slot = _freeSlots.back();
            _freeSlots.pop_back();
        } else {
            slot = _data.nodes.size();
            _data.nodes.emplace_back();
            _objectIds.push_back(0);
            _animOfNode.push_back(-1);
        }
        slots[k] = slot;
    }
    for (size_t k = 0; k < subtree.size(); ++k) {
        SceneNodeData node = _bakedNodes[subtree[k]];
        node.name = names[k];
        node.parent = k == 0 ? _bakedNodes[tpl].parent : static_cast<int>(slots[static_cast<size_t>(localOf[static_cast<size_t>(node.parent)])]);
        if (k == 0)
            node.position = position;
        _data.nodes[slots[k]] = std::move(node);
        _objectIds[slots[k]] = objects[k];
        _animOfNode[slots[k]] = -1;
        if (anims[k]) {
            anims[k]->node = slots[k];
            _animOfNode[slots[k]] = static_cast<int>(_anims.size());
            _anims.push_back(std::move(*anims[k]));
        }
        _nodeByName.try_emplace(names[k], slots[k]);   // 같은 서브트리 안의 같은 이름은 먼저 만든 것이 첫 노드
    }
    _instances[instanceName] = Instance{std::move(slots), objects[0]};
    _liveDynamic += static_cast<int>(subtree.size());
    ++_spawnsThisStep;
}

void Scene::DestroyObject(const std::string& instanceName)
{
    const auto it = _instances.find(instanceName);
    if (it == _instances.end()) {
        if (_nodeByName.count(instanceName))
            throw std::runtime_error("'" + instanceName + "' was not created by spawn_object, so it cannot be destroyed (hide a scene node with set_node_visible; destroy the instance root, not its children)");
        throw std::runtime_error("no node named '" + instanceName + "'");
    }
    _renderer.DestroyObject(it->second.rootObject);   // 자손까지 지워진다. 실패하면 아무것도 바꾸지 않는다
    for (size_t slot : it->second.slots) {
        const int a = _animOfNode[slot];
        if (a >= 0) {
            const size_t last = _anims.size() - 1;
            if (static_cast<size_t>(a) != last) {
                _anims[static_cast<size_t>(a)] = std::move(_anims[last]);
                _animOfNode[_anims[static_cast<size_t>(a)].node] = a;
            }
            _anims.pop_back();
        }
        const auto name = _nodeByName.find(_data.nodes[slot].name);
        if (name != _nodeByName.end() && name->second == slot)
            _nodeByName.erase(name);
        _data.nodes[slot] = SceneNodeData{};
        _objectIds[slot] = 0;
        _animOfNode[slot] = -1;
        _freeSlots.push_back(slot);
    }
    _liveDynamic -= static_cast<int>(it->second.slots.size());
    _instances.erase(it);
}

}  // namespace gge
