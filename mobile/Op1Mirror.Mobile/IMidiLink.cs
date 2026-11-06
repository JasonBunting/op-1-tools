namespace Op1Mirror.Mobile;

/// <summary>A two-way MIDI connection to the OP-1, one per platform (Platforms/Android/AndroidMidiLink.cs).</summary>
public interface IMidiLink : IDisposable
{
	/// <summary>Complete SysEx messages (F0 … F7) from the OP-1, raised on a MIDI thread.</summary>
	event Action<byte[]>? SysEx;

	bool Connected { get; }

	/// <summary>A one-line description of the connection for the status line.</summary>
	string Status { get; }

	/// <summary>Looks for the OP-1 and connects if it is there; cheap to call while connected.</summary>
	void TryConnect();

	void Send(byte[] message);

	/// <summary>Drops the connection; the next <see cref="TryConnect"/> opens it afresh.</summary>
	void Reconnect();
}
