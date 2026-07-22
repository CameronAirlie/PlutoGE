using System.Collections.ObjectModel;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class EntityNodeViewModel : ObservableObject
{
    private bool _isActive;
    private string _name;

    public EntityNodeViewModel(uint id, string name, bool isActive, IEnumerable<EntityNodeViewModel>? children = null)
    {
        Id = id;
        _name = name;
        _isActive = isActive;
        Children = new ObservableCollection<EntityNodeViewModel>(children ?? []);
    }

    public uint Id { get; }
    public string Name { get => _name; internal set => SetProperty(ref _name, value); }
    public bool IsActive
    {
        get => _isActive;
        internal set
        {
            if (SetProperty(ref _isActive, value)) OnPropertyChanged(nameof(DisplayOpacity));
        }
    }
    public double DisplayOpacity => IsActive ? 1.0 : 0.48;
    public ObservableCollection<EntityNodeViewModel> Children { get; }
}
