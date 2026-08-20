{
  description = "Ys Map & Mesh Viewer — Native 3D viewer for Ys: The Oath in Felghana, Ys Origin, and Ys VI";

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
        mingw = pkgs.pkgsCross.mingwW64.buildPackages;
        mingwPkgs = pkgs.pkgsCross.mingwW64;

        imguiSrc = pkgs.fetchFromGitHub {
          owner = "ocornut";
          repo = "imgui";
          rev = "v1.92.4";
          hash = "sha256-DyQ2fh749S41UFdLto7TtxsnBsd7CBzAUFq36LeZZ5Y=";
        };

        luaSrc = pkgs.runCommand "lua-5.4-src" { } ''
          mkdir -p $out
          tar xzf ${pkgs.lua5_4.src} --strip-components=1 -C $out
        '';

        pythonEnv = pkgs.python3.withPackages (ps: with ps; [
          numpy
          pillow
          scipy
          pygltflib
          tqdm
          pytest
        ]);
      in {
        devShells.default = pkgs.mkShell {
          name = "ys-viewer-dev";

          packages = with pkgs; [
            mingw.gcc
            mingw.binutils
            gnumake
            pkg-config
            pythonEnv
            lua5_4
            git
            blender
            unzip
            p7zip
            xvfb-run
          ];

          buildInputs = with pkgs; [
            raylib
            zlib
            libGL
            mesa
            libx11
            libxrandr
            libxinerama
            libxcursor
            libxi
            inter
            ipafont
          ];
          shellHook = ''
            export YS_ROOT=$PWD
            export IMGUI_DIR=${imguiSrc}
            export LUA_SRC=${luaSrc}
            export RAYLIB_INC=${pkgs.raylib}/include
            export RAYLIB_LIB=${pkgs.raylib}/lib
            export FONT_LATIN=${pkgs.inter}/share/fonts/truetype/InterVariable.ttf
            export FONT_CJK=${pkgs.ipafont}/share/fonts/truetype/ipag.ttf

            export RAYLIB_CROSS_INC=${mingwPkgs.raylib}/include
            export RAYLIB_CROSS_LIB=${mingwPkgs.raylib}/lib
            export RAYLIB_CROSS_DLL=${mingwPkgs.raylib}/bin/libraylib.dll
            export GLFW_CROSS_DLL=${mingwPkgs.glfw}/bin/glfw3.dll
            export ZLIB_CROSS_INC=${mingwPkgs.zlib}/include
            export ZLIB_CROSS_LIB=${mingwPkgs.zlib}/lib
            export MCFG_LIBDIR=$(x86_64-w64-mingw32-g++ -### -x c++ /dev/null -o /dev/null 2>&1 \
              | tr ' ' '\n' | grep -m1 -oE '^-L/nix/store/[^ ]*mcfgthread[^ ]*/lib' | cut -c3-)
            export MCFG_DLL=$(dirname "$MCFG_LIBDIR")/bin/libmcfgthread-2.dll

            export MINGW_CC=x86_64-w64-mingw32-gcc
            export MINGW_CXX=x86_64-w64-mingw32-g++
            export PYTHONPATH="$PWD:$PYTHONPATH"

            echo "Ys Map & Mesh Viewer dev shell ready"
          '';
        };

        formatter = pkgs.nixfmt-rfc-style;
      });
}
