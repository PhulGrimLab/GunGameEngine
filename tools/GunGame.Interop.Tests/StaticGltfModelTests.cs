using System.Numerics;
using System.Text.Json;
using GunGame.Interop;

namespace GunGame.Interop.Tests;

/// <summary>
/// 스킨이 없는 실제 모델(CesiumMilkTruck: Blender glTF I/O 1.0.5가 내보낸 파일, CC-BY 4.0)로 정적 메시 경로를 검증한다. 방 모델 같은
/// 레벨 지오메트리가 이 경로를 쓴다: 노드 계층(회전 포함), 같은 메시를 쓰는 노드 두 개(인스턴싱), 프리미티브 여러 개, 재질/텍스처 여러 개,
/// 노드 애니메이션(바퀴). 기준값은 임포터를 쓰지 않고 glb 원본에서 규격대로 계산한다(RealGltfModelTests와 같은 독립 오라클).
/// </summary>
public class StaticGltfModelTests
{
    private const string Model = "CesiumMilkTruck";

    private sealed record Expected(Vector3[] Positions, Vector3[] Normals, Vector2[] Uvs, uint[] Indices);

    /// <summary>노드 번호 순서(임포터와 같은 순서), 노드의 프리미티브 순서로 월드 변환을 적용한 정점/삼각형.</summary>
    private static Expected Oracle(RealGltfModelTests.RawGlb g)
    {
        JsonElement nodes = g.Json.GetProperty("nodes");
        var positions = new List<Vector3>();
        var normals = new List<Vector3>();
        var uvs = new List<Vector2>();
        var indices = new List<uint>();
        for (int node = 0; node < nodes.GetArrayLength(); node++)
        {
            if (!nodes[node].TryGetProperty("mesh", out JsonElement meshIndex))
                continue;
            Matrix4x4 world = Matrix4x4.Identity;
            for (int n = node; n >= 0; n = g.ParentOf(n))
                world *= RealGltfModelTests.Local(g.RestTrs(n));
            Assert.True(Matrix4x4.Invert(world, out Matrix4x4 inverse));
            Matrix4x4 normalMatrix = Matrix4x4.Transpose(inverse);
            bool mirrored = world.GetDeterminant() < 0;

            foreach (JsonElement prim in g.Json.GetProperty("meshes")[meshIndex.GetInt32()].GetProperty("primitives").EnumerateArray())
            {
                JsonElement attrs = prim.GetProperty("attributes");
                double[][] p = g.Read(attrs.GetProperty("POSITION").GetInt32());
                double[][] nrm = g.Read(attrs.GetProperty("NORMAL").GetInt32());
                double[][] uv = g.Read(attrs.GetProperty("TEXCOORD_0").GetInt32());
                uint baseVertex = (uint)positions.Count;
                for (int v = 0; v < p.Length; v++)
                {
                    positions.Add(Vector3.Transform(new Vector3((float)p[v][0], (float)p[v][1], (float)p[v][2]), world));
                    normals.Add(Vector3.Normalize(Vector3.TransformNormal(new Vector3((float)nrm[v][0], (float)nrm[v][1], (float)nrm[v][2]), normalMatrix)));
                    uvs.Add(new Vector2((float)uv[v][0], (float)uv[v][1]));
                }
                double[][] idx = g.Read(prim.GetProperty("indices").GetInt32());
                for (int t = 0; t < idx.Length; t += 3)
                {
                    indices.Add((uint)idx[t][0] + baseVertex);
                    indices.Add((uint)idx[t + (mirrored ? 2 : 1)][0] + baseVertex);
                    indices.Add((uint)idx[t + (mirrored ? 1 : 2)][0] + baseVertex);
                }
            }
        }
        return new Expected([.. positions], [.. normals], [.. uvs], [.. indices]);
    }

    [Fact]
    public void RealStaticModel_VerticesNormalsUvsAndTriangles_MatchTheSpec()
    {
        string path = RealGltfModelTests.ModelPath(Model);
        Expected expected = Oracle(new RealGltfModelTests.RawGlb(path));
        ImportedModel actual = GltfImporter.Import(path);

        // 인스턴싱: 바퀴 메시(정점 828개)를 노드 두 개가 쓰므로 앞의 두 덩어리는 같은 모양이 서로 다른 자리에 있다. 그 뒤는 트럭 프리미티브 3개.
        Assert.True(expected.Positions.Length > 828 * 2, $"vertices {expected.Positions.Length}");
        Vector3 Center(int from) => expected.Positions.Skip(from).Take(828).Aggregate(Vector3.Zero, (a, p) => a + p) / 828;
        Assert.True(Vector3.Distance(Center(0), Center(828)) > 2f, "the two wheel instances sit at different places");
        Assert.Equal(expected.Positions.Length, actual.Vertices.Length / 8);
        Assert.Equal(expected.Indices, actual.Indices);   // 삼각형 개수와 감김(미러 노드면 뒤집힘)까지 같다
        Assert.Null(actual.Skeleton);

        for (int v = 0; v < expected.Positions.Length; v++)
        {
            var position = new Vector3(actual.Vertices[v * 8], actual.Vertices[v * 8 + 1], actual.Vertices[v * 8 + 2]);
            var normal = new Vector3(actual.Vertices[v * 8 + 3], actual.Vertices[v * 8 + 4], actual.Vertices[v * 8 + 5]);
            var uv = new Vector2(actual.Vertices[v * 8 + 6], actual.Vertices[v * 8 + 7]);
            Assert.True(Vector3.Distance(expected.Positions[v], position) < 1e-4f, $"vertex {v}: position {position}, spec {expected.Positions[v]}");
            Assert.True(Vector3.Distance(expected.Normals[v], normal) < 1e-3f, $"vertex {v}: normal {normal}, spec {expected.Normals[v]}");
            Assert.True(Vector2.Distance(expected.Uvs[v], uv) < 1e-6f, $"vertex {v}: uv {uv}, spec {expected.Uvs[v]}");   // 엔진도 UV 원점이 좌상단이라 뒤집지 않는다
        }
    }

    [Fact]
    public void RealStaticModel_TrianglesWindCounterClockwise_AgainstTheirVertexNormals()
    {
        // 감김 방향의 독립 확인: 면 법선(반시계 감김의 외적)이 정점 법선과 같은 쪽을 봐야 앞면이다.
        ImportedModel m = GltfImporter.Import(RealGltfModelTests.ModelPath(Model));
        int front = 0, back = 0;
        for (int t = 0; t < m.Indices.Length; t += 3)
        {
            Vector3 P(uint i) => new(m.Vertices[i * 8], m.Vertices[i * 8 + 1], m.Vertices[i * 8 + 2]);
            Vector3 N(uint i) => new(m.Vertices[i * 8 + 3], m.Vertices[i * 8 + 4], m.Vertices[i * 8 + 5]);
            Vector3 face = Vector3.Cross(P(m.Indices[t + 1]) - P(m.Indices[t]), P(m.Indices[t + 2]) - P(m.Indices[t]));
            if (face.LengthSquared() < 1e-12f)
                continue;   // 퇴화 삼각형
            float d = Vector3.Dot(face, N(m.Indices[t]) + N(m.Indices[t + 1]) + N(m.Indices[t + 2]));
            if (d > 0) front++; else back++;
        }
        Assert.True(front > 0 && back * 100 <= front, $"front-facing {front}, back-facing {back}");   // 99% 이상이 앞면(매끈한 곡면의 극소수 예외 허용)
    }

    [Fact]
    public void RealStaticModel_SitsUprightOnTheGround_AccordingToTheSpecFormula()
    {
        // 오라클 자체 확인: 트럭은 바퀴가 바닥(y=0 부근)에 닿고 위(+Y)로 서 있어야 한다.
        Expected e = Oracle(new RealGltfModelTests.RawGlb(RealGltfModelTests.ModelPath(Model)));
        Vector3 min = e.Positions.Aggregate(new Vector3(float.MaxValue), Vector3.Min), max = e.Positions.Aggregate(new Vector3(float.MinValue), Vector3.Max);
        Vector3 size = max - min;
        Assert.InRange(min.Y, -0.1f, 0.1f);
        Assert.True(size.Y < size.X || size.Y < size.Z, $"a truck is longer than it is tall: size {size}");
        Assert.True(size.Y > 1.5f && size.Y < 3.5f, $"size {size}");
    }

    [Fact]
    public void RealStaticModel_DropsNodeAnimationAndMaterials_AndSaysSo()
    {
        ImportedModel m = GltfImporter.Import(RealGltfModelTests.ModelPath(Model));
        Assert.Empty(m.Clips);   // 바퀴 회전은 스킨이 아닌 노드 애니메이션이라 지원하지 않는다
        Assert.Contains(m.Warnings, w => w.Contains("animations ignored because the file has no skin"));
        // 재질/텍스처(4개/2개)는 가져오지 않는다: 메시는 하나로 합쳐지고 텍스처는 씬 노드가 따로 지정한다(문서화된 한계). 그래서 경고한다.
        Assert.Contains(m.Warnings, w => w.Contains("uses 4 materials") && w.Contains("texture atlas"));
        Assert.Equal(2, m.Warnings.Count);   // 노드 애니메이션 경고와 재질 경고, 그 밖의 경고는 없다
    }

    [Theory]
    [InlineData("RiggedSimple")]
    [InlineData("CesiumMan")]
    [InlineData("Fox")]
    public void SingleMaterialModels_GetNoMaterialWarning(string model)
    {
        Assert.DoesNotContain(GltfImporter.Import(RealGltfModelTests.ModelPath(model)).Warnings, w => w.Contains("materials"));
    }
}
