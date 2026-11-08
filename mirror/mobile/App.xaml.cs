namespace Op1Mirror.Mobile;

public partial class App : Application
{
	public App()
	{
		InitializeComponent();
	}

	protected override Window CreateWindow(IActivationState? activationState)
	{
		var page = new MirrorPage();
		var window = new Window(page);
		// A page's own appearing events don't cover leaving and returning to the app
		window.Resumed += (_, _) => page.Start();
		window.Stopped += (_, _) => page.Stop();
		window.Destroying += (_, _) => page.Stop();
		return window;
	}
}
