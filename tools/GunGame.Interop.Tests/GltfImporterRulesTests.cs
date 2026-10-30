using System.Text.Json.Nodes;
using GunGame.Interop;

namespace GunGame.Interop.Tests;

/// <summary>
/// glTF 임포터의 규칙/경고/오류 분기. 커버리지에서 한 번도 실행되지 않던 경로(노드 matrix, 정규화 정수 접근자, 각종 거부 사유와
/// 경고)를 직접 만든 파일로 검증한다. 오류는 사유(메시지)까지 확인해서 어떤 검사가 걸러냈는지 구별한다.
/// </summary>
public class GltfImporterRulesTests
{
    private static string Patched(TestAssets assets, GltfBuilder builder, string name, Action<JsonNode> patch)
    {
        builder.Glb = false;
        string path = builder.Write(assets.Directory, name);
        JsonNode doc = JsonNode.Parse(File.ReadAllText(path))!;
        patch(doc);
        File.WriteAllText(path, doc.ToJsonString());
        return path;
    }

    private static JsonNode PrimitiveAttribute(JsonNode doc, string attribute) =>
        doc["accessors"]![doc["meshes"]![0]!["primitives"]![0]!["attributes"]![attribute]!.GetValue<int>()]!;

    private static JsonNode IndicesAccessor(JsonNode doc) =>
        doc["accessors"]![doc["meshes"]![0]!["primitives"]![0]!["indices"]!.GetValue<int>()]!;

    private static int MeshNodeIndex(JsonNode doc) => doc["nodes"]!.AsArray().Count - 1;

    // ---- 거부: 사유까지 확인한다 ----
    [Fact]
    public void MalformedModels_AreRejectedWithTheirOwnReason()
    {
        var cases = new (string Name, GltfBuilder Builder, Action<JsonNode> Patch, string Expected)[]
        {
            ("sparse accessor", new(), d => PrimitiveAttribute(d, "POSITION")["sparse"] = new JsonObject(), "sparse"),
            ("accessor type", new(), d => PrimitiveAttribute(d, "POSITION")["type"] = "MAT3", "unsupported accessor type"),
            ("component type", new(), d => PrimitiveAttribute(d, "POSITION")["componentType"] = 5124, "unsupported component type"),
            ("outside the view", new(), d => PrimitiveAttribute(d, "POSITION")["count"] = 100000, "reads outside"),
            ("component count", new(), d => PrimitiveAttribute(d, "POSITION")["type"] = "VEC2", "has 2 components, expected 3"),
            ("signed indices", new(), d => IndicesAccessor(d)["componentType"] = 5122, "unsigned integer"),   // 크기가 같은 short라 뷰 범위 검사는 통과한다
            ("non-base64 data uri", new(), d => d["buffers"]![0]!["uri"] = "data:text/plain,hello", "base64"),
            ("missing child", new(), d => d["nodes"]![0]!["children"] = new JsonArray(1, 99), "does not exist"),
            ("short rotation", new(), d => d["nodes"]![0]!["rotation"] = new JsonArray(0, 0, 0), "expected 4 numbers"),
            ("short translation", new(), d => d["nodes"]![1]!["translation"] = new JsonArray(1, 2), "expected 3 numbers"),
            ("short matrix", new(), d => d["nodes"]![MeshNodeIndex(d)]!["matrix"] = new JsonArray(Enumerable.Range(0, 15).Select(_ => (JsonNode)1).ToArray()), "16 numbers"),
            ("skin without joints", new(), d => d["skins"]![0]!["joints"] = new JsonArray(), "no joints"),
            ("duplicate joint", new(), d => d["skins"]![0]!["joints"] = new JsonArray(1, 1, 2), "same joint twice"),
            ("ibm count", new(), d => d["skins"]![0]!["joints"] = new JsonArray(2, 3), "inverseBindMatrices count"),
            ("normal count", new(), d => PrimitiveAttribute(d, "NORMAL")["count"] = 2, "attribute counts do not match"),
            ("indices not triangles", new(), d => IndicesAccessor(d)["count"] = 5, "primitive indices are invalid"),
            ("weights count", new(), d => PrimitiveAttribute(d, "WEIGHTS_0")["count"] = 2, "joint/weight counts"),
            ("no triangles", new(), d => d["meshes"]![0]!["primitives"]![0]!["mode"] = 1, "no triangle geometry"),
        };

        using var assets = new TestAssets();
        foreach ((string name, GltfBuilder builder, Action<JsonNode> patch, string expected) in cases)
        {
            string path = Patched(assets, builder, name.Replace(' ', '_'), patch);
            Exception? thrown = Record.Exception(() => GltfImporter.Import(path));
            Assert.True(thrown is GltfException, $"{name}: expected a GltfException but got {thrown?.GetType().Name ?? "no exception"}");
            Assert.True(thrown!.Message.Contains(expected), $"{name}: expected '{expected}' but got '{thrown.Message}'");
        }
    }

    [Fact]
    public void RootThatIsNotAnObject_IsRejected()
    {
        using var assets = new TestAssets();
        string path = assets.Path_("array.gltf");
        File.WriteAllText(path, "[1,2,3]");
        Assert.Contains("object", Assert.Throws<GltfException>(() => GltfImporter.Import(path)).Message);
    }

    [Fact]
    public void IndicesBeyondTheVertexCount_AreRejected()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "oob", d =>
        {
            foreach (string attribute in new[] { "POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0" })
                PrimitiveAttribute(d, attribute)["count"] = 2;  // 개수는 서로 일치하지만 인덱스(최대 5)가 정점 수(2)를 넘는다
        });
        Assert.Contains("primitive indices are invalid", Assert.Throws<GltfException>(() => GltfImporter.Import(path)).Message);
    }

    [Fact]
    public void VertexReferencingAJointOutsideTheSkin_IsRejected()
    {
        using var assets = new TestAssets();
        string path = Patched(assets, new GltfBuilder(), "joint_oob", d =>
        {
            d["skins"]![0]!["joints"] = new JsonArray(3, 2);                          // 조인트 3개 중 2개만 남긴다
            d["accessors"]![d["skins"]![0]!["inverseBindMatrices"]!.GetValue<int>()]!["count"] = 2;
        });
        Assert.Contains("vertex references joint", Assert.Throws<GltfException>(() => GltfImporter.Import(path)).Message);
    }

    // ---- 경고: 가져오되 알려 준다 ----
    [Fact]
    public void UnsupportedParts_AreImportedWithWarnings()
    {
        using var assets = new TestAssets();

        string lines = Patched(assets, new GltfBuilder(), "mixed_modes", d =>
        {
            var prims = d["meshes"]![0]!["primitives"]!.AsArray();
            var copy = JsonNode.Parse(prims[0]!.ToJsonString())!;
            copy["mode"] = 1;  // 선분 프리미티브: 삼각형만 지원한다
            prims.Add(copy);
        });
        ImportedModel withLines = GltfImporter.Import(lines);
        Assert.Contains(withLines.Warnings, w => w.Contains("primitive mode 1 skipped"));
        Assert.Equal(12, withLines.Indices.Length);  // 삼각형 프리미티브는 그대로

        string unskinned = Patched(assets, new GltfBuilder(), "unskinned_extra", d =>
        {
            var prims = d["meshes"]![0]!["primitives"]!.AsArray();
            var copy = JsonNode.Parse(prims[0]!.ToJsonString())!;
            copy["attributes"]!.AsObject().Remove("JOINTS_0");
            copy["attributes"]!.AsObject().Remove("WEIGHTS_0");
            prims.Add(copy);
        });
        ImportedModel skinned = GltfImporter.Import(unskinned);
        Assert.Contains(skinned.Warnings, w => w.Contains("non-skinned primitive was skipped"));
        Assert.Equal(12, skinned.Indices.Length);

        string twoSkins = Patched(assets, new GltfBuilder(), "two_skins", d =>
        {
            var skins = d["skins"]!.AsArray();
            skins.Add(JsonNode.Parse(skins[0]!.ToJsonString()));
        });
        Assert.Contains(GltfImporter.Import(twoSkins).Warnings, w => w.Contains("multiple skins"));

        string noSkin = Patched(assets, new GltfBuilder { Skinned = false }, "anim_without_skin", d =>
            d["animations"] = new JsonArray(new JsonObject()));
        Assert.Contains(GltfImporter.Import(noSkin).Warnings, w => w.Contains("animations ignored"));
    }

    [Fact]
    public void AnimationChannels_ThatCannotBeUsed_AreSkippedWithWarnings()
    {
        using var assets = new TestAssets();

        string notAJoint = Patched(assets, new GltfBuilder(), "channel_not_joint", d =>
            d["animations"]![0]!["channels"]![0]!["target"]!["node"] = MeshNodeIndex(d));
        ImportedModel a = GltfImporter.Import(notAJoint);
        Assert.Contains(a.Warnings, w => w.Contains("not a joint"));
        Assert.Contains(a.Warnings, w => w.Contains("no usable channels"));
        Assert.Empty(a.Clips);

        string weights = Patched(assets, new GltfBuilder(), "channel_weights", d =>
            d["animations"]![0]!["channels"]![0]!["target"]!["path"] = "weights");
        ImportedModel b = GltfImporter.Import(weights);
        Assert.Contains(b.Warnings, w => w.Contains("'weights' channels are not supported"));
        Assert.Empty(b.Clips);

        string mismatched = Patched(assets, new GltfBuilder(), "keys_mismatch", d =>
        {
            int output = d["animations"]![0]!["samplers"]![0]!["output"]!.GetValue<int>();
            d["accessors"]![output]!["count"] = 1;  // 시간 2개 대 값 1개
        });
        Assert.Contains("mismatched keyframe counts", Assert.Throws<GltfException>(() => GltfImporter.Import(mismatched)).Message);
    }

    [Fact]
    public void ClipNames_AreFilledInAndKeptUnique()
    {
        using var assets = new TestAssets();
        var builder = new GltfBuilder();
        builder.Animations.Clear();
        builder.Animations.Add(("walk", 0f, 1f));
        builder.Animations.Add(("walk", 0f, 2f));
        builder.Animations.Add(("unnamed", 0f, 3f));
        string path = Patched(assets, builder, "names", d => d["animations"]![2]!.AsObject().Remove("name"));
        Assert.Equal(["walk", "walk#1", "clip2"], GltfImporter.Import(path).Clips.Select(c => c.Name));
    }

    // ---- 노드 matrix ----
    [Fact]
    public void NodeMatrix_IsAppliedLikeTranslationRotationScale()
    {
        using var assets = new TestAssets();
        ImportedModel plain = GltfImporter.Import(Patched(assets, new GltfBuilder { Skinned = false }, "plain", _ => { }));
        // glTF 행렬은 열 우선: 대각 (2,3,4) 스케일 + 이동 (1,2,3)
        float[] matrix = [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 2, 3, 1];
        ImportedModel moved = GltfImporter.Import(Patched(assets, new GltfBuilder { Skinned = false }, "matrix", d =>
            d["nodes"]![MeshNodeIndex(d)]!["matrix"] = new JsonArray(matrix.Select(v => (JsonNode)v).ToArray())));

        Assert.Equal(plain.Vertices.Length, moved.Vertices.Length);
        for (int v = 0; v < plain.Vertices.Length / 8; v++)
        {
            Assert.Equal(plain.Vertices[v * 8] * 2 + 1, moved.Vertices[v * 8], 4);
            Assert.Equal(plain.Vertices[v * 8 + 1] * 3 + 2, moved.Vertices[v * 8 + 1], 4);
            Assert.Equal(plain.Vertices[v * 8 + 2] * 4 + 3, moved.Vertices[v * 8 + 2], 4);
        }
    }

    // ---- 접근자 성분 형식 ----
    private static string MiniTriangle(TestAssets assets, string name, int uvType, bool normalized, byte[] uvBytes, int indexType, byte[] indexBytes)
    {
        var bin = new List<byte>();
        foreach (float f in new float[] { 0, 0, 0, 1, 0, 0, 0, 1, 0 })
            bin.AddRange(BitConverter.GetBytes(f));
        int uvOffset = bin.Count;
        bin.AddRange(uvBytes);
        int indexOffset = bin.Count;
        bin.AddRange(indexBytes);
        var doc = new JsonObject
        {
            ["asset"] = new JsonObject { ["version"] = "2.0" },
            ["scene"] = 0,
            ["scenes"] = new JsonArray(new JsonObject { ["nodes"] = new JsonArray(0) }),
            ["nodes"] = new JsonArray(new JsonObject { ["mesh"] = 0 }),
            ["meshes"] = new JsonArray(new JsonObject
            {
                ["primitives"] = new JsonArray(new JsonObject
                {
                    ["attributes"] = new JsonObject { ["POSITION"] = 0, ["TEXCOORD_0"] = 1 },
                    ["indices"] = 2,
                }),
            }),
            ["accessors"] = new JsonArray(
                new JsonObject { ["bufferView"] = 0, ["componentType"] = 5126, ["count"] = 3, ["type"] = "VEC3" },
                new JsonObject { ["bufferView"] = 1, ["componentType"] = uvType, ["count"] = 3, ["type"] = "VEC2", ["normalized"] = normalized },
                new JsonObject { ["bufferView"] = 2, ["componentType"] = indexType, ["count"] = 3, ["type"] = "SCALAR" }),
            ["bufferViews"] = new JsonArray(
                new JsonObject { ["buffer"] = 0, ["byteOffset"] = 0, ["byteLength"] = 36 },
                new JsonObject { ["buffer"] = 0, ["byteOffset"] = uvOffset, ["byteLength"] = uvBytes.Length },
                new JsonObject { ["buffer"] = 0, ["byteOffset"] = indexOffset, ["byteLength"] = indexBytes.Length }),
            ["buffers"] = new JsonArray(new JsonObject
            {
                ["byteLength"] = bin.Count,
                ["uri"] = "data:application/octet-stream;base64," + Convert.ToBase64String(bin.ToArray()),
            }),
        };
        string path = assets.Path_(name + ".gltf");
        File.WriteAllText(path, doc.ToJsonString());
        return path;
    }

    private static float[] Uvs(ImportedModel m) => Enumerable.Range(0, 3).SelectMany(v => new[] { m.Vertices[v * 8 + 6], m.Vertices[v * 8 + 7] }).ToArray();

    [Fact]
    public void NormalizedIntegerTexcoords_AreScaledToZeroOne_AndRawIntegersAreKept()
    {
        using var assets = new TestAssets();
        byte[] idx16 = [0, 0, 1, 0, 2, 0];
        void Check(string name, int type, bool normalized, byte[] bytes, float[] expected)
        {
            ImportedModel m = GltfImporter.Import(MiniTriangle(assets, name, type, normalized, bytes, 5123, idx16));
            float[] got = Uvs(m);
            for (int i = 0; i < expected.Length; i++)
                Assert.True(MathF.Abs(got[i] - expected[i]) < 1e-4f, $"{name}[{i}]: expected {expected[i]} got {got[i]}");
        }

        Check("ubyte_norm", 5121, true, [0, 0, 255, 0, 128, 255], [0, 0, 1, 0, 128 / 255f, 1]);
        Check("ubyte_raw", 5121, false, [0, 0, 255, 0, 128, 255], [0, 0, 255, 0, 128, 255]);
        Check("ushort_norm", 5123, true, [0, 0, 0, 0, 255, 255, 0, 0, 0, 128, 255, 255], [0, 0, 1, 0, 32768 / 65535f, 1]);
        Check("short_norm", 5122, true, [0, 0, 0, 0, 255, 127, 0, 0, 0, 128, 0, 0], [0, 0, 1, 0, -1, 0]);        // 32767 -> 1, -32768 -> -1(하한)
        Check("short_raw", 5122, false, [0, 0, 0, 0, 255, 127, 0, 0, 0, 128, 0, 0], [0, 0, 32767, 0, -32768, 0]);
        Check("byte_norm", 5120, true, [0, 0, 127, 0, 128, 64], [0, 0, 1, 0, -1, 64 / 127f]);                      // -128 -> -1(하한)
        Check("byte_raw", 5120, false, [0, 0, 127, 0, 128, 64], [0, 0, 127, 0, -128, 64]);
    }

    [Fact]
    public void IndexTypes_AllReadTheSameTriangle()
    {
        using var assets = new TestAssets();
        byte[] uv = [0, 0, 0, 0, 0, 0];
        ImportedModel ubyte = GltfImporter.Import(MiniTriangle(assets, "idx8", 5121, true, uv, 5121, [0, 1, 2]));
        ImportedModel ushortIdx = GltfImporter.Import(MiniTriangle(assets, "idx16", 5121, true, uv, 5123, [0, 0, 1, 0, 2, 0]));
        ImportedModel uintIdx = GltfImporter.Import(MiniTriangle(assets, "idx32", 5121, true, uv, 5125, [0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0]));
        foreach (ImportedModel m in new[] { ubyte, ushortIdx, uintIdx })
            Assert.Equal<uint>([0, 1, 2], m.Indices);
    }
}
