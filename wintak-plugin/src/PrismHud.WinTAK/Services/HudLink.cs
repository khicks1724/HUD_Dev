using System;
using System.IO.Ports;
using System.Text;
using System.Threading;
using Newtonsoft.Json.Linq;

namespace PrismHud.WinTAK.Services
{
    /// <summary>Latest "@HUD {json}" state from the HUD (firmware/main/telemetry.c).</summary>
    internal sealed class HudState
    {
        public double Heading = double.NaN, Pitch, Roll;
        public string HeadingSource = "";
        public int Tracks = -1, Link = -1, Mode = -1, Thermal = -1;
        public long Layers = -1;
        public double Range;
        public string ThermalSource = "";
        public double ThermalFps;
        public DateTime ReceivedUtc;
    }

    /// <summary>
    /// The HUD's USB-C serial port (CH343, 2 Mbit/s): console text lines and
    /// binary thermal packets out, "@HUD" state lines in. See
    /// firmware/main/usb_link.h for the packet format.
    /// </summary>
    internal sealed class HudLink : IDisposable
    {
        internal const int Baud = 2000000;
        private const byte PktThermal = 1, PktPalette = 2, PktThermalOff = 3, PktMapJpeg = 4;

        private readonly object writeLock = new object();
        private readonly StringBuilder rx = new StringBuilder();
        private ComPort port;
        private Timer watchdog;
        private volatile bool disposed;
        private int watching;

        public string PortName => port?.Name;
        public string PreferredPort { get; set; } // null/"Auto" = find by VID/PID
        public bool IsOpen => port != null && port.IsOpen;
        public HudState State { get; private set; } = new HudState();
        public string LastError { get; private set; } = "";
        public long BytesSent;

        public event Action Connected;
        public event Action<HudState> StateReceived;

        public void Start()
        {
            watchdog = new Timer(_ => Watch(), null, 0, 1000);
        }

        private void Watch()
        {
            if (disposed || Interlocked.Exchange(ref watching, 1) == 1) return;
            try
            {
                // unplugged (port gone from Windows): drop it so a replug reconnects
                if (IsOpen && Array.IndexOf(SerialPort.GetPortNames(), PortName) < 0) Drop("HUD unplugged");
                if (!IsOpen) TryOpen();
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
                var found = SerialPorts.Find(SerialPorts.HudVidPid);
                name = found.Count > 0 ? found[0] : null;
            }
            if (name == null)
            {
                LastError = "HUD not found on USB";
                return;
            }
            try
            {
                // DTR/RTS drive the ESP32's EN/BOOT through the auto-reset
                // circuit: keep both released so opening the port doesn't reset it.
                var p = ComPort.Open(name, Baud, false, false);
                port = p;
                LastError = "";
                new Thread(() => ReadLoop(p)) { IsBackground = true, Name = "PRISM HUD reader" }.Start();
                SendLine("");
                SendLine("fake off");
                SendLine("stream on 2");
                Connected?.Invoke();
            }
            catch (Exception e)
            {
                LastError = name + ": " + e.Message;
            }
        }

        public void Reconnect()
        {
            Drop("");
        }

        private void Drop(string why)
        {
            var p = port;
            port = null;
            if (!string.IsNullOrEmpty(why)) LastError = why;
            p?.Dispose();
        }

        private void ReadLoop(ComPort p)
        {
            var buf = new byte[8192];
            while (!disposed && port == p)
            {
                int n;
                try
                {
                    n = p.Read(buf, 0, buf.Length);
                }
                catch (Exception e)
                {
                    if (port == p) Drop("HUD: " + e.Message);
                    return;
                }
                if (n <= 0) continue;
                try
                {
                    rx.Append(Encoding.ASCII.GetString(buf, 0, n));
                    int nl;
                    while ((nl = IndexOf(rx, '\n')) >= 0)
                    {
                        var line = rx.ToString(0, nl).TrimEnd('\r');
                        rx.Remove(0, nl + 1);
                        var k = line.IndexOf("@HUD ", StringComparison.Ordinal);
                        if (k >= 0) ParseState(line.Substring(k + 5));
                    }
                    if (rx.Length > 16384) rx.Clear();
                }
                catch (Exception)
                {
                }
            }
        }

        private static int IndexOf(StringBuilder sb, char c)
        {
            for (var i = 0; i < sb.Length; i++)
                if (sb[i] == c) return i;
            return -1;
        }

        private void ParseState(string json)
        {
            try
            {
                var o = JObject.Parse(json);
                var att = (JObject)o["att"];
                var s = new HudState
                {
                    Heading = att.Value<double>("h"),
                    Pitch = att.Value<double>("p"),
                    Roll = att.Value<double>("r"),
                    HeadingSource = att.Value<string>("src") ?? "",
                    Tracks = o.Value<int?>("tracks") ?? -1,
                    Link = o.Value<int?>("link") ?? -1,
                    Mode = o.Value<int?>("mode") ?? -1,
                    Thermal = o.Value<int?>("thermal") ?? -1,
                    Layers = o.Value<long?>("layers") ?? -1,
                    Range = o.Value<double?>("range") ?? 0,
                    ThermalSource = o.Value<string>("thsrc") ?? "",
                    ThermalFps = o.Value<double?>("thfps") ?? 0,
                    ReceivedUtc = DateTime.UtcNow,
                };
                State = s;
                StateReceived?.Invoke(s);
            }
            catch (Exception)
            {
                // partial line
            }
        }

        public bool SendLine(string line)
        {
            return Write(Encoding.ASCII.GetBytes(line + "\n"));
        }

        public bool SendThermalFrame(byte[] px, int w, int h, double hfovDeg)
        {
            return Write(Packet(PktThermal, px, w, h, hfovDeg));
        }

        public bool SendMapJpeg(byte[] jpg, int w, int h)
        {
            return Write(Packet(PktMapJpeg, jpg, w, h, 0));
        }

        public bool SendPalette(byte[] rgb768)
        {
            return Write(Packet(PktPalette, rgb768, 0, 0, 0));
        }

        public bool SendThermalOff()
        {
            return Write(Packet(PktThermalOff, new byte[0], 0, 0, 0));
        }

        internal static byte[] Packet(byte type, byte[] payload, int w, int h, double hfovDeg)
        {
            var len = payload.Length;
            var buf = new byte[2 + 12 + len + 2];
            buf[0] = 0xA5;
            buf[1] = 0x5A;
            buf[2] = type;
            buf[3] = 0;
            Put16(buf, 4, w);
            Put16(buf, 6, h);
            Put16(buf, 8, (int)Math.Round(hfovDeg * 100));
            buf[10] = (byte)len;
            buf[11] = (byte)(len >> 8);
            buf[12] = (byte)(len >> 16);
            buf[13] = (byte)(len >> 24);
            Buffer.BlockCopy(payload, 0, buf, 14, len);
            var sum = 0;
            for (var i = 2; i < 14 + len; i++) sum += buf[i];
            Put16(buf, 14 + len, sum & 0xFFFF);
            return buf;
        }

        private static void Put16(byte[] b, int at, int v)
        {
            b[at] = (byte)v;
            b[at + 1] = (byte)(v >> 8);
        }

        private bool Write(byte[] data)
        {
            var p = port;
            if (p == null || !p.IsOpen) return false;
            lock (writeLock)
            {
                try
                {
                    p.Write(data, 0, data.Length);
                    BytesSent += data.Length;
                    return true;
                }
                catch (Exception e)
                {
                    Drop(e.Message);
                    return false;
                }
            }
        }

        public void Dispose()
        {
            disposed = true;
            watchdog?.Dispose();
            if (IsOpen)
            {
                SendLine("stream off");
                SendThermalOff();
            }
            Drop("");
        }
    }
}
