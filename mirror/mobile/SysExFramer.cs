namespace Op1Mirror.Mobile;

/// <summary>
/// Reassembles SysEx messages from a raw MIDI byte stream that may split them anywhere (Android delivers USB MIDI that
/// way). Real-time bytes inside a message are skipped; any other status byte abandons it.
/// </summary>
public sealed class SysExFramer(Action<byte[]> onMessage)
{
	readonly List<byte> buffer = new(256);
	bool inside;

	public void Push(ReadOnlySpan<byte> bytes)
	{
		foreach (var b in bytes)
		{
			if (b >= 0xF8) continue;                          // real-time: may appear anywhere
			if (b == 0xF0)
			{
				buffer.Clear();
				buffer.Add(b);
				inside = true;
			}
			else if (!inside) continue;
			else if (b == 0xF7)
			{
				buffer.Add(b);
				inside = false;
				onMessage(buffer.ToArray());
			}
			else if (b >= 0x80) inside = false;               // a new status byte: the message was cut short
			else buffer.Add(b);
		}
	}
}
