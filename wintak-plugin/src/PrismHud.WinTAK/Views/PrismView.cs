using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO.Ports;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using PrismHud.WinTAK.Services;

namespace PrismHud.WinTAK.Views
{
    /// <summary>
    /// PRISM dock pane: the same dark prism look as the ATAK plugin, in
    /// Bahnschrift. Links, live HUD attitude, the thermal feed with its
    /// palette, and every HUD display control.
    /// </summary>
    public sealed class PrismView : UserControl
    {
        // theme (same values as the ATAK PrismTheme)
        private static readonly Color Bg = Color.FromRgb(7, 8, 10);
        private static readonly Color Panel = Color.FromRgb(14, 16, 19);
        private static readonly Color PanelHi = Color.FromRgb(22, 25, 29);
        private static readonly Color Line = Color.FromRgb(35, 38, 43);
        private static readonly Color Text = Color.FromRgb(241, 241, 241);
        private static readonly Color Muted = Color.FromRgb(140, 145, 153);
        private static readonly Color Dim = Color.FromRgb(90, 94, 101);
        private static readonly Color Ok = Color.FromRgb(52, 211, 153);
        private static readonly Color Warn = Color.FromRgb(247, 185, 85);
        private static readonly Color Bad = Color.FromRgb(248, 113, 113);
        private static readonly Color[] Spectrum =
        {
            Color.FromRgb(122, 92, 255), Color.FromRgb(61, 169, 255), Color.FromRgb(52, 211, 153),
            Color.FromRgb(255, 230, 92), Color.FromRgb(255, 159, 67), Color.FromRgb(255, 77, 109),
        };
        private static readonly FontFamily Font = new FontFamily("Bahnschrift, Segoe UI");

        private static readonly string[] Modes = { "Normal", "Minimal", "Calib", "Status" };
        private static readonly string[] Layouts = { "Full", "Clean", "Combat", "Nav" };
        private static readonly string[] LayoutCmd = { "full", "clean", "combat", "nav" };
        private static readonly long[] LayoutMask = { 0x1FF, 0x1A5, 0x3E4, 0x11F };
        private static readonly string[] LayerNames = { "Heading tape", "Horizon", "Reticle", "Radar", "Status text",
            "Names", "Ranges", "Target info", "Edge arrows", "Enemy labels only" };
        private static readonly string[] ThermalModes = { "Off", "Full", "Hot" };
        private static readonly string[] Ranges = { "5 km", "10 km", "20 km", "50 km" };
        private static readonly string[] Zooms = { "1x", "2x", "4x" };
        private static readonly string[] Enhances = { "Off", "Auto H", "Auto U" };
        private static readonly int[] EnhanceLevel = { 0, 5, 6 };

        private readonly DispatcherTimer refresh;
        private readonly WriteableBitmap previewBmp = new WriteableBitmap(ThermalPipeline.OutW, ThermalPipeline.OutH, 96, 96, PixelFormats.Bgr32, null);
        private readonly int[] previewPx = new int[ThermalPipeline.OutW * ThermalPipeline.OutH];
        private int[] paletteBgr = new int[256];
        private byte[] pendingPreview;
        private bool previewBlank;

        private Pill hudPill, camPill;
        private TextBlock vHeading, vPitch, vRoll, vFix, vSent, vOnHud, vLink, vNote, vCamInfo;
        private Segmented segMode, segLayout, segThermal, segPalette, segRange, segZoom, segEnhance;
        private readonly List<Chip> chips = new List<Chip>();
        private long shownLayers = 0x1FF;
        private DateTime layersSentUtc = DateTime.MinValue;
        private ComboBox hudPortBox, camPortBox;
        private TextBox fovBox;

        public PrismView()
        {
            Background = new SolidColorBrush(Bg);
            FontFamily = Font;
            Foreground = new SolidColorBrush(Text);
            Content = new ScrollViewer
            {
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
                Content = Build(),
            };
            SetPalette(PrismRuntime.Settings.Palette);
            refresh = new DispatcherTimer(TimeSpan.FromMilliseconds(250), DispatcherPriority.Background, (s, e) => Render(), Dispatcher);
            Loaded += (s, e) =>
            {
                refresh.Start();
                if (PrismRuntime.Thermal != null) PrismRuntime.Thermal.PreviewReady += OnPreview;
            };
            Unloaded += (s, e) =>
            {
                refresh.Stop();
                if (PrismRuntime.Thermal != null) PrismRuntime.Thermal.PreviewReady -= OnPreview;
            };
        }

        // ------------------------------------------------------------ layout

        private UIElement Build()
        {
            var col = new StackPanel { Margin = new Thickness(16, 14, 16, 20) };

            // header: mark + wordmark
            var head = new DockPanel { LastChildFill = true };
            head.Children.Add(Mark());
            var word = T("PRISM", 24, Text, FontWeights.SemiBold);
            word.Margin = new Thickness(12, 0, 0, 0);
            word.VerticalAlignment = VerticalAlignment.Center;
            word.FontStretch = FontStretches.Expanded;
            SetSpacing(word, 8);
            head.Children.Add(word);
            col.Children.Add(head);
            col.Children.Add(Space(12));
            col.Children.Add(SpectrumRule());
            col.Children.Add(Space(14));

            // links
            var linkRow = new DockPanel();
            var linkLabel = Label("Links");
            linkRow.Children.Add(linkLabel);
            var pills = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
            hudPill = new Pill();
            camPill = new Pill { Margin = new Thickness(6, 0, 0, 0) };
            pills.Children.Add(hudPill);
            pills.Children.Add(camPill);
            DockPanel.SetDock(pills, Dock.Right);
            linkRow.Children.Insert(0, pills);
            col.Children.Add(linkRow);
            col.Children.Add(Space(6));
            var ports = new Grid();
            ports.ColumnDefinitions.Add(new ColumnDefinition());
            ports.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(6) });
            ports.ColumnDefinitions.Add(new ColumnDefinition());
            hudPortBox = PortBox(PrismRuntime.Settings.HudPort, v =>
            {
                PrismRuntime.Settings.HudPort = v;
                PrismRuntime.Settings.Save();
                PrismRuntime.Hud.PreferredPort = v;
                PrismRuntime.Hud.Reconnect();
            });
            camPortBox = PortBox(PrismRuntime.Settings.CameraPort, v =>
            {
                PrismRuntime.Settings.CameraPort = v;
                PrismRuntime.Settings.Save();
                PrismRuntime.Camera.PreferredPort = v;
                PrismRuntime.Camera.Reconnect();
            });
            ports.Children.Add(Tile("HUD port", hudPortBox));
            var camTile = Tile("RPX camera port", camPortBox);
            Grid.SetColumn(camTile, 2);
            ports.Children.Add(camTile);
            col.Children.Add(ports);
            col.Children.Add(Space(18));

            // HUD view: attitude + thermal preview
            col.Children.Add(Label("HUD view"));
            col.Children.Add(Space(6));
            var att = Row3(out vHeading, out vPitch, out vRoll, "Heading", "Pitch", "Roll");
            col.Children.Add(att);
            col.Children.Add(Space(6));
            col.Children.Add(PreviewBox());
            col.Children.Add(Space(4));
            vCamInfo = T("", 12, Muted);
            col.Children.Add(vCamInfo);
            col.Children.Add(Space(18));

            // feed
            col.Children.Add(Label("Feed"));
            col.Children.Add(Space(6));
            vFix = Value();
            vFix.FontSize = 16;
            col.Children.Add(Tile("Your position (MGRS) → HUD", vFix));
            col.Children.Add(Space(6));
            col.Children.Add(Row3(out vSent, out vOnHud, out vLink, "Sent", "On HUD", "HUD link"));
            col.Children.Add(Space(8));
            segRange = new Segmented(Ranges, i =>
            {
                PrismRuntime.Settings.RangeIndex = i;
                PrismRuntime.Settings.Save();
                PrismRuntime.Feeder.MaxRangeM = PrismRuntime.RangesM[i];
                Send("range " + ((int)PrismRuntime.RangesM[i]).ToString(CultureInfo.InvariantCulture));
                Send("save");
            });
            segRange.Select(PrismRuntime.Settings.RangeIndex);
            col.Children.Add(segRange);
            col.Children.Add(Space(18));

            // thermal
            col.Children.Add(Label("Thermal"));
            col.Children.Add(Space(6));
            segThermal = new Segmented(ThermalModes, i => PrismRuntime.SetThermalMode(i));
            col.Children.Add(segThermal);
            col.Children.Add(Space(6));
            var test = new Chip("Test pattern (no camera)");
            test.Click += () =>
            {
                PrismRuntime.TestPattern = !PrismRuntime.TestPattern;
                test.On = PrismRuntime.TestPattern;
                if (PrismRuntime.TestPattern && PrismRuntime.Settings.ThermalMode > 0) Send("thermal " + PrismRuntime.Settings.ThermalMode);
            };
            col.Children.Add(test);
            col.Children.Add(Space(8));
            col.Children.Add(Label("Palette"));
            col.Children.Add(Space(6));
            segPalette = new Segmented(Palettes.Names.Take(4).ToArray(), i => { PrismRuntime.SetPalette(i); SetPalette(i); });
            var segPalette2 = new Segmented(Palettes.Names.Skip(4).ToArray(), i => { PrismRuntime.SetPalette(i + 4); SetPalette(i + 4); });
            segPalette.Partner = segPalette2;
            segPalette2.Partner = segPalette;
            var pal = PrismRuntime.Settings.Palette;
            if (pal < 4) segPalette.Select(pal); else segPalette2.Select(pal - 4);
            col.Children.Add(segPalette);
            col.Children.Add(Space(6));
            col.Children.Add(segPalette2);
            col.Children.Add(Space(8));
            var camGrid = new Grid();
            camGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            camGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(6) });
            camGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.4, GridUnitType.Star) });
            fovBox = new TextBox
            {
                Text = PrismRuntime.Settings.CameraHfovDeg.ToString("0.#", CultureInfo.InvariantCulture),
                Background = Brushes.Transparent, Foreground = new SolidColorBrush(Text), BorderThickness = new Thickness(0),
                FontSize = 18, FontFamily = Font, CaretBrush = Brushes.White,
            };
            fovBox.LostFocus += (s, e) => ApplyFov();
            fovBox.KeyDown += (s, e) => { if (e.Key == Key.Enter) ApplyFov(); };
            camGrid.Children.Add(Tile("Camera FOV (deg)", fovBox));
            segZoom = new Segmented(Zooms, i =>
            {
                PrismRuntime.Settings.Zoom = i;
                PrismRuntime.Settings.Save();
                PrismRuntime.Camera.SetZoom(i);
            });
            segZoom.Select(PrismRuntime.Settings.Zoom);
            var zoomTile = Tile("Zoom", segZoom);
            Grid.SetColumn(zoomTile, 2);
            camGrid.Children.Add(zoomTile);
            col.Children.Add(camGrid);
            col.Children.Add(Space(6));
            segEnhance = new Segmented(Enhances, i =>
            {
                PrismRuntime.Settings.Enhance = EnhanceLevel[i];
                PrismRuntime.Settings.Save();
                PrismRuntime.Camera.SetEnhance(EnhanceLevel[i]);
            });
            segEnhance.Select(Array.IndexOf(EnhanceLevel, PrismRuntime.Settings.Enhance));
            col.Children.Add(Tile("Contrast enhance", segEnhance));
            col.Children.Add(Space(6));
            col.Children.Add(AlignSliders());
            col.Children.Add(Space(18));

            // display
            col.Children.Add(Label("Display"));
            col.Children.Add(Space(6));
            segMode = new Segmented(Modes, i => Send("mode " + i));
            col.Children.Add(segMode);
            col.Children.Add(Space(8));
            col.Children.Add(Label("Layout"));
            col.Children.Add(Space(6));
            segLayout = new Segmented(Layouts, i => SetLayers(LayoutMask[i], "show " + LayoutCmd[i]));
            col.Children.Add(segLayout);
            col.Children.Add(Space(8));
            col.Children.Add(Label("On screen"));
            col.Children.Add(Space(6));
            var grid = new UniformGrid { Columns = 2 };
            for (var i = 0; i < LayerNames.Length; i++)
            {
                var bit = 1L << i;
                var chip = new Chip(LayerNames[i]) { Margin = new Thickness(0, 0, i % 2 == 0 ? 3 : 0, 6) };
                if (i % 2 == 1) chip.Margin = new Thickness(3, 0, 0, 6);
                chip.Click += () =>
                {
                    var m = shownLayers ^ bit;
                    SetLayers(m, "show " + m.ToString(CultureInfo.InvariantCulture));
                };
                chips.Add(chip);
                grid.Children.Add(chip);
            }
            col.Children.Add(grid);
            ShowLayers(shownLayers);
            col.Children.Add(Space(8));
            col.Children.Add(Label("Brightness"));
            var bright = new Slider { Minimum = 0, Maximum = 100, Value = 80, Margin = new Thickness(0, 6, 0, 0) };
            bright.PreviewMouseUp += (s, e) => Send("bright " + (int)bright.Value);
            col.Children.Add(bright);
            col.Children.Add(Space(18));

            // align
            col.Children.Add(Label("Align"));
            col.Children.Add(Space(6));
            var a1 = new UniformGrid { Columns = 3 };
            a1.Children.Add(Btn("Level trim", () => { Send("cal trim"); Send("save"); Note("Level trim saved: hold the HUD level on the true horizon when you click it."); }, 0));
            a1.Children.Add(Btn("Flip IMU", () => { Send("cal flip"); Send("save"); Note("IMU turned 180° and saved: use if pitch and roll move the wrong way. Click again to undo."); }, 1));
            a1.Children.Add(Btn("Save", () => { Send("save"); Note("Saved on the HUD."); }, 2));
            col.Children.Add(a1);
            col.Children.Add(Space(10));
            vNote = T("", 12, Muted);
            vNote.TextWrapping = TextWrapping.Wrap;
            col.Children.Add(vNote);
            col.Children.Add(Space(12));
            var help = T("Plug the HUD and the RPX camera into this laptop's USB ports; both are found automatically. " +
                         "WinTAK units within the range and your WinTAK position go to the HUD every 2 s. " +
                         "The thermal image is placed on the HUD from the camera FOV; fine-tune with the alignment sliders.", 12, Muted);
            help.TextWrapping = TextWrapping.Wrap;
            col.Children.Add(help);
            return col;
        }

        private UIElement PreviewBox()
        {
            // 4:3 camera image with a reticle, framed like the HUD
            var img = new Image { Source = previewBmp, Stretch = Stretch.Uniform };
            RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.Linear);
            var overlay = new Canvas { IsHitTestVisible = false };
            var root = new Grid { Background = Brushes.Black, Height = 240 };
            root.Children.Add(img);
            root.Children.Add(overlay);
            root.SizeChanged += (s, e) =>
            {
                overlay.Children.Clear();
                var cx = e.NewSize.Width / 2;
                var cy = e.NewSize.Height / 2;
                foreach (var l in new[] { new[] { cx - 12, cy, cx - 4, cy }, new[] { cx + 4, cy, cx + 12, cy }, new[] { cx, cy - 12, cx, cy - 4 }, new[] { cx, cy + 4, cx, cy + 12 } })
                    overlay.Children.Add(new System.Windows.Shapes.Line { X1 = l[0], Y1 = l[1], X2 = l[2], Y2 = l[3], Stroke = Brushes.White, StrokeThickness = 1.4 });
            };
            return new Border { Child = root, BorderBrush = new SolidColorBrush(Line), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(4) };
        }

        private UIElement AlignSliders()
        {
            var cfg = new StackPanel();
            var sx = MiniSlider(cfg, "Thermal shift X (px)", -60, 60);
            var sy = MiniSlider(cfg, "Thermal shift Y (px)", -60, 60);
            var sr = MiniSlider(cfg, "Thermal roll (deg)", -10, 10);
            Action send = () => Send(string.Format(CultureInfo.InvariantCulture, "thal {0:F1} {1:F1} {2:F2}", sx.Value, sy.Value, sr.Value));
            foreach (var s in new[] { sx, sy, sr })
                s.PreviewMouseUp += (o, e) => { send(); Send("save"); };
            return Tile("Thermal alignment", cfg);
        }

        private Slider MiniSlider(Panel host, string label, double min, double max)
        {
            var row = new DockPanel { Margin = new Thickness(0, 2, 0, 2) };
            var t = T(label, 12, Muted);
            t.Width = 140;
            row.Children.Add(t);
            var val = T("0", 12, Text);
            val.Width = 40;
            val.TextAlignment = TextAlignment.Right;
            DockPanel.SetDock(val, Dock.Right);
            row.Children.Insert(0, val);
            var s = new Slider { Minimum = min, Maximum = max, Value = 0, SmallChange = 0.5 };
            s.ValueChanged += (o, e) => val.Text = s.Value.ToString("0.0", CultureInfo.InvariantCulture);
            row.Children.Add(s);
            host.Children.Add(row);
            return s;
        }

        // ------------------------------------------------------------ actions

        private static void Send(string line)
        {
            PrismRuntime.Hud?.SendLine(line);
        }

        private void Note(string text)
        {
            vNote.Text = text;
        }

        private void ApplyFov()
        {
            if (!double.TryParse(fovBox.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) || v < 5 || v > 120)
            {
                fovBox.Text = PrismRuntime.Settings.CameraHfovDeg.ToString("0.#", CultureInfo.InvariantCulture);
                return;
            }
            PrismRuntime.Settings.CameraHfovDeg = v;
            PrismRuntime.Settings.Save();
            PrismRuntime.Thermal.CameraHfovDeg = v;
        }

        private void SetLayers(long mask, string cmd)
        {
            layersSentUtc = DateTime.UtcNow;
            Send(cmd);
            Send("save");
            ShowLayers(mask);
        }

        private void ShowLayers(long mask)
        {
            shownLayers = mask;
            for (var i = 0; i < chips.Count; i++) chips[i].On = (mask & (1L << i)) != 0;
            var preset = Array.IndexOf(LayoutMask, mask);
            if (segLayout == null) return;
            if (preset >= 0) segLayout.Select(preset);
            else segLayout.Clear();
        }

        private void SetPalette(int index)
        {
            var rgb = Palettes.Build(index);
            var p = new int[256];
            for (var i = 0; i < 256; i++) p[i] = rgb[i * 3] << 16 | rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
            paletteBgr = p;
        }

        private void OnPreview(byte[] px)
        {
            var copy = (byte[])px.Clone();
            pendingPreview = copy;
            Dispatcher.BeginInvoke(new Action(() =>
            {
                var f = pendingPreview;
                if (f == null) return;
                pendingPreview = null;
                previewBlank = false;
                var pal = paletteBgr;
                for (var i = 0; i < f.Length; i++) previewPx[i] = pal[f[i]];
                previewBmp.WritePixels(new Int32Rect(0, 0, ThermalPipeline.OutW, ThermalPipeline.OutH), previewPx, ThermalPipeline.OutW * 4, 0);
            }), DispatcherPriority.Background);
        }

        private void Render()
        {
            var hud = PrismRuntime.Hud;
            var cam = PrismRuntime.Camera;
            if (hud == null) return;
            var st = hud.State;
            var live = hud.IsOpen && (DateTime.UtcNow - st.ReceivedUtc).TotalSeconds < 3;
            if (live) hudPill.Set("HUD " + hud.PortName, Ok);
            else if (hud.IsOpen) hudPill.Set("HUD waiting", Warn);
            else hudPill.Set("No HUD", Bad);
            if (cam.Streaming) camPill.Set("RPX " + cam.Fps.ToString("0", CultureInfo.InvariantCulture) + " fps", Ok);
            else if (cam.IsOpen) camPill.Set("RPX waiting", Warn);
            else camPill.Set("No camera", Bad);
            hudPill.ToolTip = hud.LastError;
            camPill.ToolTip = cam.LastError;

            vHeading.Text = live ? ((int)Math.Round(st.Heading) % 360).ToString("000", CultureInfo.InvariantCulture) + "°" : "—";
            vPitch.Text = live ? st.Pitch.ToString("+0.0;-0.0", CultureInfo.InvariantCulture) + "°" : "—";
            vRoll.Text = live ? st.Roll.ToString("+0.0;-0.0", CultureInfo.InvariantCulture) + "°" : "—";
            vFix.Text = PrismRuntime.FixText;
            vSent.Text = PrismRuntime.TracksSent.ToString(CultureInfo.InvariantCulture);
            vOnHud.Text = live && st.Tracks >= 0 ? st.Tracks.ToString(CultureInfo.InvariantCulture) : "—";
            vLink.Text = !live ? "—" : st.Link == 2 ? "TAK+USB" : st.Link == 1 ? "WI-FI+USB" : "USB";

            // no camera and no test pattern: blank the preview instead of freezing the last frame
            if (!cam.Streaming && !PrismRuntime.TestPattern && !previewBlank)
            {
                Array.Clear(previewPx, 0, previewPx.Length);
                previewBmp.WritePixels(new Int32Rect(0, 0, ThermalPipeline.OutW, ThermalPipeline.OutH), previewPx, ThermalPipeline.OutW * 4, 0);
                previewBlank = true;
            }
            var th = PrismRuntime.Thermal;
            vCamInfo.Text = cam.Streaming
                ? string.Format(CultureInfo.InvariantCulture, "Camera {0:0.#}° HFOV ({1:0.#}° at {2}x) · to HUD {3:0.0} fps{4}{5}",
                    th.CameraHfovDeg, th.EffectiveHfov, cam.ZoomFactor, th.SentFps,
                    live && st.ThermalSource == "usb" ? " · HUD showing it" : live ? " · HUD not showing it yet" : "",
                    float.IsNaN(cam.Temperature) ? "" : string.Format(CultureInfo.InvariantCulture, " · core {0:0}°C", cam.Temperature))
                : cam.IsOpen ? "RPX camera on " + cam.PortName + ", starting its stream…" : cam.LastError;

            if (live)
            {
                if (st.Mode >= 0) segMode.Select(st.Mode);
                if (st.Thermal >= 0) segThermal.Select(st.Thermal);
                if (st.Layers >= 0 && (DateTime.UtcNow - layersSentUtc).TotalSeconds > 2.5) ShowLayers(st.Layers);
            }
        }

        // ------------------------------------------------------------ widgets

        private static TextBlock T(string s, double size, Color c, FontWeight? w = null)
        {
            return new TextBlock { Text = s, FontSize = size, Foreground = new SolidColorBrush(c), FontFamily = Font, FontWeight = w ?? FontWeights.Normal };
        }

        private static void SetSpacing(TextBlock t, double px)
        {
            // WPF has no letter spacing; approximate with thin spaces for the wordmark
            t.Text = string.Join(" ", t.Text.ToCharArray());
        }

        private static TextBlock Label(string s)
        {
            var t = T(s.ToUpperInvariant(), 11, Muted, FontWeights.SemiBold);
            t.FontStretch = FontStretches.Condensed;
            t.Text = string.Join(" ", t.Text.ToCharArray());
            return t;
        }

        private static TextBlock Value()
        {
            var t = T("—", 20, Text, FontWeights.SemiBold);
            t.FontStretch = FontStretches.Condensed;
            return t;
        }

        private static FrameworkElement Space(double h)
        {
            return new Border { Height = h };
        }

        private static Border Box(Color fill, Color stroke, double radius)
        {
            return new Border
            {
                Background = new SolidColorBrush(fill),
                BorderBrush = new SolidColorBrush(stroke),
                BorderThickness = new Thickness(stroke.A == 0 ? 0 : 1),
                CornerRadius = new CornerRadius(radius),
            };
        }

        private static Border Tile(string label, UIElement value)
        {
            var b = Box(Panel, Line, 4);
            b.Padding = new Thickness(10, 8, 10, 9);
            var sp = new StackPanel();
            sp.Children.Add(Label(label));
            sp.Children.Add(Space(4));
            sp.Children.Add(value);
            b.Child = sp;
            return b;
        }

        private Grid Row3(out TextBlock a, out TextBlock b, out TextBlock c, string la, string lb, string lc)
        {
            var g = new Grid();
            for (var i = 0; i < 5; i++)
                g.ColumnDefinitions.Add(new ColumnDefinition { Width = i % 2 == 1 ? new GridLength(6) : new GridLength(1, GridUnitType.Star) });
            a = Value();
            b = Value();
            c = Value();
            var ta = Tile(la, a);
            var tb = Tile(lb, b);
            var tc = Tile(lc, c);
            Grid.SetColumn(tb, 2);
            Grid.SetColumn(tc, 4);
            g.Children.Add(ta);
            g.Children.Add(tb);
            g.Children.Add(tc);
            return g;
        }

        private static Border Btn(string text, Action onClick, int column)
        {
            var b = Box(PanelHi, Color.FromRgb(70, 74, 80), 5);
            b.Margin = new Thickness(column == 0 ? 0 : 3, 0, column == 2 ? 0 : 3, 0);
            b.Padding = new Thickness(8, 10, 8, 10);
            b.Cursor = Cursors.Hand;
            var t = T(text.ToUpperInvariant(), 13, Text, FontWeights.SemiBold);
            t.FontStretch = FontStretches.Condensed;
            t.HorizontalAlignment = HorizontalAlignment.Center;
            b.Child = t;
            b.MouseLeftButtonUp += (s, e) => onClick();
            return b;
        }

        private ComboBox PortBox(string current, Action<string> onPick)
        {
            var box = new ComboBox { FontFamily = Font, FontSize = 14, IsEditable = false };
            Action fill = () =>
            {
                var sel = box.SelectedItem as string ?? current;
                box.Items.Clear();
                box.Items.Add("Auto");
                foreach (var p in SerialPort.GetPortNames().OrderBy(p => p.Length).ThenBy(p => p)) box.Items.Add(p);
                box.SelectedItem = box.Items.Contains(sel) ? sel : "Auto";
            };
            fill();
            box.DropDownOpened += (s, e) => fill();
            box.SelectionChanged += (s, e) =>
            {
                var v = box.SelectedItem as string;
                if (v != null && v != current)
                {
                    current = v;
                    onPick(v);
                }
            };
            return box;
        }

        private static UIElement SpectrumRule()
        {
            var stops = new GradientStopCollection { new GradientStop(Color.FromArgb(0, 255, 255, 255), 0), new GradientStop(Colors.White, 0.2) };
            for (var i = 0; i < Spectrum.Length; i++) stops.Add(new GradientStop(Spectrum[i], 0.35 + 0.65 * i / (Spectrum.Length - 1)));
            return new Rectangle { Height = 2, Fill = new LinearGradientBrush(stops, 0) };
        }

        private static UIElement Mark()
        {
            // same drawing as the toolbar icon: HUD brackets, prism, beam in, spectrum out
            var c = new Canvas { Width = 40, Height = 40 };
            var g = new Canvas { RenderTransform = new ScaleTransform(40 / 48.0, 40 / 48.0) };
            Action<string, Color, double> path = (d, col, w) => g.Children.Add(new Path
            {
                Data = Geometry.Parse(d), Stroke = new SolidColorBrush(col), StrokeThickness = w,
                StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round, StrokeLineJoin = PenLineJoin.Round,
            });
            path("M5,13 V5 H13 M35,5 H43 V13 M43,35 V43 H35 M13,43 H5 V35", Colors.White, 2.6);
            path("M21,13 L29.5,31 L12.5,31 Z", Colors.White, 2.6);
            path("M7,25.5 L16.4,23.8", Colors.White, 2.4);
            path("M25.3,22 L39,16.5", Spectrum[1], 2);
            path("M25.3,22 L39.5,22.5", Spectrum[3], 2);
            path("M25.3,22 L39,28.5", Spectrum[5], 2);
            c.Children.Add(g);
            return c;
        }

        private sealed class Pill : Border
        {
            private readonly Ellipse dot = new Ellipse { Width = 7, Height = 7 };
            private readonly TextBlock text = T("", 10.5, Text, FontWeights.SemiBold);

            public Pill()
            {
                Background = new SolidColorBrush(Panel);
                BorderBrush = new SolidColorBrush(Line);
                BorderThickness = new Thickness(1);
                CornerRadius = new CornerRadius(10);
                Padding = new Thickness(8, 3, 9, 3);
                text.Margin = new Thickness(6, 0, 0, 0);
                text.MaxWidth = 130;
                text.TextTrimming = TextTrimming.CharacterEllipsis;
                var sp = new StackPanel { Orientation = Orientation.Horizontal };
                dot.VerticalAlignment = VerticalAlignment.Center;
                sp.Children.Add(dot);
                sp.Children.Add(text);
                Child = sp;
            }

            public void Set(string s, Color c)
            {
                text.Text = s.ToUpperInvariant();
                dot.Fill = new SolidColorBrush(c);
            }
        }

        private sealed class Segmented : Border
        {
            private readonly List<Border> items = new List<Border>();
            private int selected = -1;
            public Segmented Partner;

            public Segmented(string[] labels, Action<int> onPick)
            {
                Background = new SolidColorBrush(Panel);
                BorderBrush = new SolidColorBrush(Line);
                BorderThickness = new Thickness(1);
                CornerRadius = new CornerRadius(5);
                Padding = new Thickness(3);
                var g = new UniformGrid { Rows = 1 };
                for (var i = 0; i < labels.Length; i++)
                {
                    var idx = i;
                    var t = T(labels[i].ToUpperInvariant(), 12, Muted, FontWeights.SemiBold);
                    t.FontStretch = FontStretches.Condensed;
                    t.HorizontalAlignment = HorizontalAlignment.Center;
                    var b = new Border { Child = t, Padding = new Thickness(0, 7, 0, 7), CornerRadius = new CornerRadius(3), Background = Brushes.Transparent, Cursor = Cursors.Hand };
                    b.MouseLeftButtonUp += (s, e) =>
                    {
                        Select(idx);
                        Partner?.Clear();
                        onPick(idx);
                    };
                    items.Add(b);
                    g.Children.Add(b);
                }
                Child = g;
            }

            public void Select(int idx)
            {
                if (idx == selected || idx < 0 || idx >= items.Count) return;
                selected = idx;
                for (var i = 0; i < items.Count; i++)
                {
                    var on = i == idx;
                    items[i].Background = on ? new SolidColorBrush(Color.FromRgb(236, 236, 236)) : Brushes.Transparent;
                    ((TextBlock)items[i].Child).Foreground = new SolidColorBrush(on ? Color.FromRgb(9, 9, 9) : Muted);
                }
            }

            public void Clear()
            {
                selected = -1;
                foreach (var b in items)
                {
                    b.Background = Brushes.Transparent;
                    ((TextBlock)b.Child).Foreground = new SolidColorBrush(Muted);
                }
            }
        }

        private sealed class Chip : Border
        {
            private readonly TextBlock text;
            private bool on;
            public event Action Click;

            public Chip(string label)
            {
                text = T(label.ToUpperInvariant(), 11.5, Muted, FontWeights.SemiBold);
                text.FontStretch = FontStretches.Condensed;
                text.HorizontalAlignment = HorizontalAlignment.Center;
                text.TextTrimming = TextTrimming.CharacterEllipsis;
                Child = text;
                Padding = new Thickness(6, 8, 6, 8);
                CornerRadius = new CornerRadius(4);
                BorderThickness = new Thickness(1);
                Cursor = Cursors.Hand;
                MouseLeftButtonUp += (s, e) => Click?.Invoke();
                On = false;
            }

            public bool On
            {
                get { return on; }
                set
                {
                    on = value;
                    // outline, not a fill: a grid of solid white chips is too heavy
                    Background = new SolidColorBrush(on ? PanelHi : Panel);
                    BorderBrush = new SolidColorBrush(on ? Color.FromRgb(200, 200, 200) : Line);
                    text.Foreground = new SolidColorBrush(on ? Text : Dim);
                }
            }
        }
    }
}
