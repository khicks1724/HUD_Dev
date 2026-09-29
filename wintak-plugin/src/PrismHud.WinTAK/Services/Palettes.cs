using System;

namespace PrismHud.WinTAK.Services
{
    /// <summary>
    /// Thermal colour maps, 256 x RGB, index = intensity (0 cold, 255 hot).
    /// On the prism black is see-through, so maps that keep cold near black
    /// (White hot, Ironbow, Red hot, Green) keep the view clear.
    /// </summary>
    internal static class Palettes
    {
        internal static readonly string[] Names = { "White hot", "Black hot", "Ironbow", "Rainbow", "Red hot", "Arctic", "Green" };

        internal static byte[] Build(int index)
        {
            var rgb = new byte[768];
            for (var i = 0; i < 256; i++)
            {
                var t = i / 255.0;
                double r, g, b;
                switch (index)
                {
                    case 1: r = g = b = 1 - t; break;
                    case 2: Stops(t, out r, out g, out b, Iron); break;
                    case 3: Stops(t, out r, out g, out b, Rainbow); break;
                    case 4: // grey with the hottest 20 % in red/yellow
                        if (t < 0.8) { r = g = b = t * 0.9; }
                        else { var k = (t - 0.8) / 0.2; r = 1; g = 0.25 + 0.75 * k * k; b = 0.1; }
                        break;
                    case 5: Stops(t, out r, out g, out b, Arctic); break;
                    case 6: r = 0.15 * t; g = t; b = 0.2 * t; break;
                    default: r = g = b = t; break;
                }
                rgb[i * 3] = To8(r);
                rgb[i * 3 + 1] = To8(g);
                rgb[i * 3 + 2] = To8(b);
            }
            return rgb;
        }

        // position, r, g, b
        private static readonly double[,] Iron =
        {
            { 0.00, 0.00, 0.00, 0.00 }, { 0.18, 0.20, 0.00, 0.45 }, { 0.38, 0.65, 0.05, 0.55 },
            { 0.58, 0.93, 0.35, 0.10 }, { 0.80, 1.00, 0.72, 0.00 }, { 1.00, 1.00, 1.00, 0.85 },
        };

        private static readonly double[,] Rainbow =
        {
            { 0.00, 0.00, 0.00, 0.00 }, { 0.12, 0.25, 0.00, 0.55 }, { 0.30, 0.00, 0.30, 1.00 },
            { 0.48, 0.00, 0.85, 0.40 }, { 0.66, 0.95, 0.95, 0.00 }, { 0.84, 1.00, 0.40, 0.00 }, { 1.00, 1.00, 0.10, 0.25 },
        };

        private static readonly double[,] Arctic =
        {
            { 0.00, 0.00, 0.00, 0.00 }, { 0.40, 0.00, 0.20, 0.55 }, { 0.70, 0.20, 0.65, 1.00 },
            { 0.88, 0.95, 0.75, 0.20 }, { 1.00, 1.00, 1.00, 0.90 },
        };

        private static void Stops(double t, out double r, out double g, out double b, double[,] s)
        {
            var n = s.GetLength(0);
            for (var k = 1; k < n; k++)
            {
                if (t > s[k, 0] && k < n - 1) continue;
                var a = (t - s[k - 1, 0]) / (s[k, 0] - s[k - 1, 0]);
                a = Math.Max(0, Math.Min(1, a));
                r = s[k - 1, 1] + (s[k, 1] - s[k - 1, 1]) * a;
                g = s[k - 1, 2] + (s[k, 2] - s[k - 1, 2]) * a;
                b = s[k - 1, 3] + (s[k, 3] - s[k - 1, 3]) * a;
                return;
            }
            r = g = b = t;
        }

        private static byte To8(double v)
        {
            return (byte)Math.Max(0, Math.Min(255, (int)Math.Round(v * 255)));
        }
    }
}
