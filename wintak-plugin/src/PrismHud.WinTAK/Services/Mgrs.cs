using System;
using System.Globalization;

namespace PrismHud.WinTAK.Services
{
    /// <summary>WGS-84 lat/lon -> 10-digit MGRS ("11S NU 86315 07312"), UTM zones only.</summary>
    internal static class Mgrs
    {
        private const string Bands = "CDEFGHJKLMNPQRSTUVWX";
        private const string Col = "ABCDEFGHJKLMNPQRSTUVWXYZ"; // no I, O

        internal static string Format(double lat, double lon)
        {
            if (lat < -80 || lat > 84) return lat.ToString("F5", CultureInfo.InvariantCulture) + ", " + lon.ToString("F5", CultureInfo.InvariantCulture);
            var zone = (int)Math.Floor((lon + 180) / 6) + 1;
            if (lat >= 56 && lat < 64 && lon >= 3 && lon < 12) zone = 32;
            if (lat >= 72)
            {
                if (lon >= 0 && lon < 9) zone = 31;
                else if (lon >= 9 && lon < 21) zone = 33;
                else if (lon >= 21 && lon < 33) zone = 35;
                else if (lon >= 33 && lon < 42) zone = 37;
            }
            ToUtm(lat, lon, zone, out var e, out var n);
            var band = Bands[Math.Min(19, (int)Math.Floor((lat + 80) / 8))];
            var set = (zone - 1) % 6 + 1;
            var colOrigin = (set - 1) % 3 * 8;        // sets 1/4: A, 2/5: J, 3/6: S
            var col = Col[(colOrigin + (int)Math.Floor(e / 100000) - 1) % 24];
            var rowLetters = "ABCDEFGHJKLMNPQRSTUV";
            var rowOffset = set % 2 == 0 ? 5 : 0;
            var row = rowLetters[((int)Math.Floor(n / 100000) + rowOffset) % 20];
            var ee = (int)Math.Floor(e % 100000);
            var nn = (int)Math.Floor(n % 100000);
            return string.Format(CultureInfo.InvariantCulture, "{0}{1} {2}{3} {4:D5} {5:D5}", zone, band, col, row, ee, nn);
        }

        private static void ToUtm(double lat, double lon, int zone, out double easting, out double northing)
        {
            const double a = 6378137.0, f = 1 / 298.257223563, k0 = 0.9996;
            var e2 = f * (2 - f);
            var ep2 = e2 / (1 - e2);
            var phi = lat * Math.PI / 180;
            var lam0 = ((zone - 1) * 6 - 180 + 3) * Math.PI / 180;
            var lam = lon * Math.PI / 180;
            var N = a / Math.Sqrt(1 - e2 * Math.Sin(phi) * Math.Sin(phi));
            var T = Math.Tan(phi) * Math.Tan(phi);
            var C = ep2 * Math.Cos(phi) * Math.Cos(phi);
            var A = Math.Cos(phi) * (lam - lam0);
            var M = a * ((1 - e2 / 4 - 3 * e2 * e2 / 64 - 5 * e2 * e2 * e2 / 256) * phi
                         - (3 * e2 / 8 + 3 * e2 * e2 / 32 + 45 * e2 * e2 * e2 / 1024) * Math.Sin(2 * phi)
                         + (15 * e2 * e2 / 256 + 45 * e2 * e2 * e2 / 1024) * Math.Sin(4 * phi)
                         - (35 * e2 * e2 * e2 / 3072) * Math.Sin(6 * phi));
            easting = k0 * N * (A + (1 - T + C) * A * A * A / 6 + (5 - 18 * T + T * T + 72 * C - 58 * ep2) * Math.Pow(A, 5) / 120) + 500000;
            northing = k0 * (M + N * Math.Tan(phi) * (A * A / 2 + (5 - T + 9 * C + 4 * C * C) * Math.Pow(A, 4) / 24
                                                      + (61 - 58 * T + T * T + 600 * C - 330 * ep2) * Math.Pow(A, 6) / 720));
            if (lat < 0) northing += 10000000;
        }
    }
}
