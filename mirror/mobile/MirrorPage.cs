using System.Runtime.InteropServices;
using SkiaSharp;
using SkiaSharp.Views.Maui;
using SkiaSharp.Views.Maui.Controls;

namespace Op1Mirror.Mobile;

/// <summary>
/// The OP-1's screen, full screen in landscape. Tap to show or hide the controls, which work as in the Windows viewer
/// (mirror/windows): the plugin's settings go out as control changes on channel 16 (mirror/plugin/mirror.c).
/// The controls' settings are remembered between launches. The MIDI ports are held only while the app is on screen.
/// </summary>
public sealed class MirrorPage : ContentPage
{
	readonly MirrorDecoder decoder = new();
	readonly object gate = new();
	readonly SKCanvasView canvas = new();
	readonly Label status = NewLabel(), notice = NewLabel();
	readonly Slider budget = NewSlider("Budget", 127, 64), scan = NewSlider("Scan", 127, 64), refresh = NewSlider("Refresh", 16, 1);
	readonly Switch auto = NewSwitch("Auto", true), wholeFrames = NewSwitch("Whole frames", false),
	                wholeZoom = NewSwitch("Whole-number zoom", true);
	readonly VerticalStackLayout panel;
	readonly IDispatcherTimer timer;
	IMidiLink? link;
	SKImage? image;
	int[] pixels = [];
	bool dirty = true;
	DateTime lastStatus = DateTime.MinValue, lastConnectTry = DateTime.MinValue, lastData = DateTime.Now;
	long lastBytes, lastLost;
	double bestPasses;

	const int ControlChannel = 15, KeepAliveCc = 118, BudgetCc = 119, ScanCc = 120, ModeCc = 121, RefreshCc = 122;
	const int ModeFrames = 1, ModePalette = 2, ModeRle = 4;
	static readonly TimeSpan NoDataNotice = TimeSpan.FromSeconds(3), NoDataReconnect = TimeSpan.FromSeconds(6);

	public MirrorPage()
	{
		BackgroundColor = Colors.Black;
		canvas.PaintSurface += OnPaint;
		var tap = new TapGestureRecognizer();
		tap.Tapped += (_, _) => panel!.IsVisible = !panel.IsVisible;
		canvas.GestureRecognizers.Add(tap);

		panel = new VerticalStackLayout
		{
			BackgroundColor = Color.FromRgba(0, 0, 0, 0.8), Padding = new Thickness(16, 8), Spacing = 4,
			VerticalOptions = LayoutOptions.End, IsVisible = false,
			Children =
			{
				Row(Labeled("Budget", budget, BudgetCc), Toggle("Auto", auto)),
				Row(Labeled("Scan", scan, ScanCc), Labeled("Refresh", refresh, RefreshCc)),
				Row(Toggle("Whole frames", wholeFrames), Toggle("Whole-number zoom", wholeZoom)),
				status,
			}
		};
		wholeFrames.Toggled += (_, _) =>
		{
			SendMode();
			lock (gate) { decoder.WholeFrames = wholeFrames.IsToggled; dirty = true; }
		};
		wholeZoom.Toggled += (_, _) => canvas.InvalidateSurface();
		decoder.WholeFrames = wholeFrames.IsToggled;
		notice.HorizontalOptions = notice.VerticalOptions = LayoutOptions.Center;
		(notice.BackgroundColor, notice.TextColor, notice.FontSize, notice.Padding) = (Colors.Black, Colors.LightGray, 16, new Thickness(16, 8));
		notice.HorizontalTextAlignment = TextAlignment.Center;
		notice.IsVisible = false;

		Content = new Grid { Children = { canvas, notice, panel } };
		timer = Dispatcher.CreateTimer();
		timer.Interval = TimeSpan.FromMilliseconds(16);
		timer.Tick += (_, _) => Tick();
	}

	protected override void OnAppearing()
	{
		base.OnAppearing();
		Start();
	}

	protected override void OnDisappearing()
	{
		Stop();
		base.OnDisappearing();
	}

	/// <summary>The app came on screen. Safe to call again; see <see cref="App"/>.</summary>
	public void Start()
	{
		if (link is not null) return;
#if ANDROID
		link = new AndroidMidiLink();
#else
		throw new PlatformNotSupportedException("No MIDI link for this platform yet.");
#endif
		link.SysEx += OnSysEx;
		lastData = DateTime.Now;
		lastConnectTry = DateTime.MinValue;
		DeviceDisplay.Current.KeepScreenOn = true;
		timer.Start();
	}

	/// <summary>The app left the screen: the keep-alive stops, so the OP-1 goes quiet, and the ports are free again.</summary>
	public void Stop()
	{
		if (link is null) return;
		timer.Stop();
		DeviceDisplay.Current.KeepScreenOn = false;
		link.SysEx -= OnSysEx;
		link.Dispose();
		link = null;
	}

	static Label NewLabel() => new() { TextColor = Colors.Gray, FontSize = 12 };

	static Slider NewSlider(string name, int max, int value)
	{
		var slider = new Slider
		{
			Maximum = max, Minimum = 0, Value = Math.Clamp(Preferences.Default.Get(name, value), 0, max), WidthRequest = 220,
			MinimumTrackColor = Colors.Gray, MaximumTrackColor = Colors.DimGray, ThumbColor = Colors.LightGray
		};
		slider.ValueChanged += (_, _) => Preferences.Default.Set(name, (int)Math.Round(slider.Value));
		return slider;
	}

	static Switch NewSwitch(string name, bool on)
	{
		var toggle = new Switch { IsToggled = Preferences.Default.Get(name, on) };
		toggle.Toggled += (_, _) => Preferences.Default.Set(name, toggle.IsToggled);
		return toggle;
	}

	static HorizontalStackLayout Row(params View[] views)
	{
		var row = new HorizontalStackLayout { Spacing = 24 };
		foreach (var v in views) row.Children.Add(v);
		return row;
	}

	static HorizontalStackLayout Toggle(string name, Switch toggle) =>
		new() { Spacing = 6, Children = { toggle, new Label { Text = name, TextColor = Colors.Gray, VerticalOptions = LayoutOptions.Center } } };

	HorizontalStackLayout Labeled(string name, Slider slider, int cc)
	{
		var value = new Label { Text = $"{slider.Value:F0}", TextColor = Colors.Gray, WidthRequest = 32, VerticalOptions = LayoutOptions.Center };
		slider.ValueChanged += (_, _) =>
		{
			slider.Value = Math.Round(slider.Value);
			value.Text = $"{slider.Value:F0}";
			Send(cc, (int)slider.Value);
		};
		return new HorizontalStackLayout
		{
			Spacing = 6,
			Children = { new Label { Text = name, TextColor = Colors.Gray, WidthRequest = 56, VerticalOptions = LayoutOptions.Center }, slider, value }
		};
	}

	void OnSysEx(byte[] message)
	{
		lock (gate)
		{
			lastData = DateTime.Now;
			if (decoder.Feed(message)) dirty = true;
		}
	}

	void Tick()
	{
		if (link is null) return;
		if (!link.Connected && DateTime.Now - lastConnectTry > TimeSpan.FromSeconds(2))
		{
			lastConnectTry = DateTime.Now;
			link.TryConnect();                                    // opens asynchronously; the next status tick talks to it
			bestPasses = 0;
		}
		lock (gate)
		{
			if (dirty)
			{
				int lw = decoder.Height, lh = decoder.Width;
				if (pixels.Length != lw * lh) pixels = new int[lw * lh];
				decoder.RenderLandscape(pixels);
				var info = new SKImageInfo(lw, lh, SKColorType.Bgra8888, SKAlphaType.Opaque);
				image?.Dispose();
				image = SKImage.FromPixelCopy(info, MemoryMarshal.AsBytes(pixels.AsSpan()), lw * 4);
				dirty = false;
				canvas.InvalidateSurface();
			}
			if (DateTime.Now - lastStatus >= TimeSpan.FromSeconds(1))
			{
				double secs = Math.Max(0.001, (DateTime.Now - lastStatus).TotalSeconds);
				double kbps = (decoder.Bytes - lastBytes) / 1024.0 / secs;
				var (fps, pps, _) = decoder.Rates;
				lastBytes = decoder.Bytes;
				lastStatus = DateTime.Now;
				if (auto.IsToggled && link.Connected) AdjustBudget(pps);
				lastLost = decoder.Lost;
				Send(KeepAliveCc, 127);
				SendAll();
				string frames = !wholeFrames.IsToggled ? "" :
				                !decoder.HasFrameBuffer ? "   (no frame buffer on the OP-1: live)" :
				                decoder.SendingFrames ? $"   frames {decoder.FramesSent}" : "   (screen too big for whole frames: live)";
				status.Text = $"{link.Status}   {kbps:F1} KB/s   lost {decoder.Lost}   OP-1 {fps:F0} fps   {pps:F0} passes/s{frames}";
				var silent = DateTime.Now - lastData;
				if (!link.Connected) lastData = DateTime.Now;
				else if (silent > NoDataReconnect)                    // connected but nothing arrives: start over
				{
					link.Reconnect();
					lastData = DateTime.Now;
				}
				notice.Text = !link.Connected ? link.Status : "Connected, waiting for the OP-1's screen.\nIs the mirror plugin installed?";
				notice.IsVisible = !link.Connected || silent > NoDataNotice;
			}
		}
	}

	void OnPaint(object? sender, SKPaintSurfaceEventArgs e)
	{
		var c = e.Surface.Canvas;
		c.Clear(SKColors.Black);
		bool whole = wholeZoom.IsToggled;
		lock (gate)
		{
			if (image is null) return;
			var size = e.Info;
			double z = Math.Min((double)size.Width / image.Width, (double)size.Height / image.Height);
			if (whole) z = Math.Max(1, Math.Floor(z));
			float w = (float)(image.Width * z), h = (float)(image.Height * z);
			var dest = SKRect.Create((size.Width - w) / 2, (size.Height - h) / 2, w, h);
			c.DrawImage(image, dest, new SKSamplingOptions(SKFilterMode.Nearest));
		}
	}

	/// <summary>Auto: as the Windows viewer; back off on losses or a slowing OP-1, otherwise creep up.</summary>
	void AdjustBudget(double passesPerSecond)
	{
		bestPasses = Math.Max(bestPasses, passesPerSecond);
		double s = budget.Value;
		if (decoder.Lost > lastLost) s -= 16;
		else if (passesPerSecond > 0 && passesPerSecond < bestPasses * 0.9) s -= 8;
		else s += 2;
		budget.Value = Math.Clamp(s, 0, 127);
	}

	void SendMode() => Send(ModeCc, (wholeFrames.IsToggled ? ModeFrames : 0) | ModePalette | ModeRle);

	void SendAll()
	{
		Send(BudgetCc, (int)budget.Value);
		Send(ScanCc, (int)scan.Value);
		Send(RefreshCc, (int)refresh.Value);
		SendMode();
	}

	void Send(int cc, int value) => link?.Send([(byte)(0xB0 | ControlChannel), (byte)cc, (byte)value]);
}
