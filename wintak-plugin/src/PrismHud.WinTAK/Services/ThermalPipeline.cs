using System;
using System.Threading;

namespace PrismHud.WinTAK.Services
{
    /// <summary>
    /// RPX 640x480 frames -> 128x96 (5x5 average) -> HUD at up to 15 fps.
    /// The HUD places the frame from the camera FOV we send (its own FOV is
    /// ~40 deg, the RPX is 32 deg / zoom), so the heat lines up with the world.
    /// A 128x96 frame is 12.3 kB, ~60 ms on the 2 Mbit/s link, so ~15 fps
    /// fits with room for track lines (160x120 only managed ~8 and felt laggy).
    /// </summary>
    internal sealed class ThermalPipeline
    {
        internal const int OutW = 128, OutH = 96, Step = RpxCamera.Width / OutW; // 5x5 average

        private readonly HudLink hud;
        private readonly RpxCamera camera;
        private readonly byte[] small = new byte[OutW * OutH];
        private readonly byte[] preview = new byte[OutW * OutH];
        private DateTime lastSent = DateTime.MinValue, lastPreview = DateTime.MinValue;
        private int sending; // 1 while a frame is being written

        public double CameraHfovDeg = 32.0;
        public double TargetFps = 15;
        public bool SendToHud = true;
        public int FramesSent;
        public double SentFps;
        private int sentCount;
        private DateTime fpsT0 = DateTime.UtcNow;

        /// <summary>160x120 intensity copy for the pane preview, ~10 Hz.</summary>
        public event Action<byte[]> PreviewReady;

        public ThermalPipeline(HudLink hud, RpxCamera camera)
        {
            this.hud = hud;
            this.camera = camera;
            camera.FrameReceived += OnFrame;
        }

        public double EffectiveHfov => CameraHfovDeg / camera.ZoomFactor;

        /// <summary>Feed a 640x480 frame from somewhere other than the camera (test pattern).</summary>
        internal void Push(byte[] f)
        {
            OnFrame(f);
        }

        private void OnFrame(byte[] f)
        {
            var now = DateTime.UtcNow;
            // the HUD's Map mode shows the map, not thermal: leave the link to the map
            var wantSend = SendToHud && hud.IsOpen && hud.State.Mode != MapCapture.HudModeMap &&
                           (now - lastSent).TotalSeconds >= 1.0 / TargetFps;
            var wantPreview = (now - lastPreview).TotalMilliseconds >= 100;
            if (!wantSend && !wantPreview) return;
            Downscale(f, small);
            if (wantPreview)
            {
                lastPreview = now;
                Buffer.BlockCopy(small, 0, preview, 0, small.Length);
                PreviewReady?.Invoke(preview);
            }
            if (!wantSend || Interlocked.Exchange(ref sending, 1) == 1) return;
            lastSent = now;
            var copy = (byte[])small.Clone();
            var fov = EffectiveHfov;
            ThreadPool.QueueUserWorkItem(_ =>
            {
                try
                {
                    if (hud.SendThermalFrame(copy, OutW, OutH, fov))
                    {
                        FramesSent++;
                        sentCount++;
                        var dt = (DateTime.UtcNow - fpsT0).TotalSeconds;
                        if (dt >= 2)
                        {
                            SentFps = sentCount / dt;
                            sentCount = 0;
                            fpsT0 = DateTime.UtcNow;
                        }
                    }
                }
                finally
                {
                    Interlocked.Exchange(ref sending, 0);
                }
            });
        }

        internal static void Downscale(byte[] src, byte[] dst)
        {
            const int k = Step;
            for (var y = 0; y < OutH; y++)
            {
                for (var x = 0; x < OutW; x++)
                {
                    var sum = 0;
                    var row = y * k * RpxCamera.Width + x * k;
                    for (var dy = 0; dy < k; dy++)
                    {
                        var r = row + dy * RpxCamera.Width;
                        for (var dx = 0; dx < k; dx++) sum += src[r + dx];
                    }
                    dst[y * OutW + x] = (byte)(sum / (k * k));
                }
            }
        }
    }
}
