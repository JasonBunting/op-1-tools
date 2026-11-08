using System.IO;
using System.Text.Json;

namespace Op1Mirror;

/// <summary>What the viewer remembers between launches, in a small JSON file in the user's local app data.</summary>
public sealed class ViewerSettings
{
    public int Budget { get; set; } = 64;
    public int Scan { get; set; } = 64;
    public int Refresh { get; set; } = 1;
    public bool Auto { get; set; } = true;
    public bool WholeFrames { get; set; }
    public bool Palette { get; set; } = true;
    public bool Rle { get; set; } = true;
    public bool WholeZoom { get; set; } = true;
    public bool Topmost { get; set; }
    public bool Maximized { get; set; }
    /// <summary>The window's normal size and place; null until it has been closed once.</summary>
    public double? Left { get; set; }
    public double? Top { get; set; }
    public double? Width { get; set; }
    public double? Height { get; set; }

    public static string DefaultPath { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Op1Mirror", "settings.json");

    /// <summary>The saved settings, or the defaults if the file is missing or unreadable.</summary>
    public static ViewerSettings Load(string path)
    {
        try
        {
            return JsonSerializer.Deserialize<ViewerSettings>(File.ReadAllText(path)) ?? new();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return new();
        }
    }

    /// <summary>Best effort: not being able to save must never stop the viewer from closing.</summary>
    public void Save(string path)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
    }
}
