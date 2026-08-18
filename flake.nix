{
  description = "Ys: The Oath in Felghana map mesh and texture extraction/rendering pipeline";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };
        pythonEnv = pkgs.python3.withPackages (ps: with ps; [
          numpy
          pillow
          scipy
          pygltflib
          tqdm
          pytest
        ]);
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            pythonEnv
            blender
            unzip
            p7zip
            file
            hexdump
            gcc
            gnumake
            cmake
            pkg-config
            git
          ];

          shellHook = ''
            export PYTHONPATH="$PWD:$PYTHONPATH"
          '';
        };
      });
}
