{
  description = "BestClient DDNet";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation rec {
        pname = "bestclient";
        version = "2.3";

        src = pkgs.fetchurl {
          url = "https://github.com/BestProjectTeam/BestClient/releases/download/v${version}/BestClient-linux.tar.xz";
          hash = "sha256-ngp73/RXIY1nCky3hhrTN+gRlBJVyw9TJJAMNTjpy6o=";
        };

        nativeBuildInputs = [ 
          pkgs.autoPatchelfHook
          pkgs.makeWrapper
        ];

        buildInputs = [
          pkgs.stdenv.cc.cc.lib
          pkgs.SDL2
          pkgs.freetype
          pkgs.libGL
          pkgs.curl
          pkgs.openssl
	        pkgs.vulkan-loader
	        pkgs.libnotify
        ];

        # Keep URL and sourceRoot derived from `version` so future bumps
        # only need to touch `version` (plus the src hash, which Nix requires
        # to be pinned and can never be derived).
        sourceRoot = "bestclient-${version}-linux_x86_64";

        installPhase = ''
        mkdir -p $out/bin $out/share/applications
        cp -r . $out/
        chmod +x $out/DDNet

        cat > $out/share/applications/bestclient.desktop <<EOF
[Desktop Entry]
Name=BestClient
Comment=DDNet client with extra features
Exec=$out/bin/bestclient
Icon=$out/data/BestClient/bc_icon.png
Type=Application
Categories=Game;
EOF

        # wrap the binary so it runs from the right directory
        makeWrapper $out/DDNet $out/bin/bestclient \
          --run "cd $out"
        '';
      };

      apps.${system}.default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/bestclient";
      };
    };
}
