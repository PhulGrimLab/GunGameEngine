using System.Numerics;
using System.Text.Json;
using GunGame.Interop;

namespace GunGame.Interop.Tests;

/// <summary>
/// 실제 외부 glb(assets/external, Khronos glTF-Sample-Assets, CC-BY 4.0; 출처/라이선스는 assets/external/README.md)로 임포터와 엔진 스키닝을
/// 검증한다. 코드로 만든 glTF(GltfBuilder)로는 드러나지 않는 문제를 잡으려는 것이라, 기준값은 임포터 코드를 쓰지 않고 glb 원본을
/// 테스트 안에서 직접 읽어 glTF 규격 공식으로 계산한다(독립 오라클):
///   월드 정점 = sum(가중치 x 정점 x 역바인드[조인트] x 조인트의 월드 변환(조상 노드 포함)).
/// </summary>
public class RealGltfModelTests
{
    internal static string ModelPath(string name)
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            string candidate = Path.Combine(dir, "assets", "external", name, name + ".glb");
            if (File.Exists(candidate))
                return candidate;
        }
        throw new FileNotFoundException($"assets/external/{name}/{name}.glb was not found above the test binaries");
    }

    // ---- 독립 오라클: glb 원본 읽기 ----
    internal sealed class RawGlb
    {
        public readonly JsonElement Json;
        private readonly byte[] _bin;
        private readonly JsonDocument _doc;

        public RawGlb(string path)
        {
            byte[] d = File.ReadAllBytes(path);
            int offset = 12;
            string? json = null;
            byte[]? bin = null;
            while (offset < d.Length)
            {
                int length = BitConverter.ToInt32(d, offset);
                uint type = BitConverter.ToUInt32(d, offset + 4);
                if (type == 0x4E4F534A) json = System.Text.Encoding.UTF8.GetString(d, offset + 8, length);
                else if (type == 0x004E4942) bin = d[(offset + 8)..(offset + 8 + length)];
                offset += 8 + length;
            }
            _doc = JsonDocument.Parse(json!);
            Json = _doc.RootElement;
            _bin = bin!;
        }

        /// <summary>접근자를 요소당 components개 double로 읽는다(정규화 정수 포함).</summary>
        public double[][] Read(int accessorIndex)
        {
            JsonElement a = Json.GetProperty("accessors")[accessorIndex];
            JsonElement view = Json.GetProperty("bufferViews")[a.GetProperty("bufferView").GetInt32()];
            int count = a.GetProperty("count").GetInt32();
            int componentType = a.GetProperty("componentType").GetInt32();
            int components = a.GetProperty("type").GetString() switch { "SCALAR" => 1, "VEC2" => 2, "VEC3" => 3, "VEC4" => 4, "MAT4" => 16, var t => throw new InvalidOperationException(t) };
            int size = componentType switch { 5120 or 5121 => 1, 5122 or 5123 => 2, _ => 4 };
            int stride = view.TryGetProperty("byteStride", out JsonElement bs) ? bs.GetInt32() : components * size;
            int start = (view.TryGetProperty("byteOffset", out JsonElement vo) ? vo.GetInt32() : 0) + (a.TryGetProperty("byteOffset", out JsonElement ao) ? ao.GetInt32() : 0);
            bool normalized = a.TryGetProperty("normalized", out JsonElement n) && n.GetBoolean();
            var result = new double[count][];
            for (int e = 0; e < count; e++)
            {
                result[e] = new double[components];
                for (int c = 0; c < components; c++)
                {
                    int at = start + e * stride + c * size;
                    double v = componentType switch
                    {
                        5126 => BitConverter.ToSingle(_bin, at),
                        5125 => BitConverter.ToUInt32(_bin, at),
                        5123 => BitConverter.ToUInt16(_bin, at),
                        5122 => BitConverter.ToInt16(_bin, at),
                        5121 => _bin[at],
                        _ => (sbyte)_bin[at],
                    };
                    if (normalized)
                        v = componentType switch { 5121 => v / 255, 5123 => v / 65535, 5120 => Math.Max(v / 127, -1), 5122 => Math.Max(v / 32767, -1), _ => v };
                    result[e][c] = v;
                }
            }
            return result;
        }

        public (Vector3 T, Quaternion R, Vector3 S) RestTrs(int node)
        {
            JsonElement n = Json.GetProperty("nodes")[node];
            if (n.TryGetProperty("matrix", out JsonElement m))
            {
                float[] f = m.EnumerateArray().Select(x => x.GetSingle()).ToArray();
                var matrix = new Matrix4x4(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
                Assert.True(Matrix4x4.Decompose(matrix, out Vector3 s, out Quaternion r, out Vector3 t));
                return (t, r, s);
            }
            Vector3 T = Vector3.Zero, S = Vector3.One;
            Quaternion R = Quaternion.Identity;
            if (n.TryGetProperty("translation", out JsonElement te)) { float[] f = te.EnumerateArray().Select(x => x.GetSingle()).ToArray(); T = new(f[0], f[1], f[2]); }
            if (n.TryGetProperty("rotation", out JsonElement re)) { float[] f = re.EnumerateArray().Select(x => x.GetSingle()).ToArray(); R = new(f[0], f[1], f[2], f[3]); }
            if (n.TryGetProperty("scale", out JsonElement se)) { float[] f = se.EnumerateArray().Select(x => x.GetSingle()).ToArray(); S = new(f[0], f[1], f[2]); }
            return (T, R, S);
        }

        public int ParentOf(int node)
        {
            int index = 0;
            foreach (JsonElement n in Json.GetProperty("nodes").EnumerateArray())
            {
                if (n.TryGetProperty("children", out JsonElement c) && c.EnumerateArray().Any(x => x.GetInt32() == node))
                    return index;
                index++;
            }
            return -1;
        }
    }

    internal static Matrix4x4 Local((Vector3 T, Quaternion R, Vector3 S) trs) =>
        Matrix4x4.CreateScale(trs.S) * Matrix4x4.CreateFromQuaternion(Quaternion.Normalize(trs.R)) * Matrix4x4.CreateTranslation(trs.T);

    private static Vector4 Sample(double[] times, double[][] values, double t)
    {
        static Vector4 V(double[] a) => new((float)a[0], (float)a[1], (float)a[2], a.Length > 3 ? (float)a[3] : 0);
        if (t <= times[0]) return V(values[0]);
        if (t >= times[^1]) return V(values[^1]);
        int i = 0;
        while (times[i + 1] < t) i++;
        float f = (float)((t - times[i]) / (times[i + 1] - times[i]));
        if (values[0].Length == 4)
        {
            var a = new Quaternion((float)values[i][0], (float)values[i][1], (float)values[i][2], (float)values[i][3]);
            var b = new Quaternion((float)values[i + 1][0], (float)values[i + 1][1], (float)values[i + 1][2], (float)values[i + 1][3]);
            Quaternion q = Quaternion.Slerp(Quaternion.Normalize(a), Quaternion.Normalize(b), f);
            return new Vector4(q.X, q.Y, q.Z, q.W);
        }
        return Vector4.Lerp(V(values[i]), V(values[i + 1]), f);
    }

    /// <summary>규격 공식으로 계산한 모든 정점의 월드 위치. animation이 null이면 레스트 포즈.</summary>
    private static Vector3[] ReferencePositions(RawGlb g, int? animation, float time)
    {
        JsonElement skin = g.Json.GetProperty("skins")[0];
        int[] joints = skin.GetProperty("joints").EnumerateArray().Select(x => x.GetInt32()).ToArray();
        double[][] ibmRaw = g.Read(skin.GetProperty("inverseBindMatrices").GetInt32());
        Matrix4x4[] ibm = ibmRaw.Select(m => { float[] f = m.Select(x => (float)x).ToArray(); return new Matrix4x4(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]); }).ToArray();

        var trs = new Dictionary<int, (Vector3 T, Quaternion R, Vector3 S)>();
        for (int n = 0; n < g.Json.GetProperty("nodes").GetArrayLength(); n++)
            trs[n] = g.RestTrs(n);
        if (animation is int ai)
        {
            JsonElement anim = g.Json.GetProperty("animations")[ai];
            foreach (JsonElement ch in anim.GetProperty("channels").EnumerateArray())
            {
                int node = ch.GetProperty("target").GetProperty("node").GetInt32();
                JsonElement sampler = anim.GetProperty("samplers")[ch.GetProperty("sampler").GetInt32()];
                double[] times = g.Read(sampler.GetProperty("input").GetInt32()).Select(x => x[0]).ToArray();
                double[][] values = g.Read(sampler.GetProperty("output").GetInt32());
                Vector4 v = Sample(times, values, time);
                (Vector3 T, Quaternion R, Vector3 S) cur = trs[node];
                switch (ch.GetProperty("target").GetProperty("path").GetString())
                {
                    case "translation": cur.T = new Vector3(v.X, v.Y, v.Z); break;
                    case "rotation": cur.R = new Quaternion(v.X, v.Y, v.Z, v.W); break;
                    case "scale": cur.S = new Vector3(v.X, v.Y, v.Z); break;
                }
                trs[node] = cur;
            }
        }

        Matrix4x4 World(int node)
        {
            Matrix4x4 w = Matrix4x4.Identity;
            for (int n = node; n >= 0; n = g.ParentOf(n))
                w *= Local(trs[n]);
            return w;
        }
        Matrix4x4[] jointMatrix = joints.Select((node, j) => ibm[j] * World(node)).ToArray();

        JsonElement prim = g.Json.GetProperty("meshes")[0].GetProperty("primitives")[0];
        JsonElement attrs = prim.GetProperty("attributes");
        double[][] pos = g.Read(attrs.GetProperty("POSITION").GetInt32());
        double[][] jointIdx = g.Read(attrs.GetProperty("JOINTS_0").GetInt32());
        double[][] weight = g.Read(attrs.GetProperty("WEIGHTS_0").GetInt32());
        var result = new Vector3[pos.Length];
        for (int v = 0; v < pos.Length; v++)
        {
            var p = new Vector3((float)pos[v][0], (float)pos[v][1], (float)pos[v][2]);
            Vector3 sum = Vector3.Zero;
            for (int k = 0; k < 4; k++)
                if (weight[v][k] > 0)
                    sum += (float)weight[v][k] * Vector3.Transform(p, jointMatrix[(int)jointIdx[v][k]]);
            result[v] = sum;
        }
        return result;
    }

    /// <summary>임포터 + 엔진이 만든 같은 포즈의 월드 위치(엔진의 본 행렬 x 역바인드로 정점을 직접 스키닝).</summary>
    internal static Vector3[] EnginePositions(ImportedModel m, int? clip, float time)
    {
        using var r = new EngineRenderer(64, 48, useWarp: true);
        int mesh = r.CreateSkinnedMesh(m.Vertices, m.Joints!, m.Weights!, m.Indices);
        int skeleton = r.CreateSkeleton(m.Skeleton!);
        int obj = r.CreateObject(mesh);
        r.SetObjectSkeleton(obj, skeleton);
        if (clip is int ci)
            r.SetObjectPose(obj, r.CreateClip(m.Clips[ci]), time);
        else
            r.SetObjectPose(obj, 0, 0);
        int bones = m.Skeleton!.BoneCount;
        float[] pose = r.GetObjectPose(obj, bones);
        Matrix4x4 Mat(float[] f, int i) => new(f[i * 16], f[i * 16 + 1], f[i * 16 + 2], f[i * 16 + 3], f[i * 16 + 4], f[i * 16 + 5], f[i * 16 + 6], f[i * 16 + 7],
                                               f[i * 16 + 8], f[i * 16 + 9], f[i * 16 + 10], f[i * 16 + 11], f[i * 16 + 12], f[i * 16 + 13], f[i * 16 + 14], f[i * 16 + 15]);
        Matrix4x4[] skin = Enumerable.Range(0, bones).Select(i => Mat(m.Skeleton.InverseBind, i) * Mat(pose, i)).ToArray();

        int count = m.Vertices.Length / 8;
        var result = new Vector3[count];
        for (int v = 0; v < count; v++)
        {
            var p = new Vector3(m.Vertices[v * 8], m.Vertices[v * 8 + 1], m.Vertices[v * 8 + 2]);
            Vector3 sum = Vector3.Zero;
            for (int k = 0; k < 4; k++)
            {
                float w = m.Weights![v * 4 + k];
                if (w > 0)
                    sum += w * Vector3.Transform(p, skin[m.Joints![v * 4 + k]]);
            }
            result[v] = sum;
        }
        return result;
    }

    private static (Vector3 Min, Vector3 Max) Bounds(Vector3[] points) =>
        (points.Aggregate(new Vector3(float.MaxValue), Vector3.Min), points.Aggregate(new Vector3(float.MinValue), Vector3.Max));

    // ---- 규격과의 일치: 레스트 포즈와 애니메이션 중간 시점 ----
    [Theory]
    [InlineData("RiggedSimple", -1, 0f)]
    [InlineData("RiggedSimple", 0, 0.37f)]
    [InlineData("CesiumMan", -1, 0f)]
    [InlineData("CesiumMan", 0, 0.37f)]
    [InlineData("CesiumMan", 0, 0.81f)]
    [InlineData("Fox", -1, 0f)]
    [InlineData("Fox", 0, 0.37f)]   // Survey
    [InlineData("Fox", 1, 0.5f)]    // Walk
    [InlineData("Fox", 2, 0.9f)]    // Run
    public void RealModel_PosedVertices_MatchTheGltfSpecFormula(string model, int animation, float fraction)
    {
        string path = ModelPath(model);
        var raw = new RawGlb(path);
        ImportedModel imported = GltfImporter.Import(path);

        int? anim = animation < 0 ? null : animation;
        float time = anim is null ? 0 : fraction * imported.Clips[animation].Duration;
        Vector3[] expected = ReferencePositions(raw, anim, time);
        Vector3[] actual = EnginePositions(imported, anim, time);

        Assert.Equal(expected.Length, actual.Length);
        (Vector3 min, Vector3 max) = Bounds(expected);
        float tolerance = 1e-3f * Math.Max(1f, (max - min).Length());
        float worst = 0;
        int worstVertex = 0;
        for (int v = 0; v < expected.Length; v++)
        {
            float e = Vector3.Distance(expected[v], actual[v]);
            if (e > worst) { worst = e; worstVertex = v; }
        }
        Assert.True(worst <= tolerance,
            $"{model} anim={animation} t={time:0.###}: vertex {worstVertex} is {worst:0.####} away from the spec result {expected[worstVertex]} (engine {actual[worstVertex]}), tolerance {tolerance:0.####}");
    }

    // ---- 오라클 자체가 맞는지(자기 확인): 사람은 서 있어야 한다 ----
    [Fact]
    public void CesiumMan_StandsUpright_AccordingToTheSpecFormula()
    {
        Vector3[] world = ReferencePositions(new RawGlb(ModelPath("CesiumMan")), null, 0);
        (Vector3 min, Vector3 max) = Bounds(world);
        Vector3 size = max - min;
        Assert.InRange(size.Y, 1.4f, 1.6f);       // 키 약 1.5m가 Y(위)로 서 있다
        Assert.True(size.Y > size.X && size.Y > size.Z, $"size {size}");
        Assert.InRange(min.Y, -0.05f, 0.05f);     // 발이 바닥(y=0)에 있다
    }
}
