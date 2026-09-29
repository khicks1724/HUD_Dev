using System;
using System.Threading;
using WinTak.Common.Services;
using WinTak.CursorOnTarget.Services;
using WinTak.Graphics.Map;

namespace PrismHud.WinTAK.Services
{
    /// <summary>
    /// Everything that runs while WinTAK is open: the HUD link, the RPX camera,
    /// the thermal pipeline and the 1 Hz track/position feed. The dock pane is
    /// just a view onto this, so closing it keeps the HUD fed.
    /// </summary>
    internal static class PrismRuntime
    {
        internal static readonly double[] RangesM = { 5000, 10000, 20000, 50000 };

        public static PrismSettings Settings { get; private set; } = new PrismSettings();
        public static HudLink Hud { get; private set; }
        public static RpxCamera Camera { get; private set; }
        public static ThermalPipeline Thermal { get; private set; }
        public static TrackFeeder Feeder { get; private set; }

        public static string FixText { get; private set; } = "—";
        public static int TracksSent { get; private set; }
        public static event Action Tick;

        /// <summary>Moving hot-spot pattern instead of the camera, to check the HUD
        /// link and alignment without an RPX. Only runs while no camera streams.</summary>
        public static volatile bool TestPattern;

        private static Timer timer, testTimer;
        private static readonly byte[] testFrame = new byte[RpxCamera.FrameBytes];
        private static int tick, busy;

        public static void Start(ILocationService location, IMapObjectRenderer renderer, IMapGroupManager groups)
        {
            if (Hud != null) return;
            Settings = PrismSettings.Load();
            Hud = new HudLink { PreferredPort = Settings.HudPort };
            Camera = new RpxCamera { PreferredPort = Settings.CameraPort };
            Camera.SetEnhance(Settings.Enhance);
            Camera.SetZoom(Settings.Zoom);
            Thermal = new ThermalPipeline(Hud, Camera) { CameraHfovDeg = Settings.CameraHfovDeg, TargetFps = Settings.ThermalFps };
            Feeder = new TrackFeeder(location, renderer, groups) { MaxRangeM = RangesM[Settings.RangeIndex] };
            Hud.Connected += OnHudConnected;
            Hud.Start();
            Camera.Start();
            timer = new Timer(_ => Loop(), null, 1500, 1000);
            var app = System.Windows.Application.Current;
            if (app != null) app.Exit += (s, e) => Stop();
            testTimer = new Timer(_ => TestFrame(), null, 1000, 100);
        }

        private static void OnHudConnected()
        {
            Hud.SendPalette(Palettes.Build(Settings.Palette));
            Hud.SendLine("range " + ((int)RangesM[Settings.RangeIndex]).ToString(System.Globalization.CultureInfo.InvariantCulture));
        }

        /// <summary>WinTAK is closing: stop timers and release both COM ports.</summary>
        public static void Stop()
        {
            try
            {
                timer?.Dispose();
                testTimer?.Dispose();
                timer = testTimer = null;
                Camera?.Dispose();
                Hud?.Dispose();
            }
            catch (Exception)
            {
            }
        }

        public static void SetPalette(int index)
        {
            Settings.Palette = index;
            Settings.Save();
            Hud.SendPalette(Palettes.Build(index));
        }

        private static void TestFrame()
        {
            if (!TestPattern || (Camera.IsOpen && Camera.Fps > 1)) return;
            const int W = RpxCamera.Width, H = RpxCamera.Height;
            var t = Environment.TickCount / 1000.0;
            var cx = W * (0.5 + 0.3 * Math.Sin(t * 0.7));
            var cy = H * 0.55;
            for (var y = 0; y < H; y++)
            {
                var sky = y < H / 2;
                var baseV = sky ? 20 + y / 16 : 70 + (y - H / 2) / 6;
                var dy2 = (y - cy) * (y - cy) * 3.0;
                var row = y * W;
                for (var x = 0; x < W; x++)
                {
                    var d = (x - cx) * (x - cx) + dy2;
                    testFrame[row + x] = (byte)(d < 1600 ? 250 - (int)(d / 16) : baseV);
                }
            }
            // a fixed warm vertical post at the boresight, for alignment
            for (var y = H / 2 - 40; y < H / 2 + 40; y++)
                for (var x = W / 2 - 3; x <= W / 2 + 3; x++) testFrame[y * W + x] = 200;
            Thermal.Push(testFrame);
        }

        private static void Loop()
        {
            if (Interlocked.Exchange(ref busy, 1) == 1) return;
            try
            {
                var s = Feeder.Collect();
                FixText = double.IsNaN(s.Lat) ? "No position in WinTAK" : Mgrs.Format(s.Lat, s.Lon);
                if (Hud.IsOpen)
                {
                    if (s.Fix != null) Hud.SendLine(s.Fix);
                    if (tick % 2 == 0)
                    {
                        var n = 0;
                        foreach (var line in s.Tracks)
                        {
                            if (!Hud.SendLine(line)) break;
                            n++;
                        }
                        TracksSent = n;
                    }
                }
                tick++;
            }
            catch (Exception)
            {
            }
            finally
            {
                Interlocked.Exchange(ref busy, 0);
            }
            Tick?.Invoke();
        }
    }
}
