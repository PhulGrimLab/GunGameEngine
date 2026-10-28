namespace GunGame.Interop;

/// <summary>클립 채널이 건드리는 본 속성. 값은 엔진 C API의 path 번호와 같다.</summary>
public enum ChannelPath
{
    Translation = 0,
    Rotation = 1,  // 쿼터니언 xyzw
    Scale = 2,
}

/// <param name="Times">엄격히 증가, [0, duration] 안</param>
/// <param name="Values">키당 3개(이동/스케일) 또는 4개(회전)</param>
public sealed record ClipChannelData(int Bone, ChannelPath Path, float[] Times, float[] Values);

public sealed record ClipData(string Name, float Duration, IReadOnlyList<ClipChannelData> Channels);

/// <summary>본은 부모가 자식보다 앞에 오도록 정렬되어 있어야 한다(Parents[i] == -1 또는 &lt; i).</summary>
public sealed record SkeletonData(
    int[] Parents,
    float[] InverseBind,      // 본당 16 float, 행 벡터 규약(glTF 열 우선 배열과 같은 배치)
    float[] RestTranslation,  // 본당 3
    float[] RestRotation,     // 본당 4 (xyzw)
    float[] RestScale)        // 본당 3
{
    public int BoneCount => Parents.Length;
}

/// <summary>임포터가 만든 모델: 스키닝(또는 정적) 메시 + 선택적 스켈레톤 + 클립들.</summary>
public sealed class ImportedModel
{
    public required float[] Vertices { get; init; }    // 정점당 8 float
    public required uint[] Indices { get; init; }
    public uint[]? Joints { get; init; }               // 정점당 4 (스키닝일 때만)
    public float[]? Weights { get; init; }             // 정점당 4
    public SkeletonData? Skeleton { get; init; }
    public required IReadOnlyList<ClipData> Clips { get; init; }
    public required IReadOnlyList<string> Warnings { get; init; }
}
