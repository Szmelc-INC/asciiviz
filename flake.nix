{
  description = "ASCII function visualizer in C with palettes and function packs";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: rec {
        asciiviz = pkgs.stdenv.mkDerivation {
          pname = "asciiviz";
          version = "3-unstable-${self.shortRev or self.dirtyShortRev or "dirty"}";
          src = ./.;
          nativeBuildInputs = [ pkgs.gcc ];
          buildPhase = ''
            runHook preBuild
            make
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            install -Dm755 asciiviz $out/bin/asciiviz
            install -Dm644 README.md $out/share/doc/asciiviz/README.md
            install -Dm644 functions/*.cfg -t $out/share/asciiviz/functions
            install -Dm644 palettes/char/*.cfg -t $out/share/asciiviz/palettes/char
            install -Dm644 palettes/col/*.cfg -t $out/share/asciiviz/palettes/col
            install -Dm644 LICENSE $out/share/licenses/asciiviz/LICENSE
            runHook postInstall
          '';
          meta = {
            description = "ASCII function visualizer";
            homepage = "https://github.com/Szmelc-INC/asciiviz";
            license = pkgs.lib.licenses.mit;
            mainProgram = "asciiviz";
            platforms = pkgs.lib.platforms.linux;
          };
        };
        default = asciiviz;
      });

      apps = forAllSystems (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.default}/bin/asciiviz";
        };
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell { packages = with pkgs; [ gcc ]; };
      });
    };
}
