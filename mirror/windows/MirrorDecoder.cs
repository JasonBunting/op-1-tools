namespace Op1Mirror;

/// <summary>
/// Decodes the mirror plugin's SysEx (mirror/plugin/mirror.c) into the OP-1's portrait RGB565 framebuffer.
/// Not thread-safe: feed it from one thread and copy the pixels out under a lock.
/// </summary>
public sealed class MirrorDecoder
{
    public const int Stride = 256, MaxHeight = 400, Tile = 8;

    /// <summary>The picture as tiles arrive.</summary>
    public ushort[] Pixels { get; } = new ushort[Stride * MaxHeight];
    /// <summary>The picture at the last FRAME marker: what whole-frame mode shows.</summary>
    public ushort[] Shown { get; } = new ushort[Stride * MaxHeight];
    /// <summary>Show <see cref="Shown"/> instead of <see cref="Pixels"/>.</summary>
    public bool WholeFrames { get; set; }
    public int Width { get; private set; } = 188;
    public int Height { get; private set; } = 376;
    public long Messages { get; private set; }
    public long Lost { get; private set; }
    public long Bytes { get; private set; }
    public uint Frames { get; private set; }
    public uint Passes { get; private set; }
    public uint TilesSent { get; private set; }
    public uint FramesSent { get; private set; }
    /// <summary>The plugin got its whole-frame buffer (STAT status bit 0).</summary>
    public bool HasFrameBuffer { get; private set; }
    /// <summary>The plugin is sending whole frames right now (STAT status bit 1); on a screen larger than its buffer
    /// it stays live, and so does the picture here.</summary>
    public bool SendingFrames { get; private set; }

    bool ShowingFrames => WholeFrames && SendingFrames;

    /// <summary>OP-1-side rates per second, measured between consecutive STAT messages.</summary>
    public (double Fps, double Passes, double Tiles) Rates { get; private set; }

    readonly System.Diagnostics.Stopwatch clock = System.Diagnostics.Stopwatch.StartNew();
    TimeSpan lastStatAt;

    ushort? lastSeq;

    /// <summary>Takes one SysEx message, either whole (F0 … F7) or without F0 as DryWetMIDI delivers it.
    /// Returns true when the picture or geometry changed.</summary>
    public bool Feed(ReadOnlySpan<byte> sysex)
    {
        if (sysex.Length > 0 && sysex[0] == 0xF0) sysex = sysex[1..];
        if (sysex.Length < 5 || sysex[0] != 0x7D || sysex[1] != 'O' || sysex[2] != 'M' || sysex[^1] != 0xF7) return false;
        var p = Unpack7(sysex[4..^1]);
        if (p.Length < 2) return false;

        Messages++;
        Bytes += sysex.Length + 1;
        var seq = (ushort)(p[0] | p[1] << 8);
        if (lastSeq is { } last && seq != (ushort)(last + 1)) Lost += (ushort)(seq - last - 1);
        lastSeq = seq;

        switch (sysex[3])
        {
            case 0x10 when p.Length >= 6:
                int w = p[2] | p[3] << 8, h = p[4] | p[5] << 8;
                if (w == Width && h == Height || w is < 1 or > Stride || h is < 1 or > MaxHeight) return false;
                (Width, Height) = (w, h);
                return true;
            case 0x11:
                return DecodeTiles(p) && !ShowingFrames;
            case 0x12 when p.Length >= 18:
                uint frames = U32(p, 2), passes = U32(p, 6), tiles = U32(p, 14);
                var now = clock.Elapsed;
                double secs = (now - lastStatAt).TotalSeconds;
                if (lastStatAt > TimeSpan.Zero && secs > 0.1 && passes >= Passes)
                    Rates = ((frames - Frames) / secs, (passes - Passes) / secs, (tiles - TilesSent) / secs);
                (Frames, Passes, TilesSent, lastStatAt) = (frames, passes, tiles, now);
                if (p.Length < 22) return false;
                bool was = ShowingFrames;
                (HasFrameBuffer, SendingFrames) = ((U32(p, 18) & 1) != 0, (U32(p, 18) & 2) != 0);
                return ShowingFrames != was;
            case 0x13 when p.Length >= 6:
                FramesSent = U32(p, 2);
                Array.Copy(Pixels, Shown, Pixels.Length);
                return ShowingFrames;
        }
        return false;
    }

    bool DecodeTiles(byte[] p)
    {
        int i = 2;
        bool changed = false;
        Span<ushort> pal = stackalloc ushort[16];
        while (i + 3 <= p.Length)
        {
            int tx = p[i], ty = p[i + 1], n = p[i + 2];
            i += 3;
            if (tx >= Stride / Tile || ty >= MaxHeight / Tile) return changed;
            int origin = ty * Tile * Stride + tx * Tile, k = 0;
            if (n == 0)
            {
                if (i + Tile * Tile * 2 > p.Length) return changed;
                for (; k < Tile * Tile; k++, i += 2) Set(origin, k, (ushort)(p[i] | p[i + 1] << 8));
            }
            else if (n >= 0x80)
            {
                int colours = (n & 0x7F) + 1, bits = colours <= 2 ? 1 : colours <= 4 ? 2 : 4;
                if (colours > 16 || i + 2 * colours + Tile * Tile * bits / 8 > p.Length) return changed;
                for (int j = 0; j < colours; j++, i += 2) pal[j] = (ushort)(p[i] | p[i + 1] << 8);
                for (int j = 0; j < Tile * Tile; j++)
                    Set(origin, j, pal[(p[i + j * bits / 8] >> (j * bits % 8)) & ((1 << bits) - 1)]);
                i += Tile * Tile * bits / 8;
            }
            else
            {
                if (i + n * 3 > p.Length) return changed;
                for (int r = 0; r < n; r++, i += 3)
                {
                    int count = p[i] + 1;
                    var colour = (ushort)(p[i + 1] | p[i + 2] << 8);
                    for (int c = 0; c < count && k < Tile * Tile; c++) Set(origin, k++, colour);
                }
            }
            changed = true;
        }
        return changed;
    }

    void Set(int origin, int k, ushort colour) => Pixels[origin + k / Tile * Stride + k % Tile] = colour;

    /// <summary>The visible area rotated 90° clockwise into the OP-1's landscape view, as BGR32.</summary>
    public void RenderLandscape(int[] bgr32)
    {
        int lw = Height, lh = Width;
        var src = ShowingFrames ? Shown : Pixels;
        for (int ly = 0; ly < lh; ly++)
            for (int lx = 0; lx < lw; lx++)
            {
                ushort px = src[(Height - 1 - lx) * Stride + ly];
                int r = px >> 11, g = px >> 5 & 0x3F, b = px & 0x1F;
                bgr32[ly * lw + lx] = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
            }
    }

    static byte[] Unpack7(ReadOnlySpan<byte> s)
    {
        var o = new byte[s.Length * 7 / 8 + 7];
        int n = 0;
        for (int i = 0; i < s.Length;)
        {
            byte hi = s[i++];
            for (int k = 0; k < 7 && i < s.Length; k++, i++) o[n++] = (byte)(s[i] | ((hi >> k) & 1) << 7);
        }
        return o[..n];
    }

    static uint U32(byte[] p, int i) => (uint)(p[i] | p[i + 1] << 8 | p[i + 2] << 16 | p[i + 3] << 24);
}
