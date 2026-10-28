using System.Collections.ObjectModel;

namespace GunGame.Scene;

/// <summary>노드 트리. 구조(추가/삭제/이동)는 이 클래스를 통해서만 바꾼다.</summary>
public sealed class SceneDocument
{
    public ObservableCollection<SceneNode> Roots { get; } = [];

    /// <summary>노드가 추가/삭제/이동될 때 발생한다. 속성 변경은 포함하지 않는다.</summary>
    public event Action? StructureChanged;

    public void Add(SceneNode node, SceneNode? parent = null)
    {
        if (node.Parent is not null || Roots.Contains(node))
            throw new InvalidOperationException("node is already in a scene");
        Attach(node, parent);
        StructureChanged?.Invoke();
    }

    /// <summary>노드와 자손을 씬에서 제거한다.</summary>
    public void Remove(SceneNode node)
    {
        Detach(node);
        StructureChanged?.Invoke();
    }

    /// <summary>노드를 새 부모 아래로(null이면 루트로) 옮긴다. 자기 자신이나 자손 아래로는 옮길 수 없다.</summary>
    public void Move(SceneNode node, SceneNode? newParent)
    {
        for (SceneNode? p = newParent; p is not null; p = p.Parent)
            if (p == node)
                throw new InvalidOperationException("cannot move a node under itself or its descendant");
        Detach(node);
        Attach(node, newParent);
        StructureChanged?.Invoke();
    }

    /// <summary>깊이 우선, 부모 먼저.</summary>
    public IEnumerable<SceneNode> Nodes()
    {
        var stack = new Stack<SceneNode>(Roots.Reverse());
        while (stack.Count > 0)
        {
            SceneNode node = stack.Pop();
            yield return node;
            foreach (SceneNode child in node.Children.Reverse())
                stack.Push(child);
        }
    }

    private void Attach(SceneNode node, SceneNode? parent)
    {
        if (parent is null)
            Roots.Add(node);
        else
            parent.Children.Add(node);
        node.Parent = parent;
    }

    private void Detach(SceneNode node)
    {
        bool removed = node.Parent is null ? Roots.Remove(node) : node.Parent.Children.Remove(node);
        if (!removed)
            throw new InvalidOperationException("node is not in this scene");
        node.Parent = null;
    }
}
