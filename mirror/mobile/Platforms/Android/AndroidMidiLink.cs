using Android.Content;
using Android.Media.Midi;
using Android.OS;

namespace Op1Mirror.Mobile;

/// <summary>
/// The OP-1 over Android's MIDI service (android.media.midi). With the OP-1 plugged into the phone (USB OTG), the
/// system lists it as a USB MIDI device; no USB permission prompt is involved.
/// </summary>
public sealed class AndroidMidiLink : IMidiLink
{
	readonly MidiManager? manager = (MidiManager?)Android.App.Application.Context.GetSystemService(Context.MidiService);
	readonly Handler handler = new(Looper.MainLooper!);
	readonly SysExFramer framer;
	readonly Removal removal;
	MidiDevice? device;
	MidiDeviceInfo? opening;
	DateTime openingSince;
	bool disposed;
	MidiOutputPort? fromOp1;                                  // Android names ports from the device's side
	MidiInputPort? toOp1;

	public event Action<byte[]>? SysEx;
	public bool Connected => toOp1 is not null && fromOp1 is not null;
	public string Status { get; private set; } = "Looking for the OP-1…";

	public AndroidMidiLink()
	{
		framer = new SysExFramer(m => SysEx?.Invoke(m));
		removal = new Removal(this);
#pragma warning disable CA1422                                // the Executor overload needs API 33; this one covers 26..
		manager?.RegisterDeviceCallback(removal, handler);
#pragma warning restore CA1422
	}

	public void TryConnect()
	{
		if (manager is null) { Status = "This phone has no MIDI support."; return; }
		if (Connected) return;
		if (opening is not null && DateTime.UtcNow - openingSince < TimeSpan.FromSeconds(5)) return;
#pragma warning disable CA1422                                // GetDevices: still works, and covers API 26..32
		var info = manager.GetDevices()?.FirstOrDefault(IsOp1);
#pragma warning restore CA1422
		if (info is null) { Status = "No OP-1 found: plug it in with a USB OTG cable."; return; }
		(opening, openingSince) = (info, DateTime.UtcNow);
		Status = "Opening the OP-1…";
		manager.OpenDevice(info, new Opened(this, info), handler);
	}

	static bool IsOp1(MidiDeviceInfo info)
	{
		var p = info.Properties;
		string name = $"{p?.GetString(MidiDeviceInfo.PropertyName)} {p?.GetString(MidiDeviceInfo.PropertyProduct)}";
		return name.Contains("OP-1", StringComparison.OrdinalIgnoreCase) && info.InputPortCount > 0 && info.OutputPortCount > 0;
	}

	void OnOpened(MidiDeviceInfo info, MidiDevice? opened)
	{
		opening = null;
		if (disposed || device is not null) { Quietly(() => opened?.Close()); return; }   // closed or timed out meanwhile
		if (opened is null) { Status = "The OP-1 is busy: close other MIDI apps."; return; }
		device = opened;
		fromOp1 = opened.OpenOutputPort(0);
		toOp1 = opened.OpenInputPort(0);
		if (!Connected) { Close(); Status = "The OP-1 is busy: close other MIDI apps."; return; }
		fromOp1!.Connect(new Receiver(framer));
		Status = $"Connected to \"{info.Properties?.GetString(MidiDeviceInfo.PropertyName)}\".";
	}

	public void Send(byte[] message)
	{
		if (toOp1 is null) return;
		try { toOp1.Send(message, 0, message.Length); }
		catch (Java.Lang.Exception) { Close(); Status = "Lost the OP-1; reconnecting…"; }
	}

	public void Reconnect()
	{
		Close();
		Status = "Reconnecting to the OP-1…";
	}

	void Close()
	{
		var (from, to, dev) = (fromOp1, toOp1, device);
		(fromOp1, toOp1, device) = (null, null, null);
		Quietly(() => from?.Close());                          // these throw when the cable is already gone
		Quietly(() => to?.Close());
		Quietly(() => dev?.Close());
	}

	static void Quietly(Action close)
	{
		try { close(); }
		catch (Java.Lang.Exception) { }
	}

	public void Dispose()
	{
		disposed = true;
		manager?.UnregisterDeviceCallback(removal);
		Close();
	}

	sealed class Opened(AndroidMidiLink link, MidiDeviceInfo info) : Java.Lang.Object, MidiManager.IOnDeviceOpenedListener
	{
		public void OnDeviceOpened(MidiDevice? device) => link.OnOpened(info, device);
	}

	sealed class Removal(AndroidMidiLink link) : MidiManager.DeviceCallback
	{
		public override void OnDeviceRemoved(MidiDeviceInfo? info)
		{
			if (info is null) return;
			if (link.opening?.Id == info.Id) link.opening = null;
			if (link.device?.Info?.Id != info.Id) return;
			link.Close();
			link.Status = "The OP-1 was unplugged.";
		}
	}

	/// <summary>Called on a MIDI thread with whatever bytes arrived; SysEx comes in pieces.</summary>
	sealed class Receiver(SysExFramer framer) : MidiReceiver
	{
		public override void OnSend(byte[]? msg, int offset, int count, long timestamp)
		{
			if (msg is not null) framer.Push(msg.AsSpan(offset, count));
		}
	}
}
