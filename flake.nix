{
  description = "hyprwobbly, a Compiz-style wobbly windows plugin for Hyprland";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
    nixpkgs.follows = "hyprland/nixpkgs";
    systems.follows = "hyprland/systems";
  };

  outputs = {
    self,
    hyprland,
    nixpkgs,
    systems,
    ...
  }: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);

    pkgsFor = eachSystem (system:
      import nixpkgs {
        localSystem.system = system;
        overlays = [
          hyprland.overlays.hyprland-packages
          self.overlays.default
        ];
      });

    sourceFiles = [
      "main.cpp"
      "Wobbly.cpp"
      "Wobbly.hpp"
      "globals.hpp"
      "shaders.hpp"
    ];

    sourceFileArgs = lib.concatMapStringsSep " " lib.escapeShellArg sourceFiles;
    hyprpmManifest = builtins.fromTOML (builtins.readFile ./hyprpm.toml);
    hyprpmBuildCommands =
      lib.concatMapStringsSep "\n" (command: ''
        cd "$repoRoot"
        ${command}
      '')
      hyprpmManifest.hyprwobbly.build;
  in {
    overlays = {
      default = final: prev: {
        hyprlandPlugins =
          (prev.hyprlandPlugins or {})
          // {
            hyprwobbly = final.callPackage ./default.nix {};
          };

        inherit (final.hyprlandPlugins) hyprwobbly;
      };
    };

    packages = eachSystem (system: {
      default = self.packages.${system}.hyprwobbly;
      inherit (pkgsFor.${system}.hyprlandPlugins) hyprwobbly;
    });

    checks = eachSystem (system: let
      pkgs = pkgsFor.${system};
      hyprlandPkg = hyprland.packages.${system}.hyprland;
      src = lib.cleanSource ./.;
    in {
      inherit (self.packages.${system}) hyprwobbly;

      hyprpm-build = pkgs.gcc14Stdenv.mkDerivation {
        pname = "hyprwobbly-hyprpm-build-check";
        version = "0.1";
        inherit src;

        inherit (hyprlandPkg) nativeBuildInputs;
        buildInputs = [hyprlandPkg] ++ hyprlandPkg.buildInputs;

        dontConfigure = true;

        buildPhase = ''
          runHook preBuild
          repoRoot="$PWD"
          test ${lib.escapeShellArg hyprpmManifest.repository.name} = hyprwobbly
          test ${lib.escapeShellArg hyprpmManifest.hyprwobbly.output} = hyprwobbly.so
          ${hyprpmBuildCommands}
          test -f ${lib.escapeShellArg hyprpmManifest.hyprwobbly.output}
          runHook postBuild
        '';

        installPhase = ''
          runHook preInstall
          mkdir -p "$out"
          cp ${lib.escapeShellArg hyprpmManifest.hyprwobbly.output} "$out/"
          runHook postInstall
        '';
      };

      format =
        pkgs.runCommand "hyprwobbly-format-check" {
          inherit src;
          nativeBuildInputs = with pkgs; [
            alejandra
            clang-tools
            deadnix
            statix
          ];
        } ''
          cd "$src"
          clang-format --dry-run --Werror ${sourceFileArgs}
          alejandra --check *.nix
          deadnix --fail .
          statix check .
          touch "$out"
        '';
    });

    devShells = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in {
      default = pkgs.mkShell.override {stdenv = pkgs.gcc14Stdenv;} {
        name = "hyprwobbly";
        inputsFrom = [
          self.packages.${system}.hyprwobbly
          hyprland.packages.${system}.hyprland
        ];
        packages = with pkgs; [
          alejandra
          clang-tools
          cmake
          deadnix
          pkg-config
          statix
        ];
      };
    });

    formatter = eachSystem (system: pkgsFor.${system}.alejandra);
  };
}
