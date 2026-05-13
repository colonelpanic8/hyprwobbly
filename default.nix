{
  lib,
  hyprland,
  hyprlandPlugins,
}:
hyprlandPlugins.mkHyprlandPlugin {
  pluginName = "hyprwobbly";
  version = "0.1";
  src = ./.;

  inherit (hyprland) nativeBuildInputs;

  meta = with lib; {
    homepage = "https://github.com/colonelpanic8/hyprwobbly";
    description = "Compiz-style wobbly windows for Hyprland";
    license = licenses.bsd3;
    platforms = platforms.linux;
  };
}
