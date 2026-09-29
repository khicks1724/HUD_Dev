using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using Microsoft.Win32;

namespace PrismHud.WinTAK.Services
{
    /// <summary>Finds COM ports by USB VID/PID (from the registry, no WMI needed).</summary>
    internal static class SerialPorts
    {
        internal const string HudVidPid = "VID_1A86&PID_55D3";          // CH343P on the HUD board
        internal static readonly string[] RpxVidPids = { "VID_0483&PID_5740", "VID_0483&PID_A499" };

        /// <summary>COM ports currently present whose USB device matches one of the VID/PIDs.</summary>
        internal static List<string> Find(params string[] vidPids)
        {
            var present = new HashSet<string>(SerialPort.GetPortNames(), StringComparer.OrdinalIgnoreCase);
            var found = new List<string>();
            try
            {
                using (var usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB"))
                {
                    if (usb == null) return found;
                    foreach (var dev in usb.GetSubKeyNames())
                    {
                        if (!vidPids.Any(v => dev.StartsWith(v, StringComparison.OrdinalIgnoreCase))) continue;
                        using (var devKey = usb.OpenSubKey(dev))
                        {
                            if (devKey == null) continue;
                            foreach (var inst in devKey.GetSubKeyNames())
                            {
                                using (var p = devKey.OpenSubKey(inst + @"\Device Parameters"))
                                {
                                    var name = p?.GetValue("PortName") as string;
                                    if (!string.IsNullOrEmpty(name) && present.Contains(name) && !found.Contains(name))
                                        found.Add(name);
                                }
                            }
                        }
                    }
                }
            }
            catch (Exception)
            {
                // registry access denied: fall back to the port the user picks
            }
            return found;
        }
    }
}
