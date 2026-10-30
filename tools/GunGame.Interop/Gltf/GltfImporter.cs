using System.Buffers.Binary;
using System.Numerics;
using System.Text;
using System.Text.Json;

namespace GunGame.Interop;

public sealed class GltfException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>
/// glTF 2.0(.glb / .gltf)에서 메시·스켈레톤·애니메이션을 읽는다. 오른손 Y-up, CCW, UV 원점 좌상단이라 엔진 규약과 같다.
/// 지원: 삼각형 프리미티브, POSITION/NORMAL/TEXCOORD_0/JOINTS_0/WEIGHTS_0, 스킨 1개, translation/rotation/scale 선형 채널.
/// 미지원(오류): sparse 액세서, STEP/CUBICSPLINE 보간. 재질/텍스처는 무시한다(텍스처는 씬 노드가 따로 지정).
/// 스킨이 있는 파일에서는 스킨된 프리미티브만 가져오고, 아르마추어 노드의 변환은 무시한다(Warnings에 보고).
/// </summary>
public static class GltfImporter
{
    private const int MaxBones = 64;
    // bufferView가 없는 접근자는 0으로 채운 값을 만드는데, 버퍼 길이로 개수가 제한되지 않아 악성 파일이 거대한 할당을 일으킬 수 있다.
    private const int MaxZeroFilledAccessorCount = 1 << 24;

    public static ImportedModel Import(string path)
    {
        try
        {
            byte[] bytes = File.ReadAllBytes(path);
            using var reader = new Reader(bytes, Path.GetDirectoryName(Path.GetFullPath(path))!);
            return reader.Read();
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException
                                       or IndexOutOfRangeException or FormatException or ArgumentException
                                       or OverflowException)
        {
            throw new GltfException("invalid glTF: " + ex.Message, ex);
        }
    }

    private sealed class Node
    {
        public int Parent = -1;
        public Vector3 Translation;
        public Quaternion Rotation = Quaternion.Identity;
        public Vector3 Scale = Vector3.One;
        public int Mesh = -1;
        public Matrix4x4 Local =>
            Matrix4x4.CreateScale(Scale) * Matrix4x4.CreateFromQuaternion(Rotation) * Matrix4x4.CreateTranslation(Translation);
    }

    private sealed class Reader : IDisposable
    {
        private readonly JsonDocument _document;
        private readonly JsonElement _root;
        private readonly string _baseDir;
        private readonly byte[]? _glbBin;
        private readonly List<byte[]?> _buffers = [];
        private readonly List<string> _warnings = [];
        private List<Node> _nodes = [];

        public Reader(byte[] file, string baseDir)
        {
            _baseDir = baseDir;
            string json;
            if (file.Length >= 4 && file[0] == 'g' && file[1] == 'l' && file[2] == 'T' && file[3] == 'F')
                (json, _glbBin) = ParseGlb(file);
            else
                json = Encoding.UTF8.GetString(file);
            _document = JsonDocument.Parse(json);
            _root = _document.RootElement;
            if (_root.ValueKind != JsonValueKind.Object)
            {
                _document.Dispose();
                throw new GltfException("glTF root must be an object");
            }
        }

        public void Dispose() => _document.Dispose();

        private static (string json, byte[]? bin) ParseGlb(byte[] file)
        {
            if (file.Length < 20 || BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(4)) != 2)
                throw new GltfException("unsupported or truncated GLB (version 2 required)");
            uint total = BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(8));
            if (total > file.Length)
                throw new GltfException("GLB length exceeds file size");
            string? json = null;
            byte[]? bin = null;
            int offset = 12;
            while (offset + 8 <= total)
            {
                uint length = BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(offset));
                uint type = BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(offset + 4));
                offset += 8;
                if (length > total - offset)
                    throw new GltfException("GLB chunk exceeds file size");
                if (type == 0x4E4F534A && json is null)
                    json = Encoding.UTF8.GetString(file, offset, (int)length);
                else if (type == 0x004E4942 && bin is null)
                    bin = file.AsSpan(offset, (int)length).ToArray();
                offset += (int)length;
            }
            return (json ?? throw new GltfException("GLB has no JSON chunk"), bin);
        }

        public ImportedModel Read()
        {
            LoadBuffers();
            LoadNodes();

            int[] remap = [];
            SkeletonData? skeleton = null;
            if (_root.TryGetProperty("skins", out JsonElement skins) && skins.GetArrayLength() > 0)
            {
                if (skins.GetArrayLength() > 1)
                    _warnings.Add("multiple skins found; only the first is used");
                (skeleton, remap) = ReadSkin(skins[0]);
            }

            (float[] vertices, uint[] indices, uint[]? joints, float[]? weights) = ReadMeshes(skeleton is not null, remap);
            List<ClipData> clips = skeleton is null ? [] : ReadAnimations(remap);
            if (skeleton is null && _root.TryGetProperty("animations", out _))
                _warnings.Add("animations ignored because the file has no skin");

            return new ImportedModel
            {
                Vertices = vertices,
                Indices = indices,
                Joints = joints,
                Weights = weights,
                Skeleton = skeleton,
                Clips = clips,
                Warnings = _warnings,
            };
        }

        // ---- 버퍼/액세서 ----
        private void LoadBuffers()
        {
            if (!_root.TryGetProperty("buffers", out JsonElement buffers))
                return;
            int index = 0;
            foreach (JsonElement buffer in buffers.EnumerateArray())
            {
                if (!buffer.TryGetProperty("uri", out JsonElement uri))
                {
                    _buffers.Add(index == 0 ? _glbBin ?? throw new GltfException("buffer 0 has no data") : throw new GltfException("buffer without uri"));
                }
                else
                {
                    string value = uri.GetString() ?? "";
                    if (value.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
                    {
                        int comma = value.IndexOf(',');
                        if (comma < 0 || !value[..comma].Contains("base64", StringComparison.OrdinalIgnoreCase))
                            throw new GltfException("only base64 data URIs are supported");
                        _buffers.Add(Convert.FromBase64String(value[(comma + 1)..]));
                    }
                    else
                    {
                        // glTF의 외부 버퍼 uri는 상대 경로여야 한다. 절대 경로/UNC(\host\share)/드라이브·스킴(':')은
                        // 파일을 열기 전에 거부한다(UNC를 열면 SMB 연결과 NTLM 인증이 일어날 수 있다).
                        string relative = Uri.UnescapeDataString(value);
                        if (Path.IsPathRooted(relative) || relative.Contains(':'))
                            throw new GltfException($"external buffer uri must be a relative path: '{value}'");
                        string full = Path.GetFullPath(Path.Combine(_baseDir, relative));
                        _buffers.Add(File.ReadAllBytes(full));
                    }
                }
                int declared = buffer.GetProperty("byteLength").GetInt32();
                if (_buffers[index]!.Length < declared)
                    throw new GltfException($"buffer {index} is shorter than its declared byteLength");
                index++;
            }
        }

        private sealed record AccessorInfo(int Count, int Components, int ComponentType, bool Normalized, byte[]? Data, int Offset, int Stride, int ComponentSize);

        private AccessorInfo Accessor(int index)
        {
            JsonElement a = _root.GetProperty("accessors")[index];
            if (a.TryGetProperty("sparse", out _))
                throw new GltfException("sparse accessors are not supported");
            int count = a.GetProperty("count").GetInt32();
            if (count < 0)
                throw new GltfException($"accessor {index} has a negative count");
            int componentType = a.GetProperty("componentType").GetInt32();
            int components = a.GetProperty("type").GetString() switch
            {
                "SCALAR" => 1, "VEC2" => 2, "VEC3" => 3, "VEC4" => 4, "MAT4" => 16,
                var t => throw new GltfException($"unsupported accessor type '{t}'"),
            };
            int componentSize = componentType switch
            {
                5120 or 5121 => 1, 5122 or 5123 => 2, 5125 or 5126 => 4,
                _ => throw new GltfException($"unsupported component type {componentType}"),
            };
            bool normalized = a.TryGetProperty("normalized", out JsonElement n) && n.GetBoolean();
            int accessorOffset = a.TryGetProperty("byteOffset", out JsonElement ao) ? ao.GetInt32() : 0;
            if (accessorOffset < 0)  // 규격은 0 이상만 허용한다. 음수면 자기 bufferView 앞쪽의 다른 데이터를 읽게 된다.
                throw new GltfException($"accessor {index} has a negative byteOffset");

            if (!a.TryGetProperty("bufferView", out JsonElement viewIndex))
            {
                if (count > MaxZeroFilledAccessorCount)
                    throw new GltfException($"accessor {index} has no bufferView and its count {count} exceeds the supported maximum of {MaxZeroFilledAccessorCount}");
                return new AccessorInfo(count, components, componentType, normalized, null, 0, 0, componentSize);
            }

            JsonElement view = _root.GetProperty("bufferViews")[viewIndex.GetInt32()];
            byte[] data = _buffers[view.GetProperty("buffer").GetInt32()]!;
            int viewOffset = view.TryGetProperty("byteOffset", out JsonElement vo) ? vo.GetInt32() : 0;
            int viewLength = view.GetProperty("byteLength").GetInt32();
            int stride = view.TryGetProperty("byteStride", out JsonElement bs) ? bs.GetInt32() : components * componentSize;

            long start = (long)viewOffset + accessorOffset;
            long end = count == 0 ? start : start + (long)(count - 1) * stride + components * componentSize;
            if (stride < components * componentSize || start < 0 || end > (long)viewOffset + viewLength || end > data.Length)
                throw new GltfException($"accessor {index} reads outside its buffer view");
            return new AccessorInfo(count, components, componentType, normalized, data, (int)start, stride, componentSize);
        }

        private static double ReadComponent(AccessorInfo a, int element, int component)
        {
            if (a.Data is null)
                return 0;
            ReadOnlySpan<byte> s = a.Data.AsSpan(a.Offset + element * a.Stride + component * a.ComponentSize);
            return a.ComponentType switch
            {
                5126 => BitConverter.ToSingle(s),
                5125 => BinaryPrimitives.ReadUInt32LittleEndian(s),
                5123 => BinaryPrimitives.ReadUInt16LittleEndian(s),
                5122 => BinaryPrimitives.ReadInt16LittleEndian(s),
                5121 => s[0],
                _ => (sbyte)s[0],
            };
        }

        private float[] ReadFloats(int accessor, int components)
        {
            AccessorInfo a = Accessor(accessor);
            if (a.Components != components)
                throw new GltfException($"accessor {accessor} has {a.Components} components, expected {components}");
            var result = new float[a.Count * components];
            for (int e = 0; e < a.Count; e++)
                for (int c = 0; c < components; c++)
                {
                    double v = ReadComponent(a, e, c);
                    if (a.Normalized && a.ComponentType != 5126)
                        v = a.ComponentType switch
                        {
                            5121 => v / 255.0, 5123 => v / 65535.0, 5120 => Math.Max(v / 127.0, -1), 5122 => Math.Max(v / 32767.0, -1), _ => v,
                        };
                    result[e * components + c] = (float)v;
                }
            return result;
        }

        private uint[] ReadUInts(int accessor, int components)
        {
            AccessorInfo a = Accessor(accessor);
            if (a.Components != components || a.ComponentType is 5126 or 5120 or 5122)
                throw new GltfException($"accessor {accessor} is not an unsigned integer accessor with {components} components");
            var result = new uint[a.Count * components];
            for (int e = 0; e < a.Count; e++)
                for (int c = 0; c < components; c++)
                    result[e * components + c] = (uint)ReadComponent(a, e, c);
            return result;
        }

        // ---- 노드 ----
        private void LoadNodes()
        {
            _nodes = [];
            if (!_root.TryGetProperty("nodes", out JsonElement nodes))
                return;
            foreach (JsonElement n in nodes.EnumerateArray())
            {
                var node = new Node();
                if (n.TryGetProperty("matrix", out JsonElement m))
                {
                    float[] f = m.EnumerateArray().Select(x => x.GetSingle()).ToArray();
                    if (f.Length != 16)
                        throw new GltfException("node matrix must have 16 numbers");
                    var matrix = new Matrix4x4(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
                    if (!Matrix4x4.Decompose(matrix, out node.Scale, out node.Rotation, out node.Translation))
                        throw new GltfException("node matrix cannot be decomposed");
                }
                else
                {
                    if (n.TryGetProperty("translation", out JsonElement t)) node.Translation = ToVector3(t);
                    if (n.TryGetProperty("rotation", out JsonElement r)) node.Rotation = Quaternion.Normalize(ToQuaternion(r));
                    if (n.TryGetProperty("scale", out JsonElement s)) node.Scale = ToVector3(s);
                }
                if (n.TryGetProperty("mesh", out JsonElement mesh))
                    node.Mesh = mesh.GetInt32();
                _nodes.Add(node);
            }

            int index = 0;
            foreach (JsonElement n in nodes.EnumerateArray())
            {
                if (n.TryGetProperty("children", out JsonElement children))
                    foreach (JsonElement child in children.EnumerateArray())
                    {
                        int childIndex = child.GetInt32();
                        if (childIndex < 0 || childIndex >= _nodes.Count)
                            throw new GltfException($"node {index} lists child {childIndex}, which does not exist");
                        // glTF 규격: 노드는 부모가 최대 하나이고 자기 자신의 자손이 될 수 없다. 어기면 조상 탐색이 끝나지 않을 수 있다.
                        if (childIndex == index)
                            throw new GltfException($"node {index} is its own child");
                        if (_nodes[childIndex].Parent >= 0)
                            throw new GltfException($"node {childIndex} has more than one parent");
                        _nodes[childIndex].Parent = index;
                    }
                index++;
            }
        }

        private static Vector3 ToVector3(JsonElement e)
        {
            float[] f = e.EnumerateArray().Select(x => x.GetSingle()).ToArray();
            return f.Length == 3 ? new Vector3(f[0], f[1], f[2]) : throw new GltfException("expected 3 numbers");
        }

        private static Quaternion ToQuaternion(JsonElement e)
        {
            float[] f = e.EnumerateArray().Select(x => x.GetSingle()).ToArray();
            return f.Length == 4 ? new Quaternion(f[0], f[1], f[2], f[3]) : throw new GltfException("expected 4 numbers");
        }

        private Matrix4x4 World(int node)
        {
            Matrix4x4 world = Matrix4x4.Identity;
            for (int n = node, guard = 0; n >= 0; n = _nodes[n].Parent, guard++)
            {
                if (guard > _nodes.Count)
                    throw new GltfException("node hierarchy contains a cycle");
                world *= _nodes[n].Local;
            }
            return world;
        }

        // ---- 스킨 ----
        /// <returns>스켈레톤(부모 우선 순서)과 remap[원래 조인트 번호] = 새 본 번호</returns>
        private (SkeletonData skeleton, int[] remap) ReadSkin(JsonElement skin)
        {
            int[] jointNodes = skin.GetProperty("joints").EnumerateArray().Select(x => x.GetInt32()).ToArray();
            int count = jointNodes.Length;
            if (count == 0)
                throw new GltfException("skin has no joints");
            if (count > MaxBones)
                throw new GltfException($"skin has {count} joints; the engine supports at most {MaxBones}");

            var jointOfNode = new Dictionary<int, int>();
            for (int i = 0; i < count; i++)
                if (!jointOfNode.TryAdd(jointNodes[i], i))
                    throw new GltfException("skin lists the same joint twice");

            // 가장 가까운 조인트 조상을 부모 본으로 삼는다.
            int[] originalParent = new int[count];
            for (int i = 0; i < count; i++)
            {
                originalParent[i] = -1;
                int guard = 0;
                for (int n = _nodes[jointNodes[i]].Parent; n >= 0; n = _nodes[n].Parent)
                {
                    if (++guard > _nodes.Count)  // 조인트 위쪽에 순환(A->B->A)이 있으면 영원히 돌지 않도록 World()와 같은 가드를 둔다
                        throw new GltfException("node hierarchy contains a cycle");
                    if (jointOfNode.TryGetValue(n, out int parentJoint)) { originalParent[i] = parentJoint; break; }
                }
            }

            // 부모가 항상 앞에 오도록 깊이 우선으로 재정렬한다. order[new] = old, remap[old] = new
            var order = new List<int>();
            var children = Enumerable.Range(0, count).ToLookup(i => originalParent[i]);
            void Visit(int joint)
            {
                order.Add(joint);
                foreach (int child in children[joint])
                    Visit(child);
            }
            foreach (int root in children[-1])
                Visit(root);
            if (order.Count != count)
                throw new GltfException("skin joint hierarchy is inconsistent");
            int[] remap = new int[count];
            for (int i = 0; i < count; i++)
                remap[order[i]] = i;

            float[]? inverseBind = skin.TryGetProperty("inverseBindMatrices", out JsonElement ibm) ? ReadFloats(ibm.GetInt32(), 16) : null;
            if (inverseBind is not null && inverseBind.Length != count * 16)
                throw new GltfException("inverseBindMatrices count does not match joints");

            var parents = new int[count];
            var ibmOut = new float[count * 16];
            var translation = new float[count * 3];
            var rotation = new float[count * 4];
            var scale = new float[count * 3];
            for (int i = 0; i < count; i++)
            {
                int old = order[i];
                Node node = _nodes[jointNodes[old]];
                parents[i] = originalParent[old] < 0 ? -1 : remap[originalParent[old]];
                if (inverseBind is not null)
                    Array.Copy(inverseBind, old * 16, ibmOut, i * 16, 16);
                else
                    for (int d = 0; d < 4; d++) ibmOut[i * 16 + d * 5] = 1;
                translation[i * 3] = node.Translation.X; translation[i * 3 + 1] = node.Translation.Y; translation[i * 3 + 2] = node.Translation.Z;
                rotation[i * 4] = node.Rotation.X; rotation[i * 4 + 1] = node.Rotation.Y; rotation[i * 4 + 2] = node.Rotation.Z; rotation[i * 4 + 3] = node.Rotation.W;
                scale[i * 3] = node.Scale.X; scale[i * 3 + 1] = node.Scale.Y; scale[i * 3 + 2] = node.Scale.Z;

                if (parents[i] < 0 && _nodes[jointNodes[old]].Parent >= 0 && !IsIdentity(World(_nodes[jointNodes[old]].Parent)))
                    _warnings.Add("the armature node has a non-identity transform, which is ignored");
            }
            return (new SkeletonData(parents, ibmOut, translation, rotation, scale), remap);
        }

        private static bool IsIdentity(Matrix4x4 m) =>
            Enumerable.Range(0, 16).All(i => MathF.Abs(Element(m, i) - (i % 5 == 0 ? 1 : 0)) < 1e-5f);

        private static float Element(Matrix4x4 m, int i) => i switch
        {
            0 => m.M11, 1 => m.M12, 2 => m.M13, 3 => m.M14, 4 => m.M21, 5 => m.M22, 6 => m.M23, 7 => m.M24,
            8 => m.M31, 9 => m.M32, 10 => m.M33, 11 => m.M34, 12 => m.M41, 13 => m.M42, 14 => m.M43, _ => m.M44,
        };

        // ---- 메시 ----
        private (float[] vertices, uint[] indices, uint[]? joints, float[]? weights) ReadMeshes(bool skinned, int[] remap)
        {
            var vertices = new List<float>();
            var indices = new List<uint>();
            var joints = new List<uint>();
            var weights = new List<float>();
            if (!_root.TryGetProperty("meshes", out JsonElement meshes))
                throw new GltfException("glTF has no meshes");

            for (int nodeIndex = 0; nodeIndex < _nodes.Count; nodeIndex++)
            {
                if (_nodes[nodeIndex].Mesh < 0)
                    continue;
                Matrix4x4 world = World(nodeIndex);
                foreach (JsonElement prim in meshes[_nodes[nodeIndex].Mesh].GetProperty("primitives").EnumerateArray())
                {
                    int mode = prim.TryGetProperty("mode", out JsonElement m) ? m.GetInt32() : 4;
                    if (mode != 4)
                    {
                        _warnings.Add($"primitive mode {mode} skipped (only triangles are supported)");
                        continue;
                    }
                    JsonElement attrs = prim.GetProperty("attributes");
                    bool primSkinned = attrs.TryGetProperty("JOINTS_0", out _) && attrs.TryGetProperty("WEIGHTS_0", out _);
                    if (skinned && !primSkinned)
                    {
                        _warnings.Add("a non-skinned primitive was skipped because the model is skinned");
                        continue;
                    }
                    AppendPrimitive(prim, attrs, skinned, skinned ? Matrix4x4.Identity : world, remap, vertices, indices, joints, weights);
                }
            }
            if (indices.Count == 0)
                throw new GltfException("glTF contains no triangle geometry");
            return (vertices.ToArray(), indices.ToArray(), skinned ? joints.ToArray() : null, skinned ? weights.ToArray() : null);
        }

        private void AppendPrimitive(JsonElement prim, JsonElement attrs, bool skinned, Matrix4x4 world, int[] remap,
                                     List<float> vertices, List<uint> indices, List<uint> joints, List<float> weights)
        {
            float[] position = ReadFloats(attrs.GetProperty("POSITION").GetInt32(), 3);
            int count = position.Length / 3;
            float[]? normal = attrs.TryGetProperty("NORMAL", out JsonElement nIdx) ? ReadFloats(nIdx.GetInt32(), 3) : null;
            float[]? uv = attrs.TryGetProperty("TEXCOORD_0", out JsonElement uIdx) ? ReadFloats(uIdx.GetInt32(), 2) : null;
            if ((normal is not null && normal.Length != position.Length) || (uv is not null && uv.Length != count * 2))
                throw new GltfException("vertex attribute counts do not match");

            uint[] tri = prim.TryGetProperty("indices", out JsonElement iIdx)
                ? ReadUInts(iIdx.GetInt32(), 1)
                : Enumerable.Range(0, count).Select(i => (uint)i).ToArray();
            if (tri.Length % 3 != 0 || tri.Any(i => i >= count))
                throw new GltfException("primitive indices are invalid");
            normal ??= GenerateNormals(position, tri);

            // 노드 변환을 정점에 구워 넣는다(스킨 메시는 변환을 쓰지 않는다). 법선은 비균등 스케일에서 표면에 수직을 유지하도록
            // 역전치 행렬로 변환하고, 행렬식이 음수인(거울 반전) 노드는 삼각형 감김 방향이 뒤집히므로 인덱스 순서를 바꿔 준다.
            Matrix4x4 normalMatrix = world;
            bool mirrored = false;
            if (!skinned)
            {
                if (Matrix4x4.Invert(world, out Matrix4x4 inverse))
                    normalMatrix = Matrix4x4.Transpose(inverse);
                // else: 특이 행렬(스케일 0 등)은 역행렬이 없어 변환된 법선이 의미가 없으므로 원래 행렬로 근사한다.
                mirrored = world.GetDeterminant() < 0;
            }

            uint baseVertex = (uint)(vertices.Count / 8);
            for (int v = 0; v < count; v++)
            {
                var p = new Vector3(position[v * 3], position[v * 3 + 1], position[v * 3 + 2]);
                var n = new Vector3(normal[v * 3], normal[v * 3 + 1], normal[v * 3 + 2]);
                if (!skinned)
                {
                    p = Vector3.Transform(p, world);
                    n = Vector3.Normalize(Vector3.TransformNormal(n, normalMatrix));
                }
                vertices.AddRange([p.X, p.Y, p.Z, n.X, n.Y, n.Z, uv?[v * 2] ?? 0, uv?[v * 2 + 1] ?? 0]);
            }
            for (int t = 0; t < tri.Length; t += 3)
            {
                indices.Add(tri[t] + baseVertex);
                indices.Add(tri[t + (mirrored ? 2 : 1)] + baseVertex);
                indices.Add(tri[t + (mirrored ? 1 : 2)] + baseVertex);
            }

            if (!skinned)
                return;
            uint[] j = ReadUInts(attrs.GetProperty("JOINTS_0").GetInt32(), 4);
            float[] w = ReadFloats(attrs.GetProperty("WEIGHTS_0").GetInt32(), 4);
            if (j.Length != count * 4 || w.Length != count * 4)
                throw new GltfException("joint/weight counts do not match vertex count");
            for (int k = 0; k < j.Length; k++)
            {
                if (w[k] > 0 && j[k] >= remap.Length)
                    throw new GltfException($"vertex references joint {j[k]} but the skin has {remap.Length} joints");
                joints.Add(w[k] > 0 ? (uint)remap[j[k]] : 0);
                weights.Add(w[k]);
            }
        }

        private static float[] GenerateNormals(float[] position, uint[] indices)
        {
            var normals = new Vector3[position.Length / 3];
            Vector3 P(uint i) => new(position[i * 3], position[i * 3 + 1], position[i * 3 + 2]);
            for (int t = 0; t < indices.Length; t += 3)
            {
                Vector3 face = Vector3.Cross(P(indices[t + 1]) - P(indices[t]), P(indices[t + 2]) - P(indices[t]));
                for (int k = 0; k < 3; k++)
                    normals[indices[t + k]] += face;
            }
            var result = new float[position.Length];
            for (int i = 0; i < normals.Length; i++)
            {
                Vector3 n = normals[i].LengthSquared() > 0 ? Vector3.Normalize(normals[i]) : Vector3.UnitY;
                result[i * 3] = n.X; result[i * 3 + 1] = n.Y; result[i * 3 + 2] = n.Z;
            }
            return result;
        }

        // ---- 애니메이션 ----
        private List<ClipData> ReadAnimations(int[] remap)
        {
            var clips = new List<ClipData>();
            if (!_root.TryGetProperty("animations", out JsonElement animations))
                return clips;

            JsonElement skin = _root.GetProperty("skins")[0];
            int[] jointNodes = skin.GetProperty("joints").EnumerateArray().Select(x => x.GetInt32()).ToArray();
            var jointOfNode = new Dictionary<int, int>();
            for (int i = 0; i < jointNodes.Length; i++)
                jointOfNode[jointNodes[i]] = i;

            int animationIndex = 0;
            foreach (JsonElement animation in animations.EnumerateArray())
            {
                string name = animation.TryGetProperty("name", out JsonElement nm) ? nm.GetString() ?? "" : "";
                if (name.Length == 0)
                    name = $"clip{animationIndex}";
                if (clips.Any(c => c.Name == name))
                    name += $"#{animationIndex}";
                animationIndex++;

                JsonElement samplers = animation.GetProperty("samplers");
                var channels = new List<ClipChannelData>();
                float duration = 0;
                foreach (JsonElement channel in animation.GetProperty("channels").EnumerateArray())
                {
                    JsonElement target = channel.GetProperty("target");
                    string pathName = target.GetProperty("path").GetString() ?? "";
                    if (!target.TryGetProperty("node", out JsonElement nodeElement) || !jointOfNode.TryGetValue(nodeElement.GetInt32(), out int joint))
                    {
                        _warnings.Add($"animation '{name}': a channel targets a node that is not a joint and was skipped");
                        continue;
                    }
                    ChannelPath? path = pathName switch
                    {
                        "translation" => ChannelPath.Translation,
                        "rotation" => ChannelPath.Rotation,
                        "scale" => ChannelPath.Scale,
                        _ => null,
                    };
                    if (path is null)
                    {
                        _warnings.Add($"animation '{name}': '{pathName}' channels are not supported and were skipped");
                        continue;
                    }

                    JsonElement sampler = samplers[channel.GetProperty("sampler").GetInt32()];
                    string interpolation = sampler.TryGetProperty("interpolation", out JsonElement ip) ? ip.GetString() ?? "LINEAR" : "LINEAR";
                    if (interpolation != "LINEAR")
                        throw new GltfException($"animation '{name}' uses {interpolation} interpolation, which is not supported");

                    int width = path == ChannelPath.Rotation ? 4 : 3;
                    float[] times = ReadFloats(sampler.GetProperty("input").GetInt32(), 1);
                    float[] values = ReadFloats(sampler.GetProperty("output").GetInt32(), width);
                    if (values.Length != times.Length * width)
                        throw new GltfException($"animation '{name}' has mismatched keyframe counts");
                    channels.Add(new ClipChannelData(remap[joint], path.Value, times, values));
                    duration = Math.Max(duration, times.Length > 0 ? times[^1] : 0);
                }

                if (channels.Count == 0)
                {
                    _warnings.Add($"animation '{name}' has no usable channels and was skipped");
                    continue;
                }
                clips.Add(new ClipData(name, duration, channels));
            }
            return clips;
        }
    }
}
