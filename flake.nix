{
  description = "Lsyncd (Live Syncing Daemon)";
  
  inputs.nixpkgs.url = "github:nixos/nixpkgs/release-21.05";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          defaultDeps = with pkgs; [
            gcc
            cmake
            glib
          ];
          version = builtins.elemAt
            (builtins.match ''.*set\(.LSYNCD_VERSION ([0-9\.]*).*''
              (builtins.substring 0 500
                (builtins.readFile ./CMakeLists.txt))) 0;
            #   buildTypes = { 
            #     lua5_2 = pkgs.lua5_2;
            #     lua5_3 = pkgs.lua5_3;
            #   };
        in
        let
           mkLsync = luaPackage: pkgs.stdenv.mkDerivation ({
            inherit version;
            name = "lsyncd";

            src = ./.;
            
            # nativeBuildInputs = [ pkgs.qt5.wrapQtAppsHook ]; 
            buildInputs = defaultDeps ++ [luaPackage];
        });
        in
        {
          packages = {
              lsyncd = mkLsync pkgs.lua5_3;
              lsyncd_lua5_1 = mkLsync pkgs.lua5_1;
              lsyncd_lua5_2 = mkLsync pkgs.lua5_2;
              lsyncd_lua5_3 = mkLsync pkgs.lua5_3;
              lsyncd_lua5_4 = mkLsync pkgs.lua5_4;
          };

          defaultPackage = self.packages.${system}.lsyncd;
          devShell = pkgs.mkShell {
            buildInputs = defaultDeps;
          };
        }
      );
}