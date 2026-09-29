using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace PrismHud.WinTAK.Services
{
    /// <summary>
    /// A COM port opened straight through Win32 (overlapped), instead of
    /// System.IO.Ports.SerialPort. SerialPort runs a hidden event thread per
    /// port that spins at 100 % CPU (and can crash the host on its finalizer)
    /// when a USB serial device is unplugged, which is what froze WinTAK.
    /// Here a read simply fails once the device is gone and we close it.
    /// Read and Write may be called from different threads at the same time.
    /// </summary>
    internal sealed class ComPort : IDisposable
    {
        private readonly SafeFileHandle handle;
        private readonly FileStream stream;
        private volatile bool closed;

        public string Name { get; }
        public bool IsOpen => !closed;

        private ComPort(string name, SafeFileHandle h)
        {
            Name = name;
            handle = h;
            stream = new FileStream(h, FileAccess.ReadWrite, 1, true);
        }

        /// <param name="dtr">DTR asserted; the HUD needs DTR and RTS released (they reset the ESP32).</param>
        public static ComPort Open(string name, int baud, bool dtr, bool rts, int rxQueue = 1 << 20)
        {
            var h = CreateFile(@"\\.\" + name, GenericRead | GenericWrite, 0, IntPtr.Zero, OpenExisting, FileFlagOverlapped, IntPtr.Zero);
            if (h.IsInvalid) throw new IOException(name + ": " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
            try
            {
                SetupComm(h, rxQueue, 1 << 16);
                var dcb = new Dcb { DCBlength = (uint)Marshal.SizeOf(typeof(Dcb)) };
                if (!GetCommState(h, ref dcb)) throw new Win32Exception(Marshal.GetLastWin32Error());
                dcb.BaudRate = (uint)baud;
                dcb.ByteSize = 8;
                dcb.Parity = 0;
                dcb.StopBits = 0;
                // fBinary=1, no parity check, no CTS/DSR flow, DTR/RTS fixed, no XON/XOFF
                dcb.Flags = 0x1 | (uint)((dtr ? 1 : 0) << 4) | (uint)((rts ? 1 : 0) << 12);
                if (!SetCommState(h, ref dcb)) throw new Win32Exception(Marshal.GetLastWin32Error());
                // return as soon as any byte is there, or after 250 ms with nothing
                var t = new CommTimeouts { ReadIntervalTimeout = uint.MaxValue, ReadTotalTimeoutMultiplier = uint.MaxValue, ReadTotalTimeoutConstant = 250, WriteTotalTimeoutConstant = 2000 };
                if (!SetCommTimeouts(h, ref t)) throw new Win32Exception(Marshal.GetLastWin32Error());
                EscapeCommFunction(h, dtr ? SetDtr : ClrDtr);
                EscapeCommFunction(h, rts ? SetRts : ClrRts);
                PurgeComm(h, 0xF);
                return new ComPort(name, h);
            }
            catch
            {
                h.Dispose();
                throw;
            }
        }

        /// <summary>Up to count bytes; 0 after ~250 ms with no data. Throws once the device is gone.</summary>
        public int Read(byte[] buf, int offset, int count)
        {
            if (closed) throw new IOException(Name + " closed");
            return stream.Read(buf, offset, count);
        }

        public void Write(byte[] buf, int offset, int count)
        {
            if (closed) throw new IOException(Name + " closed");
            stream.Write(buf, offset, count);
        }

        public void Dispose()
        {
            if (closed) return;
            closed = true;
            try
            {
                CancelIoEx(handle, IntPtr.Zero);
            }
            catch (Exception)
            {
            }
            try
            {
                stream.Dispose();
            }
            catch (Exception)
            {
            }
            try
            {
                handle.Dispose();
            }
            catch (Exception)
            {
            }
        }

        // ------------------------------------------------------------ Win32

        private const uint GenericRead = 0x80000000, GenericWrite = 0x40000000, OpenExisting = 3, FileFlagOverlapped = 0x40000000;
        private const uint SetRts = 3, ClrRts = 4, SetDtr = 5, ClrDtr = 6;

        [StructLayout(LayoutKind.Sequential)]
        private struct Dcb
        {
            public uint DCBlength;
            public uint BaudRate;
            public uint Flags;
            public ushort wReserved;
            public ushort XonLim;
            public ushort XoffLim;
            public byte ByteSize;
            public byte Parity;
            public byte StopBits;
            public sbyte XonChar;
            public sbyte XoffChar;
            public sbyte ErrorChar;
            public sbyte EofChar;
            public sbyte EvtChar;
            public ushort wReserved1;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct CommTimeouts
        {
            public uint ReadIntervalTimeout;
            public uint ReadTotalTimeoutMultiplier;
            public uint ReadTotalTimeoutConstant;
            public uint WriteTotalTimeoutMultiplier;
            public uint WriteTotalTimeoutConstant;
        }

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetCommState(SafeFileHandle h, ref Dcb dcb);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetCommState(SafeFileHandle h, ref Dcb dcb);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetCommTimeouts(SafeFileHandle h, ref CommTimeouts t);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetupComm(SafeFileHandle h, int inQueue, int outQueue);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool EscapeCommFunction(SafeFileHandle h, uint func);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool PurgeComm(SafeFileHandle h, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CancelIoEx(SafeFileHandle h, IntPtr overlapped);
    }
}
