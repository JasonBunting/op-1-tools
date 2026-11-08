// Listens for the rastprobe plugin's SysEx (F0 7D 'O' 'M' <type> <7-bit packed payload> F7), prints INFO
// messages and assembles CHUNK messages into the raw framebuffer, written as a BMP after each complete sweep.
//   dotnet run                -> lists MIDI inputs, opens the first one whose name contains "OP-1"
//   dotnet run -- "<name>"    -> opens that input
using Melanchall.DryWetMidi.Core;
using Melanchall.DryWetMidi.Multimedia;

// The whole streamed buffer: 400 rows of 512 bytes (256 px), padding included. The visible area is the top-left
// r[1] x r[3] (188 x 376 measured on the settings page).
const int Stride = 512, Width = Stride / 2, Height = 400, SweepBytes = Stride * Height;

var inputs = InputDevice.GetAll().ToList();
Console.WriteLine("MIDI inputs:");
foreach (var d in inputs) Console.WriteLine($"  {d.Name}");

var wanted = args.Length > 0 ? args[0] : "OP-1";
var device = inputs.FirstOrDefault(d => d.Name.Contains(wanted, StringComparison.OrdinalIgnoreCase));
if (device is null)
{
    Console.WriteLine($"No input matching \"{wanted}\".");
    return 1;
}

var frame = new byte[SweepBytes];
var seen = new bool[SweepBytes / 40];
uint lastSeq = 0, lost = 0, chunks = 0, sweeps = 0;
bool haveSeq = false;

device.EventReceived += (_, e) =>
{
    if (e.Event is not NormalSysExEvent sysex) return;
    var d = sysex.Data; // without F0, ends with F7
    if (d.Length < 5 || d[0] != 0x7D || d[1] != 'O' || d[2] != 'M') return;
    var p = Unpack7(d.AsSpan(4, d.Length - 5));
    if (p.Length < 4) return;

    uint seq = U32(p, 0);
    if (haveSeq && seq != lastSeq + 1) lost += seq - lastSeq - 1;
    lastSeq = seq; haveSeq = true;

    switch (d[3])
    {
        case 1 when p.Length >= 56:
            var r = Enumerable.Range(0, 8).Select(i => $"{U32(p, 16 + 4 * i):X8}");
            Console.WriteLine($"{DateTime.Now:HH:mm:ss.fff} INFO seq={seq} frames={U32(p, 4)} passes={U32(p, 8)} sent={U32(p, 12)} " +
                              $"r=[{string.Join(' ', r)}] sum={U32(p, 48):X8}@{U32(p, 52)} chunks={chunks} lost={lost}");
            break;

        case 2 when p.Length >= 12:
            uint offset = U32(p, 8);
            int n = p.Length - 12;
            if (offset + n > SweepBytes) break;
            Array.Copy(p, 12, frame, offset, n);
            chunks++;
            seen[offset / 40] = true;
            if (offset + n == SweepBytes && seen.All(s => s))
            {
                var path = Path.GetFullPath($"frame-{++sweeps:D3}.bmp");
                WriteBmp(path, frame);
                Console.WriteLine($"Wrote {path}");
                Array.Clear(seen);
            }
            break;
    }
};

try
{
    device.StartEventsListening();
}
catch (MidiDeviceException ex)
{
    Console.WriteLine($"Could not open \"{device.Name}\": {ex.Message}");
    Console.WriteLine("Usually another program has it open (a browser that used Web MIDI, a DAW). Close it and retry.");
    return 1;
}
Console.WriteLine($"Listening on \"{device.Name}\". Turn PROBE on in the OP-1 settings. Enter quits.");
Console.ReadLine();
device.StopEventsListening();
device.Dispose();
return 0;

static byte[] Unpack7(ReadOnlySpan<byte> s)
{
    var o = new List<byte>(s.Length);
    for (int i = 0; i < s.Length;)
    {
        byte hi = s[i++];
        for (int k = 0; k < 7 && i < s.Length; k++, i++) o.Add((byte)(s[i] | ((hi >> k) & 1) << 7));
    }
    return o.ToArray();
}

static uint U32(byte[] p, int i) => (uint)(p[i] | p[i + 1] << 8 | p[i + 2] << 16 | p[i + 3] << 24);

// 24-bit BMP of the whole 256x400 buffer, unrotated (the panel's native portrait orientation).
static void WriteBmp(string path, byte[] fb)
{
    int rowBytes = (Width * 3 + 3) & ~3;
    using var w = new BinaryWriter(File.Create(path));
    w.Write((ushort)0x4D42); w.Write(54 + rowBytes * Height); w.Write(0); w.Write(54);
    w.Write(40); w.Write(Width); w.Write(-Height); w.Write((ushort)1); w.Write((ushort)24);
    w.Write(0); w.Write(rowBytes * Height); w.Write(2835); w.Write(2835); w.Write(0); w.Write(0);
    var row = new byte[rowBytes];
    for (int y = 0; y < Height; y++)
    {
        for (int x = 0; x < Width; x++)
        {
            int px = fb[y * Stride + 2 * x] | fb[y * Stride + 2 * x + 1] << 8;
            row[3 * x] = (byte)((px & 0x1F) << 3);
            row[3 * x + 1] = (byte)((px >> 5 & 0x3F) << 2);
            row[3 * x + 2] = (byte)((px >> 11) << 3);
        }
        w.Write(row);
    }
}
