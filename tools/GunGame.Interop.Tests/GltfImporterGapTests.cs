using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using GunGame.Interop;

namespace GunGame.Interop.Tests;

/// <summary>
/// C# 뮤테이션 테스트에서 살아남은 glTF 임포터 변이를 겨냥한 테스트(문서 doc/03 "C# 뮤테이션" 참고).
/// GLB 컨테이너(헤더/청크) 검증, 접근자 범위 검사, 스킨 기본값(역바인드 없음, 본 수 한계), 여러 프리미티브의 인덱스 오프셋,
/// 법선 생성처럼 기존 테스트가 한 가지 모양만 확인하던 곳을 경계/비대칭 값으로 확인한다. 오류는 사유까지 단언한다.
/// </summary>
public class GltfImporterGapTests
{
    private const uint GlbMagic = 0x46546C67, JsonChunk = 0x4E4F534A, BinChunk = 0x004E4942;

    private static string Patched(TestAssets assets, GltfBuilder builder, string name, Action<JsonNode> patch)
    {
        builder.Glb = false;
        string path = builder.Write(assets.Directory, name);
        JsonNode doc = JsonNode.Parse(File.ReadAllText(path))!;
        patch(doc);
        File.WriteAllText(path, doc.ToJsonString());
        return path;
    }

    private static JsonNode Attribute(JsonNode doc, string attribute) =>
        doc["accessors"]![doc["meshes"]![0]!["primitives"]![0]!["attributes"]![attribute]!.GetValue<int>()]!;

    private static JsonNode IndicesAccessor(JsonNode doc) =>
        doc["accessors"]![doc["meshes"]![0]!["primitives"]![0]!["indices"]!.GetValue<int>()]!;

    private static JsonNode ViewOf(JsonNode doc, JsonNode accessor) => doc["bufferViews"]![accessor["bufferView"]!.GetValue<int>()]!;

    private static string Reason(string path) => Assert.Throws<GltfException>(() => GltfImporter.Import(path)).Message;

    // ---- GLB 컨테이너 ----
    private static List<(uint Type, byte[] Data)> ChunksOf(byte[] glb)
    {
        var chunks = new List<(uint, byte[])>();
        for (int offset = 12; offset + 8 <= glb.Length;)
        {
            uint length = BitConverter.ToUInt32(glb, offset), type = BitConverter.ToUInt32(glb, offset + 4);
            chunks.Add((type, glb[(offset + 8)..(offset + 8 + (int)length)]));
            offset += 8 + (int)length;
        }
        return chunks;
    }

    private static byte[] GlbFile(uint version, IEnumerable<(uint Type, byte[] Data)> chunks)
    {
        var list = chunks.ToList();
        using var ms = new MemoryStream();
        using var w = new BinaryWriter(ms);
        w.Write(GlbMagic); w.Write(version); w.Write((uint)(12 + list.Sum(c => 8 + c.Data.Length)));
        foreach ((uint type, byte[] data) in list)
        {
            w.Write((uint)data.Length); w.Write(type); w.Write(data);
        }
        return ms.ToArray();
    }

    private static string WriteBytes(TestAssets assets, string name, byte[] bytes)
    {
        string path = assets.Path_(name + ".glb");
        File.WriteAllBytes(path, bytes);
        return path;
    }

    [Fact]
    public void GlbContainer_RejectsBrokenHeadersAndChunks_WithTheirOwnReason()
    {
        using var assets = new TestAssets();
        byte[] valid = File.ReadAllBytes(new GltfBuilder().Write(assets.Directory, "valid"));
        var chunks = ChunksOf(valid);

        byte[] shortFile = valid[..19];
        Assert.Contains("truncated", Reason(WriteBytes(assets, "short", shortFile)));
        Assert.Contains("truncated", Reason(WriteBytes(assets, "magic_only", Encoding.ASCII.GetBytes("glTF"))));  // 길이 4: 매직만 있다

        Assert.Contains("version 2 required", Reason(WriteBytes(assets, "v1", GlbFile(1, chunks))));

        byte[] lengthLies = (byte[])valid.Clone();
        BitConverter.GetBytes((uint)valid.Length + 4).CopyTo(lengthLies, 8);
        Assert.Contains("GLB length exceeds file size", Reason(WriteBytes(assets, "length_lies", lengthLies)));

        byte[] chunkLies = (byte[])valid.Clone();
        BitConverter.GetBytes(0xFFFFFFF0u).CopyTo(chunkLies, 12);  // 첫 청크 길이가 파일보다 훨씬 크다
        Assert.Contains("GLB chunk exceeds file size", Reason(WriteBytes(assets, "chunk_lies", chunkLies)));

        byte[] chunkOneOver = (byte[])valid.Clone();
        BitConverter.GetBytes((uint)(valid.Length - 12 - 8 + 4)).CopyTo(chunkOneOver, 12);  // 남은 길이보다 정확히 4바이트 크다
        Assert.Contains("GLB chunk exceeds file size", Reason(WriteBytes(assets, "chunk_over", chunkOneOver)));

        Assert.Contains("no JSON chunk", Reason(WriteBytes(assets, "no_json", GlbFile(2, chunks.Where(c => c.Type == BinChunk)))));
    }

    [Fact]
    public void ShortOrEmptyContent_IsParsedAsJson_NotAsGlb()
    {
        using var assets = new TestAssets();
        // 길이 2("gl")는 매직을 읽을 수 없다: 바이트를 인덱싱하지 않고 JSON으로 읽다가 JSON 오류가 되어야 한다.
        string tiny = WriteBytes(assets, "tiny", Encoding.ASCII.GetBytes("gl"));
        var ex = Assert.Throws<GltfException>(() => GltfImporter.Import(tiny));
        Assert.IsAssignableFrom<JsonException>(ex.InnerException);

        // 헤더(12) + 빈 JSON 청크 헤더(8) = 정확히 20바이트: "잘림"이 아니라 비어 있는 JSON이다.
        byte[] exactly20 = GlbFile(2, [(JsonChunk, [])]);
        Assert.Equal(20, exactly20.Length);
        var empty = Assert.Throws<GltfException>(() => GltfImporter.Import(WriteBytes(assets, "exactly20", exactly20)));
        Assert.IsAssignableFrom<JsonException>(empty.InnerException);
        Assert.DoesNotContain("truncated", empty.Message);
    }

    [Fact]
    public void GlbContainer_UsesTheFirstJsonAndFirstBinChunk_IgnoringLaterDuplicates()
    {
        using var assets = new TestAssets();
        string plainPath = new GltfBuilder().Write(assets.Directory, "plain");
        ImportedModel plain = GltfImporter.Import(plainPath);
        var chunks = ChunksOf(File.ReadAllBytes(plainPath));

        var withDuplicates = chunks.Concat(
        [
            (JsonChunk, Encoding.UTF8.GetBytes("{ this is not json")),   // 두 번째 JSON 청크가 첫 번째를 덮어쓰면 안 된다
            (BinChunk, new byte[chunks.First(c => c.Type == BinChunk).Data.Length]),  // 두 번째 BIN 청크(0으로 채움)도 마찬가지
        ]);
        ImportedModel result = GltfImporter.Import(WriteBytes(assets, "dups", GlbFile(2, withDuplicates)));

        Assert.Equal(plain.Vertices, result.Vertices);
        Assert.Equal(plain.Indices, result.Indices);
        Assert.Equal(plain.Skeleton!.InverseBind, result.Skeleton!.InverseBind);
    }

    // ---- 접근자 범위 ----
    [Fact]
    public void AccessorRangeChecks_RejectEachWayOfReadingOutsideTheView()
    {
        using var assets = new TestAssets();
        var cases = new (string Name, Action<JsonNode> Patch)[]
        {
            // 6개 정점이 정확히 뷰를 채운다: 7개째는 뷰 밖이다(개수를 크게 잡는 것만으로는 구별되지 않는 경계).
            ("one past the end", d => Attribute(d, "POSITION")["count"] = 7),
            ("stride smaller than element", d => ViewOf(d, Attribute(d, "POSITION"))["byteStride"] = 4),
            ("negative view offset", d => ViewOf(d, Attribute(d, "POSITION"))["byteOffset"] = -4),
            // 뷰는 크다고 주장하지만 실제 버퍼는 더 짧다.
            ("view longer than the buffer", d =>
            {
                IndicesAccessor(d)["count"] = 999;
                ViewOf(d, IndicesAccessor(d))["byteLength"] = 100000;
            }),
        };
        foreach ((string name, Action<JsonNode> patch) in cases)
            Assert.Contains("reads outside", Reason(Patched(assets, new GltfBuilder(), name.Replace(' ', '_'), patch)));
    }

    [Fact]
    public void AccessorByteOffset_AddsToTheViewOffset()
    {
        using var assets = new TestAssets();
        // 인덱스 [0,1,3, 0,3,2, 2,3,5, 2,5,4]에서 첫 삼각형을 건너뛴다(ushort 3개 = 6바이트).
        string path = Patched(assets, new GltfBuilder(), "offset", d =>
        {
            IndicesAccessor(d)["byteOffset"] = 6;
            IndicesAccessor(d)["count"] = 9;
        });
        Assert.Equal<uint>([0, 3, 2, 2, 3, 5, 2, 5, 4], GltfImporter.Import(path).Indices);
    }

    [Fact]
    public void ZeroAndNegativeCounts_AreDifferentCases()
    {
        using var assets = new TestAssets();
        Assert.Contains("no triangle geometry", Reason(Patched(assets, new GltfBuilder(), "zero", d => IndicesAccessor(d)["count"] = 0)));
        Assert.Contains("negative count", Reason(Patched(assets, new GltfBuilder(), "negative", d => IndicesAccessor(d)["count"] = -3)));
    }

    [Fact]
    public void IndexEqualToVertexCount_IsRejected()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "index_equals_count", d =>
        {
            foreach (string attribute in new[] { "POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0" })
                Attribute(d, attribute)["count"] = 5;  // 가장 큰 인덱스 5가 정점 수와 같다(0..4만 유효)
        });
        Assert.Contains("primitive indices are invalid", Reason(path));
    }

    // ---- 노드 계층 ----
    [Fact]
    public void ChildIndex_MustBeInsideTheNodeList_ButZeroIsValid()
    {
        using var assets = new TestAssets();
        Assert.Contains("does not exist", Reason(Patched(assets, new GltfBuilder(), "child_at_count", d =>
            d["nodes"]![0]!["children"] = new JsonArray(1, d["nodes"]!.AsArray().Count))));  // 마지막 번호 + 1

        // 노드 0을 다른 노드의 자식으로 두는 것은 정상이다(번호 0을 "없음"으로 오해하면 안 된다).
        string zeroChild = Patched(assets, new GltfBuilder(), "child_zero", d =>
        {
            d["nodes"]![0]!.AsObject().Remove("children");
            d["nodes"]![1]!["children"] = new JsonArray(0, 2);
        });
        ImportedModel model = GltfImporter.Import(zeroChild);
        Assert.Equal([-1, 0, 1], model.Skeleton!.Parents);
    }

    // ---- 스킨 ----
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void JointRestTransforms_AreReadPerBone_InSkeletonOrder(bool reverseJointOrder)
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder { ReverseJointOrder = reverseJointOrder }, "rest", d =>
        {
            // 노드 1..3 = 본 0..2. 본마다 모든 성분이 서로 다른 값.
            float[][] t = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
            float[][] r = [[0, 0, 0.6f, 0.8f], [0.6f, 0, 0, 0.8f], [0, 0.6f, 0, 0.8f]];
            float[][] s = [[2, 3, 4], [5, 6, 7], [8, 9, 10]];
            for (int b = 0; b < 3; b++)
            {
                JsonNode node = d["nodes"]![b + 1]!;
                node["translation"] = new JsonArray(t[b].Select(v => (JsonNode)v).ToArray());
                node["rotation"] = new JsonArray(r[b].Select(v => (JsonNode)v).ToArray());
                node["scale"] = new JsonArray(s[b].Select(v => (JsonNode)v).ToArray());
            }
        });

        SkeletonData skeleton = GltfImporter.Import(path).Skeleton!;
        Assert.Equal([-1, 0, 1], skeleton.Parents);
        Assert.Equal([1f, 2, 3, 4, 5, 6, 7, 8, 9], skeleton.RestTranslation);
        Assert.Equal([0f, 0, 0.6f, 0.8f, 0.6f, 0, 0, 0.8f, 0, 0.6f, 0, 0.8f], skeleton.RestRotation);
        Assert.Equal([2f, 3, 4, 5, 6, 7, 8, 9, 10], skeleton.RestScale);
    }

    [Fact]
    public void SkinWithoutInverseBindMatrices_UsesIdentityForEveryBone()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "no_ibm", d => d["skins"]![0]!.AsObject().Remove("inverseBindMatrices"));

        float[] identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
        Assert.Equal(identity.Concat(identity).Concat(identity), GltfImporter.Import(path).Skeleton!.InverseBind);
    }

    [Fact]
    public void BoneLimit_Is64Inclusive()
    {
        using var assets = new TestAssets();
        ImportedModel atLimit = GltfImporter.Import(new GltfBuilder { Bones = 64 }.Write(assets.Directory, "bones64"));
        Assert.Equal(64, atLimit.Skeleton!.Parents.Length);

        Assert.Contains("at most 64", Reason(new GltfBuilder { Bones = 65 }.Write(assets.Directory, "bones65")));
    }

    [Fact]
    public void ArmatureTransform_AddsExactlyOneBone_NotOnePerDescendant_AndNoWarning()
    {
        using var assets = new TestAssets();
        ImportedModel model = GltfImporter.Import(new GltfBuilder { ArmatureTranslation = [5, 0, 0] }.Write(assets.Directory, "arm"));

        // 루트가 아닌 본의 조상(루트 본, 아르마추어)도 비항등 변환을 갖지만, 합성 본이 필요한 것은 루트 본 위쪽뿐이라 한 개다(3 + 1).
        Assert.Equal(4, model.Skeleton!.BoneCount);
        Assert.Empty(model.Warnings);

        // 항등 아마추어는 합성 본이 없다.
        Assert.Equal(3, GltfImporter.Import(new GltfBuilder().Write(assets.Directory, "plain")).Skeleton!.BoneCount);
    }

    [Fact]
    public void BoneLimit_CountsTheSyntheticArmatureBone_SoAnArmatureModelMayHaveAtMost63Joints()
    {
        using var assets = new TestAssets();
        ImportedModel atLimit = GltfImporter.Import(new GltfBuilder { Bones = 63, ArmatureTranslation = [0, 1, 0] }.Write(assets.Directory, "b63"));
        Assert.Equal(64, atLimit.Skeleton!.BoneCount);   // 63개 + 합성 본 = 한도 안

        string reason = Reason(new GltfBuilder { Bones = 64, ArmatureTranslation = [0, 1, 0] }.Write(assets.Directory, "b64"));
        Assert.Contains("64 joints plus 1 for the armature transform", reason);
        Assert.Contains("at most 64", reason);
    }

    // 첫 실제 조인트(본 0)가 클립으로 움직이게 한 모델: 정점 0(가중치 있음 = 본 0에 100%)과 "가중치 없음"이 같은 위치여야 한다.
    private static GltfBuilder SpinningFirstBone(float[]? armature, int unweightedVertex)
    {
        var builder = new GltfBuilder { ArmatureTranslation = armature, UnweightedVertex = unweightedVertex };
        builder.Animations.Clear();
        builder.KeyedAnimations.Add(("spin0", [0f, 1f], [(0, [0f, 1f])]));   // 본 0이 Z축으로 0 -> 1라디안
        return builder;
    }

    [Theory]
    [InlineData(false)]   // 아마추어 없음: 첫 실제 조인트가 본 0이라 엔진 규약("본 0에 고정")과 같다
    [InlineData(true)]    // 아마추어 있음: 본 0은 합성 본이므로 임포터가 첫 실제 조인트(본 1)에 묶어야 한다
    public void UnweightedVertices_MoveWithTheFirstRealJoint_AtRestAndWhileAnimated(bool withArmature)
    {
        using var assets = new TestAssets();
        float[]? armature = withArmature ? [0f, 5, 0] : null;
        ImportedModel weighted = GltfImporter.Import(SpinningFirstBone(armature, -1).Write(assets.Directory, "weighted" + withArmature));
        ImportedModel unweighted = GltfImporter.Import(SpinningFirstBone(armature, 0).Write(assets.Directory, "unweighted" + withArmature));

        int firstReal = withArmature ? 1 : 0;
        Assert.Equal<uint>([(uint)firstReal, 0, 0, 0], unweighted.Joints![..4]);
        Assert.Equal([1f, 0, 0, 0], unweighted.Weights![..4]);

        foreach ((int? clip, float time) in new (int?, float)[] { (null, 0f), (0, 0.5f), (0, 1f) })
        {
            Vector3[] expected = RealGltfModelTests.EnginePositions(weighted, clip, time);
            Vector3[] actual = RealGltfModelTests.EnginePositions(unweighted, clip, time);
            for (int v = 0; v < expected.Length; v++)
                Assert.True(Vector3.Distance(expected[v], actual[v]) < 1e-4f, $"vertex {v} at clip {clip} t={time}: expected {expected[v]}, got {actual[v]}");
        }
    }

    [Fact]
    public void NonFiniteJointWeights_AreRejectedWithAReason()
    {
        using var assets = new TestAssets();
        string reason = Reason(new GltfBuilder { NonFiniteWeight = true }.Write(assets.Directory, "nan_weight"));
        Assert.Contains("vertex 0 has a non-finite joint weight", reason);
    }

    [Fact]
    public void ArmatureTransformWithShear_IsRejectedWithAReason()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "shear_armature", d =>
        {
            // 회전한 노드(아르마추어) 위에 비균등 스케일 노드: 월드 = 회전 x 스케일은 전단이라 이동/회전/크기로 나타낼 수 없다.
            JsonArray nodes = d["nodes"]!.AsArray();
            nodes[0]!["rotation"] = new JsonArray(0f, 0f, 0.38268343f, 0.9238795f);
            int scaler = nodes.Count;
            nodes.Add(JsonNode.Parse("""{"name":"Scaler","scale":[2,1,1],"children":[0]}"""));
            d["scenes"]![0]!["nodes"]![0] = scaler;
        });
        string reason = Reason(path);
        Assert.Contains("cannot be expressed as translation/rotation/scale", reason);
        Assert.Contains("apply it in the modeling tool", reason);
    }

    [Fact]
    public void AnimationOnTheArmatureNode_IsReportedNotSilentlyDropped()
    {
        // Blender가 오브젝트(아마추어) 애니메이션을 내보내면 채널이 조인트가 아닌 노드를 가리킨다. 정적 월드만 쓰므로 경고로 알려야 한다.
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "armature_anim", d =>
            d["animations"]![0]!["channels"]![0]!["target"]!["node"] = 0);   // 노드 0 = 아르마추어
        ImportedModel model = GltfImporter.Import(path);
        Assert.Contains(model.Warnings, w => w.Contains("not a joint"));
        Assert.Empty(model.Clips);
        Assert.Contains(model.Warnings, w => w.Contains("no usable channels"));
    }

    // ---- 프리미티브 ----
    [Fact]
    public void SecondPrimitive_IndicesAreOffsetByTheFirstPrimitivesVertexCount()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder { Skinned = false }, "two_prims", d =>
        {
            var prims = d["meshes"]![0]!["primitives"]!.AsArray();
            prims.Add(JsonNode.Parse(prims[0]!.ToJsonString()));  // 같은 데이터를 한 번 더
        });

        ImportedModel model = GltfImporter.Import(path);
        Assert.Equal(12 * 8, model.Vertices.Length);  // 정점 6개 x 2
        Assert.Equal(24, model.Indices.Length);
        for (int i = 0; i < 12; i++)
            Assert.Equal(model.Indices[i] + 6, model.Indices[12 + i]);  // 두 번째 프리미티브는 정점 6개 뒤에서 시작한다
        Assert.Equal(model.Vertices[..48], model.Vertices[48..]);
    }

    [Fact]
    public void PrimitiveWithJointsButNoWeights_IsNotTreatedAsSkinned()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "joints_only", d =>
        {
            var prims = d["meshes"]![0]!["primitives"]!.AsArray();
            var copy = JsonNode.Parse(prims[0]!.ToJsonString())!;
            copy["attributes"]!.AsObject().Remove("WEIGHTS_0");  // JOINTS_0만 있다
            prims.Add(copy);
        });

        ImportedModel model = GltfImporter.Import(path);
        Assert.Contains(model.Warnings, w => w.Contains("non-skinned primitive was skipped"));
        Assert.Equal(12, model.Indices.Length);
    }

    [Fact]
    public void MissingNormals_AreGeneratedFromTheFaces_ForEveryVertex()
    {
        using var assets = new TestAssets();
        ImportedModel model = GltfImporter.Import(new GltfBuilder { Skinned = false, IncludeNormals = false }.Write(assets.Directory, "no_normals"));

        // 띠는 XY 평면에 있고 감김이 반시계라 면 법선은 +Z다. 정점 8개 float 중 3..5가 법선.
        for (int v = 0; v < 6; v++)
        {
            Assert.Equal(0f, model.Vertices[v * 8 + 3], 5);
            Assert.Equal(0f, model.Vertices[v * 8 + 4], 5);
            Assert.Equal(1f, model.Vertices[v * 8 + 5], 5);
        }
    }
    [Fact]
    public void GeneratedNormals_OfATube_PointAwayFromTheAxisAtEveryVertex()
    {
        // 정점마다 서로 다른 방향의 법선이 나와야 하므로(띠처럼 한 방향이 아님) 성분 인덱스/외적 부호 오류가 드러난다.
        using var assets = new TestAssets();
        ImportedModel model = GltfImporter.Import(new GltfBuilder { Skinned = false, IncludeNormals = false, Tube = true }.Write(assets.Directory, "tube_no_normals"));

        const int sides = 8, rings = 13;
        Assert.Equal(sides * rings * 8, model.Vertices.Length);
        for (int k = 0; k < rings; k++)
            for (int j = 0; j < sides; j++)
            {
                int v = k * sides + j;
                float angle = 2 * MathF.PI * j / sides;
                // 사각형을 대각선으로 쪼갠 삼각형의 면적 가중 평균이라 정확히 방사형은 아니다: 방향이 거의 같으면 된다(코사인 > 0.98).
                float nx = model.Vertices[v * 8 + 3], ny = model.Vertices[v * 8 + 4], nz = model.Vertices[v * 8 + 5];
                Assert.Equal(1f, MathF.Sqrt(nx * nx + ny * ny + nz * nz), 3);
                Assert.True(nx * MathF.Cos(angle) + nz * MathF.Sin(angle) > 0.98f, $"vertex {v}: normal ({nx}, {ny}, {nz}) does not point away from the axis");
            }
    }

    [Fact]
    public void EmptySkinsArray_MeansNoSkin()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder { Skinned = false }, "empty_skins", d => d["skins"] = new JsonArray());
        ImportedModel model = GltfImporter.Import(path);

        Assert.Null(model.Skeleton);
        Assert.Equal(12, model.Indices.Length);
    }

    [Fact]
    public void UnusedJointSlot_WithZeroWeight_MayHoldAnyJointNumber()
    {
        // 가중치 0인 슬롯의 조인트 번호는 쓰이지 않으므로 범위를 벗어나도 거부하지 않는다(실제 파일에 흔하다). 쓰이는 슬롯은 위에서 거부 검사됨.
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "unused_slot", d =>
        {
            JsonNode accessor = Attribute(d, "JOINTS_0");
            JsonNode view = ViewOf(d, accessor);
            string uri = d["buffers"]![0]!["uri"]!.GetValue<string>();
            byte[] bin = Convert.FromBase64String(uri[(uri.IndexOf(',') + 1)..]);
            int slot3OfVertex0 = (view["byteOffset"]?.GetValue<int>() ?? 0) + (accessor["byteOffset"]?.GetValue<int>() ?? 0) + 3;
            bin[slot3OfVertex0] = 200;  // 조인트는 3개뿐이다
            d["buffers"]![0]!["uri"] = "data:application/octet-stream;base64," + Convert.ToBase64String(bin);
        });

        ImportedModel model = GltfImporter.Import(path);
        Assert.Equal(0u, model.Joints![3]);  // 가중치 0인 슬롯은 조인트 0으로 정규화된다
        Assert.Equal(0f, model.Weights![3]);
    }
}
