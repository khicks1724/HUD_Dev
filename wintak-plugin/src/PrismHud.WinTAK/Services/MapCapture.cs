using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace PrismHud.WinTAK.Services
{
    /// <summary>
    /// Puts WinTAK's map on the HUD: grabs the middle of the map view as it is
    /// on screen (imagery, unit icons, overlays - whatever WinTAK draws), JPEG
    /// encodes it and sends it as a usb_link map packet. Full 240x240 at ~4 fps
    /// while the HUD is in Map mode, a small 96x96 at 1 fps for the map inset,
    /// nothing otherwise. Because it copies the screen, the map has to be
    /// visible (not minimised or covered).
    /// </summary>
    internal sealed class MapCapture : IDisposable
    {
        internal const int HudModeMap = 4;
        internal const long LayerMapInset = 1 << 10;

        private readonly HudLink hud;
        private readonly Timer timer;
        private readonly ImageCodecInfo jpeg;
        private readonly EncoderParameters quality;
        private IntPtr mainWindow;
        private DateTime lastSent = DateTime.MinValue;
        private int busy;

        public double Zoom = 1.0;       // 1 = the middle square of the map view, 2 = half of that
        public double SentFps { get; private set; }
        public string LastError { get; private set; } = "";
        public bool Active { get; private set; }
        private int count;
        private DateTime fpsT0 = DateTime.UtcNow;

        public MapCapture(HudLink hud)
        {
            this.hud = hud;
            foreach (var c in ImageCodecInfo.GetImageEncoders())
                if (c.FormatID == ImageFormat.Jpeg.Guid) jpeg = c;
            quality = new EncoderParameters(1);
            quality.Param[0] = new EncoderParameter(Encoder.Quality, 72L);
            timer = new Timer(_ => Tick(), null, 1000, 100);
        }

        /// <summary>WinTAK's main window, looked up on the UI thread.</summary>
        public void SetMainWindow(IntPtr hwnd)
        {
            mainWindow = hwnd;
        }

        private void Tick()
        {
            if (Interlocked.Exchange(ref busy, 1) == 1) return;
            try
            {
                var st = hud.State;
                var live = hud.IsOpen && (DateTime.UtcNow - st.ReceivedUtc).TotalSeconds < 3;
                var full = live && st.Mode == HudModeMap;
                var inset = live && !full && st.Layers >= 0 && (st.Layers & LayerMapInset) != 0 && st.Mode == 0;
                Active = full || inset;
                if (!Active || mainWindow == IntPtr.Zero) return;
                var period = full ? 0.25 : 1.0;
                if ((DateTime.UtcNow - lastSent).TotalSeconds < period) return;
                lastSent = DateTime.UtcNow;
                var size = full ? 240 : 96;
                var jpg = Capture(size);
                if (jpg != null && hud.SendMapJpeg(jpg, size, size))
                {
                    count++;
                    var dt = (DateTime.UtcNow - fpsT0).TotalSeconds;
                    if (dt >= 2)
                    {
                        SentFps = count / dt;
                        count = 0;
                        fpsT0 = DateTime.UtcNow;
                    }
                }
            }
            catch (Exception e)
            {
                LastError = e.Message;
            }
            finally
            {
                Interlocked.Exchange(ref busy, 0);
            }
        }

        private byte[] Capture(int size)
        {
            var map = FindMapView();
            if (map == IntPtr.Zero || !GetWindowRect(map, out var r))
            {
                LastError = "WinTAK map view not found";
                return null;
            }
            var w = r.Right - r.Left;
            var h = r.Bottom - r.Top;
            var side = (int)(Math.Min(w, h) / Math.Max(1.0, Zoom));
            if (side < 32)
            {
                LastError = "WinTAK map view too small";
                return null;
            }
            var x = r.Left + (w - side) / 2;
            var y = r.Top + (h - side) / 2;
            using (var grab = new Bitmap(side, side, PixelFormat.Format24bppRgb))
            using (var small = new Bitmap(size, size, PixelFormat.Format24bppRgb))
            {
                using (var g = Graphics.FromImage(grab)) g.CopyFromScreen(x, y, 0, 0, new Size(side, side));
                using (var g = Graphics.FromImage(small))
                {
                    g.InterpolationMode = InterpolationMode.HighQualityBilinear;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.DrawImage(grab, new Rectangle(0, 0, size, size));
                }
                using (var ms = new MemoryStream())
                {
                    small.Save(ms, jpeg, quality); // baseline JPEG, which the HUD's ROM decoder reads
                    LastError = "";
                    return ms.ToArray();
                }
            }
        }

        /// <summary>The map is the largest visible child window of WinTAK's main window
        /// (its OpenGL surface).</summary>
        private IntPtr FindMapView()
        {
            IntPtr best = IntPtr.Zero;
            long bestArea = 0;
            EnumChildWindows(mainWindow, (h, _) =>
            {
                if (!IsWindowVisible(h) || !GetWindowRect(h, out var r)) return true;
                long area = (long)(r.Right - r.Left) * (r.Bottom - r.Top);
                if (area > bestArea)
                {
                    bestArea = area;
                    best = h;
                }
                return true;
            }, IntPtr.Zero);
            return best;
        }

        public void Dispose()
        {
            timer.Dispose();
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Rect
        {
            public int Left, Top, Right, Bottom;
        }

        private delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool IsWindowVisible(IntPtr hwnd);

        [DllImport("user32.dll")]
        private static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    }
}
