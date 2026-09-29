using System;
using System.IO;
using Newtonsoft.Json;

namespace PrismHud.WinTAK.Services
{
    /// <summary>Plugin settings, %APPDATA%\WinTAK\PrismHud\settings.json.</summary>
    internal sealed class PrismSettings
    {
        public string HudPort = "Auto";
        public string CameraPort = "Auto";
        public double CameraHfovDeg = 32.0;   // RPX UAV640 "C" lens
        public int Palette = 2;               // Ironbow
        public int ThermalFps = 10;
        public int Zoom;                      // 0 1x, 1 2x, 2 4x
        public int Enhance = 5;               // AUTO horizontal
        public int RangeIndex = 2;            // 20 km
        public int ThermalMode = 2;           // HUD thermal view while a camera streams: 1 Full, 2 Hot

        private static string PathName =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "WinTAK", "PrismHud", "settings.json");

        public static PrismSettings Load()
        {
            try
            {
                if (File.Exists(PathName)) return JsonConvert.DeserializeObject<PrismSettings>(File.ReadAllText(PathName)) ?? new PrismSettings();
            }
            catch (Exception)
            {
            }
            return new PrismSettings();
        }

        public void Save()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(PathName));
                File.WriteAllText(PathName, JsonConvert.SerializeObject(this, Formatting.Indented));
            }
            catch (Exception)
            {
            }
        }
    }
}
