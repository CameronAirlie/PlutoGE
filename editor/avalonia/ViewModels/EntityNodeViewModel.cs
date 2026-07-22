using System.Collections.ObjectModel;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class EntityNodeViewModel
{
    public EntityNodeViewModel(uint id, string name, IEnumerable<EntityNodeViewModel>? children = null)
    {
        Id = id;
        Name = name;
        Children = new ObservableCollection<EntityNodeViewModel>(children ?? []);
    }

    public uint Id { get; }
    public string Name { get; }
    public ObservableCollection<EntityNodeViewModel> Children { get; }
}
