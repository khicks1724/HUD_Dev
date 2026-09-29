using System;
using System.IO.Ports;
using System.Threading;

namespace PrismHud.WinTAK.Services
{
    /// <summary>
    /// RPX UAV640 thermal camera over its USB serial port, following RPX's
    /// demo code (digital-trident-ir-camera-suite/RPX, camera.py): packets are
    /// 'RpxW' | len u32 | type u16 | ~type u16 | payload. We run the 8-bit
    /// stream (type 0x0B: frame index u32, temperature f32, 640x480 bytes)
    /// with the camera's white-hot equalized palette, so each byte is an
    /// intensity and PRISM applies its own colour map for the HUD.
    /// </summary>
    internal sealed class RpxCamera : IDisposable
    {
        internal const int Width = 640, Height = 480, FrameBytes = Width * Height;

        private const ushort TypeStreamEnable = 0x0007, TypeRaw8 = 0x000B, TypeOsd = 0x000C, TypeZoom = 0x0010,
            TypePalette = 0x0011, TypeEnhance = 0x0012, TypeRotate = 0x0029;
        private const int HeaderLen = 12;
        private static readonly byte[] Magic = { 0x52, 0x70, 0x78, 0x57 }; // "RpxW"

        private SerialPort port;
        private Thread reader;
        private Timer watchdog;
        private volatile bool running, disposed;
        private readonly byte[] frame = new byte[FrameBytes];

        public string PortName { get; private set; }
        public string PreferredPort { get; set; }
        public bool IsOpen => port != null && port.IsOpen;
        public string LastError { get; private set; } = "";
        public float Temperature { get; private set; } = float.NaN;
        public int Dropped { get; private set; }
        private double fps;
        private DateTime lastFrameUtc = DateTime.MinValue, openedUtc, lastConfigUtc;
        /// <summary>Frames per second, 0 once frames stop for 2 s.</summary>
        public double Fps => (DateTime.UtcNow - lastFrameUtc).TotalSeconds < 2 ? fps : 0;
        public bool Streaming => IsOpen && (DateTime.UtcNow - lastFrameUtc).TotalSeconds < 2;
        /// <summary>Raised on the watchdog thread when a camera (re)connects and starts streaming.</summary>
        public event Action StreamStarted;
        private bool announced;
        public int Enhance { get; private set; } = 5;     // AUTO horizontal
        public int Zoom { get; private set; }             // 0 1x, 1 2x, 2 4x
        public double ZoomFactor => Zoom == 1 ? 2.0 : Zoom == 2 ? 4.0 : 1.0;

        /// <summary>640x480 intensity frame; the array is reused, copy what you keep.</summary>
        public event Action<byte[]> FrameReceived;

        public void Start()
        {
            watchdog = new Timer(_ => Watch(), null, 0, 1000);
        }

        /// <summary>
        /// Once a second: find and open the camera; after it opens give it time to
        /// boot, then configure it and keep re-sending the stream command until
        /// frames come; if it is unplugged (port gone) or goes silent, drop the
        /// port and start over, so a camera plugged back in is picked up again.
        /// </summary>
        private void Watch()
        {
            if (disposed) return;
            try
            {
                if (IsOpen)
                {
                    var now = DateTime.UtcNow;
                    var gone = Array.IndexOf(SerialPort.GetPortNames(), PortName) < 0;
                    var silentFor = (now - (lastFrameUtc > openedUtc ? lastFrameUtc : openedUtc)).TotalSeconds;
                    if (gone || silentFor > 15)
                    {
                        LastError = gone ? "RPX camera unplugged" : "RPX camera stopped streaming, reconnecting";
                        Close();
                        return;
                    }
                    if (!Streaming && (now - openedUtc).TotalSeconds > 1.5 && (now - lastConfigUtc).TotalSeconds > 3)
                        Configure(); // first time after boot, then retries until frames arrive
                    if (Streaming && !announced)
                    {
                        announced = true;
                        StreamStarted?.Invoke();
                    }
                    return;
                }
                EnsureOpen();
            }
            catch (Exception e)
            {
                LastError = e.Message;
            }
        }

        private void EnsureOpen()
        {
            if (disposed || IsOpen) return;
            var name = PreferredPort;
            if (string.IsNullOrEmpty(name) || name == "Auto")
            {
                var found = SerialPorts.Find(SerialPorts.RpxVidPids);
                name = found.Count > 0 ? found[0] : null;
            }
            if (name == null)
            {
                LastError = "RPX camera not found on USB";
                return;
            }
            try
            {
                var p = new SerialPort(name, 2000000) { ReadTimeout = 1000, WriteTimeout = 1000, ReadBufferSize = 4 * 1024 * 1024 };
                p.Open();
                // .NET bug: pulling a USB serial device out while it is open makes the
                // stream finalizer throw on the GC thread, which kills the host process
                // (WinTAK). Suppress it here; Close() re-registers it.
                GC.SuppressFinalize(p.BaseStream);
                p.DtrEnable = true; // as RPX's Windows example does
                port = p;
                PortName = name;
                LastError = "";
                openedUtc = DateTime.UtcNow;
                lastConfigUtc = DateTime.MinValue;
                announced = false;
                running = true;
                reader = new Thread(ReadLoop) { IsBackground = true, Name = "PRISM RPX reader" };
                reader.Start();
                // Configure() runs from Watch() once the camera has had time to boot
            }
            catch (Exception e)
            {
                LastError = name + ": " + e.Message;
                Close();
            }
        }

        private void Configure()
        {
            lastConfigUtc = DateTime.UtcNow;
            Send(TypeOsd, 0);              // no camera reticle/logo burned into the image
            Send(TypePalette, 0x05 | (128 << 8)); // 8-bit white hot, equalized (mix 0.5)
            Send(TypeEnhance, Enhance);
            Send(TypeZoom, Zoom);
            Send(TypeStreamEnable, 0x02);  // 8-bit stream
        }

        public void SetEnhance(int level)
        {
            Enhance = level;
            Send(TypeEnhance, level);
        }

        public void SetZoom(int level)
        {
            Zoom = Math.Max(0, Math.Min(2, level));
            Send(TypeZoom, Zoom);
        }

        public void SetRotate180(bool on)
        {
            Send(TypeRotate, on ? 1 : 0);
        }

        public void Reconnect()
        {
            Close();
            ThreadPool.QueueUserWorkItem(_ => EnsureOpen());
        }

        private void Send(ushort type, int value)
        {
            var p = port;
            if (p == null || !p.IsOpen) return;
            var b = new byte[16];
            Buffer.BlockCopy(Magic, 0, b, 0, 4);
            b[4] = 4;
            b[8] = (byte)type;
            b[9] = (byte)(type >> 8);
            var not = (ushort)~type;
            b[10] = (byte)not;
            b[11] = (byte)(not >> 8);
            b[12] = (byte)value;
            b[13] = (byte)(value >> 8);
            b[14] = (byte)(value >> 16);
            b[15] = (byte)(value >> 24);
            try
            {
                lock (this) p.Write(b, 0, b.Length);
            }
            catch (Exception e)
            {
                LastError = e.Message;
            }
        }

        private void ReadExactly(SerialPort p, byte[] buf, int off, int count)
        {
            while (count > 0)
            {
                var n = p.Read(buf, off, count);
                off += n;
                count -= n;
            }
        }

        private void ReadLoop()
        {
            var hdr = new byte[HeaderLen];
            var payload = new byte[FrameBytes + 64];
            var lastIndex = -1L;
            var frames = 0;
            var t0 = DateTime.UtcNow;
            while (running)
            {
                var p = port;
                if (p == null) break;
                try
                {
                    // sync on "RpxW"
                    var matched = 0;
                    while (matched < 4)
                    {
                        var c = p.ReadByte();
                        if (c < 0) continue;
                        matched = c == Magic[matched] ? matched + 1 : (c == Magic[0] ? 1 : 0);
                    }
                    Buffer.BlockCopy(Magic, 0, hdr, 0, 4);
                    ReadExactly(p, hdr, 4, HeaderLen - 4);
                    var len = BitConverter.ToInt32(hdr, 4);
                    var type = BitConverter.ToUInt16(hdr, 8);
                    var notType = BitConverter.ToUInt16(hdr, 10);
                    if ((ushort)~type != notType || len < 0 || len > payload.Length) continue; // not a real header
                    ReadExactly(p, payload, 0, len);
                    if (type != TypeRaw8 || len < 8 + FrameBytes) continue;
                    var index = BitConverter.ToUInt32(payload, 0);
                    if (lastIndex >= 0 && index > lastIndex) Dropped += (int)(index - lastIndex - 1);
                    lastIndex = index;
                    Temperature = BitConverter.ToSingle(payload, 4);
                    Buffer.BlockCopy(payload, 8, frame, 0, FrameBytes);
                    lastFrameUtc = DateTime.UtcNow;
                    FrameReceived?.Invoke(frame);
                    frames++;
                    var dt = (DateTime.UtcNow - t0).TotalSeconds;
                    if (dt >= 2 || fps == 0)
                    {
                        fps = frames / Math.Max(dt, 0.2);
                        frames = 0;
                        t0 = DateTime.UtcNow;
                    }
                }
                catch (TimeoutException)
                {
                    // no data for a second; Watch() decides whether to reconnect
                }
                catch (Exception e)
                {
                    if (running) LastError = e.Message;
                    Close();
                    break;
                }
            }
        }

        private void Close()
        {
            running = false;
            var p = port;
            port = null;
            PortName = null;
            fps = 0;
            if (p == null) return;
            try
            {
                GC.ReRegisterForFinalize(p.BaseStream);
            }
            catch (Exception)
            {
            }
            try
            {
                p.Close();
                p.Dispose();
            }
            catch (Exception)
            {
            }
        }

        public void Dispose()
        {
            disposed = true;
            watchdog?.Dispose();
            Send(TypeStreamEnable, 0);
            Close();
        }
    }
}
