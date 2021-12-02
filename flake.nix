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
            rsync
            openssh
          ];
          version = builtins.elemAt
            (builtins.match ''.*set\(.LSYNCD_VERSION ([0-9\.]*).*''
              (builtins.substring 0 500
                (builtins.readFile ./CMakeLists.txt))) 0;
          buildTypes = { 
            lua5_1 = [pkgs.lua5_1 pkgs.lua51Packages.luaposix pkgs.lua51Packages.penlight];
            lua5_2 = [pkgs.lua5_2 pkgs.lua52Packages.luaposix pkgs.lua52Packages.penlight];
            lua5_3 = [pkgs.lua5_3 pkgs.lua53Packages.luaposix pkgs.lua53Packages.penlight];
            lua5_4 = [pkgs.lua5_4 pkgs.lua54Packages.luaposix pkgs.lua54Packages.penlight];
          };
        in
        let
           mkLsync = luaPackages: pkgs.stdenv.mkDerivation ({
            inherit version;
            name = "lsyncd";

            src = ./.;
            
            # nativeBuildInputs = [ pkgs.qt5.wrapQtAppsHook ]; 
            buildInputs = defaultDeps ++ luaPackages;
        });
        in
        {
          packages = {
              lsyncd = mkLsync buildTypes.lua5_3;
              lsyncd_lua5_1 = mkLsync buildTypes.lua5_1;
              lsyncd_lua5_2 = mkLsync buildTypes.lua5_2;
              lsyncd_lua5_3 = mkLsync buildTypes.lua5_3;
              lsyncd_lua5_4 = mkLsync buildTypes.lua5_4;
          };

          defaultPackage = self.packages.${system}.lsyncd;
          devShell = pkgs.mkShell {
            buildInputs = defaultDeps ++ buildTypes.lua5_3;
          };
        }
      );
}