using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using GunGame.Interop;

namespace GunGame.Scene;

/// <summary>
/// 씬 그래프 노드. 메시가 없으면 변환만 전달하는 그룹 노드다.
/// 경로는 메모리에서는 절대 경로이고, 파일에 저장할 때만 씬 파일 기준 상대 경로가 된다.
/// </summary>
public sealed class SceneNode : INotifyPropertyChanged
{
    private string _name = "Node";
    private string? _meshPath;
    private string? _texturePath;
    private Vec3 _position;
    private Vec3 _rotation;
    private Vec3 _scale = new(1, 1, 1);
    private Vec3 _color = new(0.8f, 0.8f, 0.8f);
    private string? _animationClip;
    private bool _animationPlaying = true;
    private float _animationSpeed = 1f;

    public event PropertyChangedEventHandler? PropertyChanged;

    public string Name { get => _name; set => Set(ref _name, value); }
    public string? MeshPath { get => _meshPath; set => Set(ref _meshPath, value); }
    public string? TexturePath { get => _texturePath; set => Set(ref _texturePath, value); }
    public Vec3 Position { get => _position; set => Set(ref _position, value); }

    /// <summary>(pitch X, yaw Y, roll Z) 라디안.</summary>
    public Vec3 Rotation { get => _rotation; set => Set(ref _rotation, value); }
    public Vec3 Scale { get => _scale; set => Set(ref _scale, value); }
    public Vec3 Color { get => _color; set => Set(ref _color, value); }

    /// <summary>재생할 클립 이름(glTF 애니메이션 이름). null이면 레스트 포즈.</summary>
    public string? AnimationClip { get => _animationClip; set => Set(ref _animationClip, value); }

    public bool AnimationPlaying { get => _animationPlaying; set => Set(ref _animationPlaying, value); }

    public float AnimationSpeed { get => _animationSpeed; set => Set(ref _animationSpeed, value); }

    public SceneNode? Parent { get; internal set; }
    public ObservableCollection<SceneNode> Children { get; } = [];

    public override string ToString() => Name;

    private void Set<T>(ref T field, T value, [CallerMemberName] string? property = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
            return;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(property));
    }
}
