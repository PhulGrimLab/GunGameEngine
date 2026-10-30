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
    public void ArmatureWarning_IsReportedOnceForTheRootBoneOnly()
    {
        using var assets = new TestAssets();
        ImportedModel model = GltfImporter.Import(new GltfBuilder { ArmatureTranslation = [5, 0, 0] }.Write(assets.Directory, "arm"));

        // 루트가 아닌 본의 조상(루트 본, 아르마추어)도 비항등 변환을 갖지만, 변환이 무시되는 것은 루트 본 위쪽뿐이라 경고는 한 번이다.
        Assert.Single(model.Warnings, w => w.Contains("armature node has a non-identity transform"));
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
}
