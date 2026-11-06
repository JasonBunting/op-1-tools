using Android.App;
using Android.Content.PM;
using Android.OS;
using AndroidX.Core.View;

namespace Op1Mirror.Mobile;

/// <summary>Landscape like the OP-1's screen, with the system bars hidden (swipe from an edge to see them).</summary>
[Activity(Theme = "@style/Maui.SplashTheme", MainLauncher = true, LaunchMode = LaunchMode.SingleTop,
          ScreenOrientation = ScreenOrientation.SensorLandscape,
          ConfigurationChanges = ConfigChanges.ScreenSize | ConfigChanges.Orientation | ConfigChanges.UiMode | ConfigChanges.ScreenLayout |
                                 ConfigChanges.SmallestScreenSize | ConfigChanges.Density |
                                 ConfigChanges.Keyboard | ConfigChanges.KeyboardHidden | ConfigChanges.Navigation)]   // USB plug-ins can raise these
public class MainActivity : MauiAppCompatActivity
{
	public override void OnWindowFocusChanged(bool hasFocus)
	{
		base.OnWindowFocusChanged(hasFocus);
		if (!hasFocus || Window is null) return;
		if (WindowCompat.GetInsetsController(Window, Window.DecorView) is not { } bars) return;
		bars.SystemBarsBehavior = WindowInsetsControllerCompat.BehaviorShowTransientBarsBySwipe;
		bars.Hide(WindowInsetsCompat.Type.SystemBars());
	}
}
