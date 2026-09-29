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
    ///
    /// The stream is ~9 MB/s, so it is read in large chunks and parsed from
    /// memory (resync included); byte-at-a-time reads fall behind for good.
    /// </summary>
    internal sealed class RpxCamera : IDisposable
    {
        internal const int Width = 640, Height = 480, FrameBytes = Width * Height;

        private const ushort TypeStreamEnable = 0x0007, TypeRaw8 = 0x000B, TypeOsd = 0x000C, TypeZoom = 0x0010,
            TypePalette = 0x0011, TypeEnhance = 0x0012;
        private const int HeaderLen = 12, MaxPacket = FrameBytes + 64;
        private const uint Magic = 0x57787052; // "RpxW" little-endian

        private readonly object sendLock = new object();
        private readonly byte[] frame = new byte[FrameBytes];
        private ComPort port;
        private Thread reader;
        private Timer watchdog;
        private volatile bool disposed;
        private int watching;

        private DateTime lastFrameUtc = DateTime.MinValue, openedUtc, lastStartUtc;
        private int starts;
        private bool announced;
        private double fps;

        public string PortName => port?.Name;
        public string PreferredPort { get; set; }
        public bool IsOpen => port != null && port.IsOpen;
        public string LastError { get; private set; } = "";
        public string Status { get; private set; } = "";
        public float Temperature { get; private set; } = float.NaN;
        public int Dropped { get; private set; }
        public int Enhance { get; private set; } = 5;     // AUTO horizontal
        public int Zoom { get; private set; }             // 0 1x, 1 2x, 2 4x
        public double ZoomFactor => Zoom == 1 ? 2.0 : Zoom == 2 ? 4.0 : 1.0;

        /// <summary>Frames per second, 0 once frames stop for 2 s.</summary>
        public double Fps => Streaming ? fps : 0;
        public bool Streaming => IsOpen && (DateTime.UtcNow - lastFrameUtc).TotalSeconds < 2;

        /// <summary>640x480 intensity frame on the reader thread; the array is reused.</summary>
        public event Action<byte[]> FrameReceived;
        /// <summary>A camera (re)connected and frames are flowing.</summary>
        public event Action StreamStarted;

        public void Start()
        {
            watchdog = new Timer(_ => Watch(), null, 0, 1000);
        }

        /// <summary>
        /// Once a second: open the camera when it appears; give it time to boot,
        /// set it up once, then only re-send "start stream" (every 5 s) until
        /// frames flow; if its port disappears or it stays silent, close and
        /// start over, so a camera that is plugged back in comes back by itself.
        /// </summary>
        private void Watch()
        {
            if (disposed || Interlocked.Exchange(ref watching, 1) == 1) return;
            try
            {
                if (!IsOpen)
                {
                    TryOpen();
                    return;
                }
                var now = DateTime.UtcNow;
                if (Array.IndexOf(SerialPort.GetPortNames(), PortName) < 0)
                {
                    Drop("RPX camera unplugged");
                    return;
                }
                if (Streaming)
                {
                    Status = "";
                    if (!announced)
                    {
                        announced = true;
                        StreamStarted?.Invoke();
                    }
                    return;
                }
                var sinceOpen = (now - openedUtc).TotalSeconds;
                var silent = (now - (lastFrameUtc > openedUtc ? lastFrameUtc : openedUtc)).TotalSeconds;
                if (sinceOpen < 2)
                {
                    Status = "RPX camera on " + PortName + ", booting…";
                    return;
                }
                if (starts == 0)
                {
                    Configure();
                }
                else if ((now - lastStartUtc).TotalSeconds >= 5)
                {
                    if (starts >= 4 && silent > 20)
                    {
                        Drop("RPX camera not streaming, reconnecting");
                        return;
                    }
                    StartStream();
                }
                Status = "RPX camera on " + PortName + ", starting its stream…";
            }
            catch (Exception e)
            {
                LastError = e.Message;
            }
            finally
            {
                Interlocked.Exchange(ref watching, 0);
            }
        }

        private void TryOpen()
        {
            var name = PreferredPort;
            if (string.IsNullOrEmpty(name) || name == "Auto")
            {
                var found = SerialPorts.Find(SerialPorts.RpxVidPids);
                name = found.Count > 0 ? found[0] : null;
            }
            if (name == null)
            {
                LastError = "RPX camera not found on USB";
                Status = "";
                return;
            }
            try
            {
                var p = ComPort.Open(name, 2000000, true, false, 8 << 20); // DTR on, as RPX's Windows example
                openedUtc = DateTime.UtcNow;
                lastFrameUtc = DateTime.MinValue;
                starts = 0;
                announced = false;
                LastError = "";
                port = p;
                reader = new Thread(() => ReadLoop(p)) { IsBackground = true, Name = "PRISM RPX reader" };
                reader.Start();
            }
            catch (Exception e)
            {
                LastError = name + ": " + e.Message;
            }
        }

        private void Configure()
        {
            Send(TypeOsd, 0);                     // no camera reticle/logo burned into the image
            Send(TypePalette, 0x05 | (128 << 8)); // 8-bit white hot, equalized (mix 0.5)
            Send(TypeEnhance, Enhance);
            Send(TypeZoom, Zoom);
            StartStream();
        }

        private void StartStream()
        {
            starts++;
            lastStartUtc = DateTime.UtcNow;
            Send(TypeStreamEnable, 0x02); // 8-bit stream
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

        public void Reconnect()
        {
            Drop("");
        }

        private void Send(ushort type, int value)
        {
            var p = port;
            if (p == null || !p.IsOpen) return;
            var b = new byte[16];
            b[0] = 0x52; b[1] = 0x70; b[2] = 0x78; b[3] = 0x57;
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
                lock (sendLock) p.Write(b, 0, b.Length);
            }
            catch (Exception e)
            {
                Drop(e.Message);
            }
        }

        private void ReadLoop(ComPort p)
        {
            // Rolling buffer: a whole packet plus a read chunk.
            const int chunk = 1 << 20;
            var buf = new byte[MaxPacket * 2 + chunk];
            var len = 0;
            long lastIndex = -1;
            var frames = 0;
            var t0 = DateTime.UtcNow;
            while (!disposed && port == p)
            {
                int n;
                try
                {
                    n = p.Read(buf, len, Math.Min(chunk, buf.Length - len));
                }
                catch (Exception e)
                {
                    if (port == p) Drop("RPX camera: " + e.Message);
                    return;
                }
                if (n <= 0) continue;
                len += n;

                var pos = 0;
                while (len - pos >= HeaderLen)
                {
                    if (BitConverter.ToUInt32(buf, pos) != Magic)
                    {
                        var next = Find(buf, pos + 1, len); // resync on the next "RpxW"
                        if (next < 0)
                        {
                            pos = Math.Max(pos, len - 3);
                            break;
                        }
                        pos = next;
                        continue;
                    }
                    var plen = BitConverter.ToInt32(buf, pos + 4);
                    var type = BitConverter.ToUInt16(buf, pos + 8);
                    var notType = BitConverter.ToUInt16(buf, pos + 10);
                    if ((ushort)~type != notType || plen < 0 || plen > MaxPacket)
                    {
                        pos++; // false match, keep looking
                        continue;
                    }
                    if (len - pos < HeaderLen + plen) break; // wait for the rest
                    var body = pos + HeaderLen;
                    if (type == TypeRaw8 && plen >= 8 + FrameBytes)
                    {
                        var index = BitConverter.ToUInt32(buf, body);
                        if (lastIndex >= 0 && index > lastIndex) Dropped += (int)(index - lastIndex - 1);
                        lastIndex = index;
                        Temperature = BitConverter.ToSingle(buf, body + 4);
                        Buffer.BlockCopy(buf, body + 8, frame, 0, FrameBytes);
                        lastFrameUtc = DateTime.UtcNow;
                        try
                        {
                            FrameReceived?.Invoke(frame);
                        }
                        catch (Exception)
                        {
                        }
                        frames++;
                        var dt = (DateTime.UtcNow - t0).TotalSeconds;
                        if (dt >= 1)
                        {
                            fps = frames / dt;
                            frames = 0;
                            t0 = DateTime.UtcNow;
                        }
                    }
                    pos = body + plen;
                }
                if (pos > 0)
                {
                    Buffer.BlockCopy(buf, pos, buf, 0, len - pos);
                    len -= pos;
                }
                if (len > buf.Length - chunk) len = 0; // cannot happen with valid data; start clean
            }
        }

        private static int Find(byte[] b, int from, int to)
        {
            for (var i = from; i + 3 < to; i++)
                if (b[i] == 0x52 && b[i + 1] == 0x70 && b[i + 2] == 0x78 && b[i + 3] == 0x57) return i;
            return -1;
        }

        private void Drop(string why)
        {
            var p = port;
            port = null;
            fps = 0;
            if (!string.IsNullOrEmpty(why)) LastError = why;
            Status = "";
            p?.Dispose();
        }

        public void Dispose()
        {
            disposed = true;
            watchdog?.Dispose();
            Send(TypeStreamEnable, 0);
            Drop("");
        }
    }
}
