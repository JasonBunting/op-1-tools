// Op1Mirror: shows the OP-1's screen live, from the mirror plugin's SysEx (firmware/mirror).
//   Op1Mirror                          window; connects to the first MIDI input named like "OP-1"
//   Op1Mirror --replay in.syx out.bmp  decodes a recorded stream (firmware/mirror/test) into a landscape BMP
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Key = System.Windows.Input.Key;
using KeyEventArgs = System.Windows.Input.KeyEventArgs;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Melanchall.DryWetMidi.Core;
using Melanchall.DryWetMidi.Multimedia;

namespace Op1Mirror;

static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        if (args.Length == 3 && args[0] == "--replay") return Replay(args[1], args[2]);
        new Application().Run(new MirrorWindow());
        return 0;
    }

    static int Replay(string input, string output)
    {
        var decoder = new MirrorDecoder();
        var data = File.ReadAllBytes(input);
        for (int start = Array.IndexOf(data, (byte)0xF0); start >= 0;)
        {
            int end = Array.IndexOf(data, (byte)0xF7, start);
            if (end < 0) break;
            decoder.Feed(data.AsSpan(start, end - start + 1));
            start = Array.IndexOf(data, (byte)0xF0, end);
        }
        var pixels = new int[decoder.Width * decoder.Height];
        decoder.RenderLandscape(pixels);
        BmpFile.Write(output, pixels, decoder.Height, decoder.Width);
        File.WriteAllText(output + ".txt", $"messages={decoder.Messages} lost={decoder.Lost} bytes={decoder.Bytes} " +
                                           $"frames={decoder.FramesSent}\n");
        return decoder.Lost == 0 ? 0 : 1;
    }
}

sealed class MirrorWindow : Window
{
    readonly MirrorDecoder decoder = new();
    readonly object gate = new();
    readonly Image image = new() { Stretch = Stretch.Uniform };
    readonly Grid view = new();
    readonly StackPanel panel = new();
    readonly TextBlock status = new() { Foreground = Brushes.Gray, Margin = new Thickness(8, 4, 8, 4), FontFamily = new FontFamily("Consolas") };
    readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(16) };
    readonly Slider budget = NewSlider(127, 64), scan = NewSlider(127, 64), refresh = NewSlider(16, 1);
    readonly CheckBox auto = NewCheck("Auto", true), wholeFrames = NewCheck("Whole frames", false),
                      palette = NewCheck("Palette tiles", true), rle = NewCheck("RLE tiles", true),
                      wholeZoom = NewCheck("Whole-number zoom", true);
    WriteableBitmap? bitmap;
    int[] pixels = [];
    bool dirty = true;
    InputDevice? device;
    OutputDevice? output;
    DateTime lastStatus = DateTime.MinValue, lastConnectTry = DateTime.MinValue;
    long lastBytes, lastLost;
    double bestPasses;
    string connection = "Looking for the OP-1…";

    // Control changes on channel 16, heard by the plugin's midi_raw (firmware/mirror/mirror.c)
    const int ControlChannel = 15, KeepAliveCc = 118, BudgetCc = 119, ScanCc = 120, ModeCc = 121, RefreshCc = 122;
    const int ModeFrames = 1, ModePalette = 2, ModeRle = 4;

    public MirrorWindow()
    {
        Title = "OP-1 Mirror";
        Background = Brushes.Black;
        Width = 376 * 3 + 40;
        Height = 188 * 3 + 140;
        RenderOptions.SetBitmapScalingMode(image, BitmapScalingMode.NearestNeighbor);
        Restore(ViewerSettings.Load(ViewerSettings.DefaultPath));

        var sliders = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(8, 4, 8, 0) };
        AddSlider(sliders, "Budget", budget, BudgetCc);
        sliders.Children.Add(auto);
        AddSlider(sliders, "Scan", scan, ScanCc);
        AddSlider(sliders, "Refresh", refresh, RefreshCc);
        var options = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(8, 4, 8, 0) };
        foreach (var box in new[] { wholeFrames, palette, rle })
        {
            options.Children.Add(box);
            box.Click += (_, _) =>
            {
                SendMode();
                lock (gate) { decoder.WholeFrames = wholeFrames.IsChecked == true; dirty = true; }
            };
        }
        options.Children.Add(wholeZoom);
        wholeZoom.Click += (_, _) => Zoom();
        options.Children.Add(new TextBlock { Text = "F11 fullscreen   T on top", Foreground = Brushes.DimGray,
                                             VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(16, 0, 0, 0) });
        panel.Children.Add(sliders);
        panel.Children.Add(options);
        panel.Children.Add(status);

        view.Children.Add(image);
        view.SizeChanged += (_, _) => Zoom();
        var root = new DockPanel();
        DockPanel.SetDock(panel, Dock.Bottom);
        root.Children.Add(panel);
        root.Children.Add(view);
        Content = root;
        KeyDown += OnKey;
        timer.Tick += (_, _) => Tick();
        timer.Start();
        Closing += (_, _) => Remember().Save(ViewerSettings.DefaultPath);
        Closed += (_, _) => Disconnect();
    }

    void Restore(ViewerSettings s)
    {
        (budget.Value, scan.Value, refresh.Value) = (s.Budget, s.Scan, s.Refresh);     // the sliders keep them in range
        (auto.IsChecked, wholeFrames.IsChecked, palette.IsChecked) = (s.Auto, s.WholeFrames, s.Palette);
        (rle.IsChecked, wholeZoom.IsChecked) = (s.Rle, s.WholeZoom);
        decoder.WholeFrames = s.WholeFrames;
        Topmost = s.Topmost;
        if (s is { Left: { } left, Top: { } top, Width: { } width, Height: { } height } && width >= 200 && height >= 150 &&
            OnScreen(left, top, width))
        {
            (WindowStartupLocation, Left, Top, Width, Height) = (WindowStartupLocation.Manual, left, top, width, height);
        }
        if (s.Maximized) WindowState = WindowState.Maximized;
    }

    /// <summary>False when the title bar would be out of reach, say on a monitor that has since been unplugged.</summary>
    static bool OnScreen(double left, double top, double width) =>
        left + width > SystemParameters.VirtualScreenLeft + 100 &&
        left < SystemParameters.VirtualScreenLeft + SystemParameters.VirtualScreenWidth - 100 &&
        top >= SystemParameters.VirtualScreenTop - 10 &&
        top < SystemParameters.VirtualScreenTop + SystemParameters.VirtualScreenHeight - 50;

    ViewerSettings Remember()
    {
        var s = new ViewerSettings
        {
            Budget = (int)budget.Value, Scan = (int)scan.Value, Refresh = (int)refresh.Value,
            Auto = auto.IsChecked == true, WholeFrames = wholeFrames.IsChecked == true, Palette = palette.IsChecked == true,
            Rle = rle.IsChecked == true, WholeZoom = wholeZoom.IsChecked == true, Topmost = Topmost,
            Maximized = WindowState == WindowState.Maximized && WindowStyle != WindowStyle.None    // fullscreen isn't kept
        };
        if (!RestoreBounds.IsEmpty)
            (s.Left, s.Top, s.Width, s.Height) = (RestoreBounds.Left, RestoreBounds.Top, RestoreBounds.Width, RestoreBounds.Height);
        return s;
    }

    static Slider NewSlider(int max, int value) => new()
    {
        Minimum = 0, Maximum = max, Value = value, Width = 160, IsSnapToTickEnabled = true, TickFrequency = 1,
        VerticalAlignment = VerticalAlignment.Center, Focusable = false
    };

    static CheckBox NewCheck(string text, bool on) => new()
    {
        Content = text, IsChecked = on, Foreground = Brushes.Gray, VerticalAlignment = VerticalAlignment.Center,
        Margin = new Thickness(0, 0, 16, 0), Focusable = false
    };

    void AddSlider(Panel to, string name, Slider slider, int cc)
    {
        var label = new TextBlock { Foreground = Brushes.Gray, Width = 32, VerticalAlignment = VerticalAlignment.Center,
                                    Margin = new Thickness(6, 0, 16, 0), FontFamily = new FontFamily("Consolas"),
                                    Text = $"{slider.Value:F0}" };
        to.Children.Add(new TextBlock { Text = name, Foreground = Brushes.Gray, VerticalAlignment = VerticalAlignment.Center,
                                        Margin = new Thickness(0, 0, 6, 0) });
        to.Children.Add(slider);
        to.Children.Add(label);
        slider.ValueChanged += (_, _) => { label.Text = $"{slider.Value:F0}"; Send(cc, (int)slider.Value); };
    }

    void OnKey(object sender, KeyEventArgs e)
    {
        bool full = WindowStyle == WindowStyle.None;
        if (e.Key == Key.F11 || e.Key == Key.Escape && full)
        {
            full = !full;
            WindowStyle = full ? WindowStyle.None : WindowStyle.SingleBorderWindow;
            WindowState = full ? WindowState.Maximized : WindowState.Normal;
            panel.Visibility = full ? Visibility.Collapsed : Visibility.Visible;
        }
        else if (e.Key == Key.T) Topmost = !Topmost;
    }

    /// <summary>Whole-number zoom: every OP-1 pixel becomes an even block, so thin lines and small text stay crisp.</summary>
    void Zoom()
    {
        if (bitmap is null) return;
        if (wholeZoom.IsChecked != true)
        {
            (image.Width, image.Height) = (double.NaN, double.NaN);
            return;
        }
        double z = Math.Max(1, Math.Floor(Math.Min(view.ActualWidth / bitmap.PixelWidth, view.ActualHeight / bitmap.PixelHeight)));
        (image.Width, image.Height) = (bitmap.PixelWidth * z, bitmap.PixelHeight * z);
    }

    void Tick()
    {
        if (device is null && DateTime.Now - lastConnectTry > TimeSpan.FromSeconds(2)) Connect();
        lock (gate)
        {
            if (dirty)
            {
                int lw = decoder.Height, lh = decoder.Width;
                if (bitmap is null || bitmap.PixelWidth != lw || bitmap.PixelHeight != lh)
                {
                    bitmap = new WriteableBitmap(lw, lh, 96, 96, PixelFormats.Bgr32, null);
                    pixels = new int[lw * lh];
                    image.Source = bitmap;
                    Zoom();
                }
                decoder.RenderLandscape(pixels);
                bitmap.WritePixels(new Int32Rect(0, 0, lw, lh), pixels, lw * 4, 0);
                dirty = false;
            }
            if (DateTime.Now - lastStatus >= TimeSpan.FromSeconds(1))
            {
                double secs = Math.Max(0.001, (DateTime.Now - lastStatus).TotalSeconds);
                double kbps = (decoder.Bytes - lastBytes) / 1024.0 / secs;
                var (fps, pps, tps) = decoder.Rates;
                lastBytes = decoder.Bytes;
                lastStatus = DateTime.Now;
                if (auto.IsChecked == true) AdjustBudget(pps);
                lastLost = decoder.Lost;
                Send(KeepAliveCc, 127);
                SendAll();
                string frames = wholeFrames.IsChecked != true ? "" :
                                !decoder.HasFrameBuffer ? "   (no frame buffer on the OP-1: live)" :
                                decoder.SendingFrames ? $"   frames {decoder.FramesSent}" : "   (screen too big for whole frames: live)";
                status.Text = $"{connection}   {kbps,6:F1} KB/s   lost {decoder.Lost}   OP-1: {fps,3:F0} fps  " +
                              $"{pps,4:F0} passes/s  {tps,5:F0} tiles/s   screen {decoder.Width}x{decoder.Height}{frames}";
            }
        }
    }

    /// <summary>Auto: back off hard on losses, gently when the OP-1's main loop slows (passes/s below its best),
    /// otherwise creep up.</summary>
    void AdjustBudget(double passesPerSecond)
    {
        bestPasses = Math.Max(bestPasses, passesPerSecond);
        double s = budget.Value;
        if (decoder.Lost > lastLost) s -= 16;
        else if (passesPerSecond > 0 && passesPerSecond < bestPasses * 0.9) s -= 8;
        else s += 2;
        budget.Value = Math.Clamp(s, 0, 127);
    }

    void SendMode() => Send(ModeCc, (wholeFrames.IsChecked == true ? ModeFrames : 0) |
                                    (palette.IsChecked == true ? ModePalette : 0) | (rle.IsChecked == true ? ModeRle : 0));

    void SendAll()
    {
        Send(BudgetCc, (int)budget.Value);
        Send(ScanCc, (int)scan.Value);
        Send(RefreshCc, (int)refresh.Value);
        SendMode();
    }

    void Send(int cc, int value)
    {
        if (output is null) return;
        try
        {
            output.SendEvent(new ControlChangeEvent((Melanchall.DryWetMidi.Common.SevenBitNumber)cc,
                                                    (Melanchall.DryWetMidi.Common.SevenBitNumber)value)
                             { Channel = (Melanchall.DryWetMidi.Common.FourBitNumber)ControlChannel });
        }
        catch (MidiDeviceException)
        {
            Disconnect();
            connection = "Lost the OP-1; reconnecting…";
        }
    }

    void Connect()
    {
        lastConnectTry = DateTime.Now;
        static bool IsOp1(string name) => name.Contains("OP-1", StringComparison.OrdinalIgnoreCase);
        var input = InputDevice.GetAll().FirstOrDefault(d => IsOp1(d.Name));
        var outDevice = OutputDevice.GetAll().FirstOrDefault(d => IsOp1(d.Name));
        if (input is null || outDevice is null)
        {
            connection = "No OP-1 MIDI ports found.";
            return;
        }
        try
        {
            input.EventReceived += OnEvent;
            input.StartEventsListening();
            outDevice.PrepareForEventsSending();
            (device, output) = (input, outDevice);
            bestPasses = 0;
            connection = $"Connected to \"{input.Name}\".";
            Send(KeepAliveCc, 127);
            SendAll();
        }
        catch (MidiDeviceException)
        {
            input.EventReceived -= OnEvent;
            input.Dispose();
            outDevice.Dispose();
            connection = "OP-1 is busy: close browsers/DAWs using its MIDI port.";
        }
    }

    void Disconnect()
    {
        device?.StopEventsListening();
        device?.Dispose();
        output?.Dispose();
        (device, output) = (null, null);
    }

    void OnEvent(object? sender, MidiEventReceivedEventArgs e)
    {
        if (e.Event is not NormalSysExEvent sysex) return;
        lock (gate)
            if (decoder.Feed(sysex.Data)) dirty = true;
    }
}

static class BmpFile
{
    public static void Write(string path, int[] bgr32, int width, int height)
    {
        int rowBytes = (width * 3 + 3) & ~3;
        using var w = new BinaryWriter(File.Create(path));
        w.Write((ushort)0x4D42); w.Write(54 + rowBytes * height); w.Write(0); w.Write(54);
        w.Write(40); w.Write(width); w.Write(-height); w.Write((ushort)1); w.Write((ushort)24);
        w.Write(0); w.Write(rowBytes * height); w.Write(2835); w.Write(2835); w.Write(0); w.Write(0);
        var row = new byte[rowBytes];
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int c = bgr32[y * width + x];
                row[3 * x] = (byte)c; row[3 * x + 1] = (byte)(c >> 8); row[3 * x + 2] = (byte)(c >> 16);
            }
            w.Write(row);
        }
    }
}
