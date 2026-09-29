using System.ComponentModel.Composition;
using Prism.Mef.Modularity;
using Prism.Modularity;
using PrismHud.WinTAK.Services;
using PrismHud.WinTAK.Views;
using WinTak.Common.Services;
using WinTak.CursorOnTarget.Services;
using WinTak.Framework.Docking;
using WinTak.Framework.Docking.Attributes;
using WinTak.Framework.Tools;
using WinTak.Framework.Tools.Attributes;
using WinTak.Graphics.Map;

namespace PrismHud.WinTAK
{
    [ModuleExport(typeof(PrismModule), InitializationMode = InitializationMode.WhenAvailable)]
    internal sealed class PrismModule : IModule
    {
        [Import(AllowDefault = true)] public ILocationService LocationService { get; set; }
        [Import(AllowDefault = true)] public IMapObjectRenderer MapObjectRenderer { get; set; }
        [Import(AllowDefault = true)] public IMapGroupManager MapGroupManager { get; set; }

        public void Initialize()
        {
            PrismRuntime.Start(LocationService, MapObjectRenderer, MapGroupManager);
        }
    }

    [Button("PrismHud_WinTAK_Button", "PRISM",
        LargeImage = "pack://application:,,,/PrismHud.WinTAK;component/Assets/prism.svg",
        SmallImage = "pack://application:,,,/PrismHud.WinTAK;component/Assets/prism.svg")]
    internal sealed class PrismButton : Button
    {
        private readonly IDockingManager dockingManager;

        [ImportingConstructor]
        public PrismButton(IDockingManager dockingManager)
        {
            this.dockingManager = dockingManager;
        }

        protected override void OnClick()
        {
            base.OnClick();
            dockingManager.GetDockPane(PrismDockPane.Id)?.Activate();
        }
    }

    [DockPane(Id, "PRISM", Content = typeof(PrismView), PreferredWidth = 430)]
    internal sealed class PrismDockPane : DockPane
    {
        internal const string Id = "PrismHud_WinTAK_DockPane";

        [ImportingConstructor]
        public PrismDockPane()
        {
        }
    }
}
