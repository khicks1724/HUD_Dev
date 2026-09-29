using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows;
using TAKEngine.Core;
using WinTak.Common.Services;
using WinTak.CursorOnTarget.Graphics;
using WinTak.CursorOnTarget.Services;
using WinTak.Graphics;
using WinTak.Graphics.Map;

namespace PrismHud.WinTAK.Services
{
    /// <summary>
    /// WinTAK's own position and map units -> HUD console lines, the same lines
    /// the ATAK plugin sends (firmware/main/console_cmds.c):
    ///   fix &lt;lat&gt; &lt;lon&gt; &lt;hae&gt;
    ///   trk &lt;uid&gt; &lt;type&gt; &lt;lat&gt; &lt;lon&gt; &lt;hae&gt; &lt;stale_s&gt; &lt;callsign|-&gt;
    /// Nearest 40 units within range, spaces in names as '_'.
    /// </summary>
    internal sealed class TrackFeeder
    {
        internal const int MaxTracks = 40;
        private static readonly Regex NotAName = new Regex(
            @"^(.*-?\d{1,3}\.\d{3,}.*|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-.*|a-[a-z]-[A-Z](-.*)?)$", RegexOptions.Compiled);

        private readonly ILocationService location;
        private readonly IMapObjectRenderer renderer;
        private readonly IMapGroupManager groups;

        public double MaxRangeM = 20000;

        internal sealed class Snapshot
        {
            public string Fix;
            public double Lat = double.NaN, Lon = double.NaN;
            public readonly List<string> Tracks = new List<string>();
        }

        public TrackFeeder(ILocationService location, IMapObjectRenderer renderer, IMapGroupManager groups)
        {
            this.location = location;
            this.renderer = renderer;
            this.groups = groups;
        }

        /// <summary>Reads WinTAK's map on the UI thread.</summary>
        public Snapshot Collect()
        {
            Snapshot snap = null;
            var d = Application.Current?.Dispatcher;
            if (d == null) return new Snapshot();
            d.Invoke(() => snap = CollectOnUi());
            return snap ?? new Snapshot();
        }

        private Snapshot CollectOnUi()
        {
            var s = new Snapshot();
            GeoPoint me = null;
            string selfUid = "";
            try
            {
                me = location?.GetGpsPosition();
                if (!Valid(me)) me = location?.GetGpsMarker(false)?.Position;
                selfUid = location?.GetSelfCotEvent()?.Uid ?? "";
            }
            catch (Exception)
            {
            }
            if (Valid(me))
            {
                s.Lat = me.Latitude;
                s.Lon = me.Longitude;
                s.Fix = string.Format(CultureInfo.InvariantCulture, "fix {0:F7} {1:F7} {2:F1}", me.Latitude, me.Longitude, Hae(me));
            }

            var seen = new HashSet<MapItem>();
            var found = new List<Tuple<double, string>>();
            // A unit is usually a CompositeMapItem (callsign + "cot-event") whose
            // children draw it (an unnamed CotMapMarker, label, heading...). Take
            // the outermost item that describes a unit and skip its children.
            AllItems(item =>
            {
                if (!seen.Add(item)) return true;
                string type;
                GeoPoint p;
                if (!Describe(item, out type, out p) || !type.StartsWith("a-", StringComparison.Ordinal) || !Valid(p)) return false;
                var uid = item.Uid ?? (item as CotMapMarker)?.CotEvent?.Uid;
                if (string.IsNullOrEmpty(uid) || uid == selfUid || !item.Visible) return true;
                var r = Valid(me) ? Distance(me, p) : 0;
                if (Valid(me) && r > MaxRangeM) return true;
                found.Add(Tuple.Create(r, string.Format(CultureInfo.InvariantCulture,
                    "trk {0} {1} {2:F7} {3:F7} {4:F1} 30 {5}", Token(uid), Token(type), p.Latitude, p.Longitude, Hae(p), Name(item, uid))));
                return true;
            });
            s.Tracks.AddRange(found.OrderBy(t => t.Item1).Take(MaxTracks).Select(t => t.Item2));
            if ((DateTime.UtcNow - lastDump).TotalSeconds > 60) DumpItems(seen);
            return s;
        }

        private DateTime lastDump = DateTime.MinValue;

        /// <summary>What WinTAK's map holds, for diagnosing units that don't reach the HUD:
        /// %APPDATA%\WinTAK\PrismHud\map-items.txt</summary>
        private void DumpItems(IEnumerable<MapItem> items)
        {
            lastDump = DateTime.UtcNow;
            try
            {
                var sb = new System.Text.StringBuilder();
                foreach (var item in items.Take(200))
                {
                    var cot = item as CotMapMarker;
                    var m = item as MapMarkerBase;
                    sb.AppendFormat(CultureInfo.InvariantCulture, "{0} | uid={1} | name={2} | vis={3} | pos={4} | cot={5} | props={6}\r\n",
                        item.GetType().FullName, item.Uid, item.Name, item.Visible,
                        m?.Position == null ? "-" : m.Position.Latitude.ToString("F5", CultureInfo.InvariantCulture) + "," + m.Position.Longitude.ToString("F5", CultureInfo.InvariantCulture),
                        cot?.CotEvent?.Type ?? "-",
                        item.Properties == null ? "-" : string.Join(",", item.Properties.Keys.Take(25)));
                }
                var dir = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "WinTAK", "PrismHud");
                System.IO.Directory.CreateDirectory(dir);
                System.IO.File.WriteAllText(System.IO.Path.Combine(dir, "map-items.txt"), sb.ToString());
            }
            catch (Exception)
            {
            }
        }

        /// <summary>Every map item, including ones nested in map groups (overlays
        /// and other plugins keep their markers inside groups).</summary>
        /// <param name="visit">returns true when the item is handled and its children should be skipped</param>
        private void AllItems(Func<MapItem, bool> visit)
        {
            var stack = new Stack<KeyValuePair<MapItem, int>>();
            if (renderer?.MapItems != null)
                foreach (var pair in renderer.MapItems)
                    if (pair.Value != null) stack.Push(new KeyValuePair<MapItem, int>(pair.Value, 0));
            if (groups?.MapItems != null)
                foreach (var item in groups.MapItems)
                    if (item != null) stack.Push(new KeyValuePair<MapItem, int>(item, 0));
            var visited = 0;
            while (stack.Count > 0 && visited++ < 20000)
            {
                var top = stack.Pop();
                if (visit(top.Key) || top.Value >= 8) continue;
                MapItemCollection children = null;
                try
                {
                    children = top.Key.MapItems;
                }
                catch (Exception)
                {
                }
                if (children == null) continue;
                foreach (var child in children.ToList())
                    if (child != null) stack.Push(new KeyValuePair<MapItem, int>(child, top.Value + 1));
            }
        }

        /// <summary>CoT type and position of a map item: from the CoT marker itself,
        /// or from the "cot-event" XML WinTAK keeps on imported/plugin items.</summary>
        private static bool Describe(MapItem item, out string type, out GeoPoint point)
        {
            type = (item as CotMapMarker)?.CotEvent?.Type;
            point = (item as MapMarkerBase)?.Position;
            object raw = null;
            if ((type == null || !Valid(point)) && item.Properties != null && item.Properties.TryGetValue("cot-event", out raw) && raw != null)
            {
                try
                {
                    var doc = raw as System.Xml.XmlDocument;
                    if (doc == null)
                    {
                        doc = new System.Xml.XmlDocument();
                        doc.LoadXml(Convert.ToString(raw, CultureInfo.InvariantCulture));
                    }
                    var ev = doc.SelectSingleNode("//event") as System.Xml.XmlElement;
                    if (ev != null)
                    {
                        if (type == null) type = ev.GetAttribute("type");
                        var pt = ev.SelectSingleNode("point") as System.Xml.XmlElement;
                        if (!Valid(point) && pt != null &&
                            double.TryParse(pt.GetAttribute("lat"), NumberStyles.Float, CultureInfo.InvariantCulture, out var la) &&
                            double.TryParse(pt.GetAttribute("lon"), NumberStyles.Float, CultureInfo.InvariantCulture, out var lo))
                        {
                            double.TryParse(pt.GetAttribute("hae"), NumberStyles.Float, CultureInfo.InvariantCulture, out var hae);
                            point = new GeoPoint(la, lo) { Altitude = hae > 9e6 ? 0 : hae };
                        }
                    }
                }
                catch (Exception)
                {
                }
            }
            if (type == null && item.Properties != null && item.Properties.TryGetValue("type", out raw) && raw is string t) type = t;
            return !string.IsNullOrEmpty(type) && point != null;
        }

        private static string Name(MapItem item, string uid)
        {
            var candidates = new List<string> { item.Name };
            if (item.Properties != null)
                foreach (var key in new[] { "callsign", "title" })
                    if (item.Properties.TryGetValue(key, out var v) && v != null) candidates.Add(Convert.ToString(v, CultureInfo.InvariantCulture));
            foreach (var c in candidates)
            {
                var t = c?.Trim();
                if (string.IsNullOrEmpty(t) || t == uid || NotAName.IsMatch(t)) continue;
                return Token(t);
            }
            return "-";
        }

        private static string Token(string s)
        {
            return Regex.Replace(s.Trim(), "[\\s\"]+", "_");
        }

        private static bool Valid(GeoPoint p)
        {
            return p != null && !double.IsNaN(p.Latitude) && !double.IsNaN(p.Longitude) &&
                   Math.Abs(p.Latitude) <= 90 && Math.Abs(p.Longitude) <= 180 && (p.Latitude != 0 || p.Longitude != 0);
        }

        private static double Hae(GeoPoint p)
        {
            return double.IsNaN(p.Altitude) ? 0 : p.Altitude;
        }

        private static double Distance(GeoPoint a, GeoPoint b)
        {
            const double R = 6371008.8;
            var p1 = a.Latitude * Math.PI / 180;
            var p2 = b.Latitude * Math.PI / 180;
            var dp = p2 - p1;
            var dl = (b.Longitude - a.Longitude) * Math.PI / 180;
            var h = Math.Sin(dp / 2) * Math.Sin(dp / 2) + Math.Cos(p1) * Math.Cos(p2) * Math.Sin(dl / 2) * Math.Sin(dl / 2);
            return 2 * R * Math.Asin(Math.Min(1, Math.Sqrt(h)));
        }
    }
}
