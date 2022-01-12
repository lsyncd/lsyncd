{
  description = "Lsyncd (Live Syncing Daemon)";
  
  inputs.nixpkgs.url = "github:nixos/nixpkgs/release-21.11";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = (import nixpkgs {
            inherit system;
            # Makes the config pure as well. See <nixpkgs>/top-level/impure.nix:
            config = {
              allowBroken = true;
            };}); #.legacyPackages.${system};
          defaultDeps = with pkgs; [
            gcc
            cmake
            gnumake
            glib
            rsync
            openssh
            curl
          ];
          version = builtins.elemAt
            (builtins.match ''.*set\(.LSYNCD_VERSION ([0-9\.]*).*''
              (builtins.substring 0 500
                (builtins.readFile ./CMakeLists.txt))) 0;
          mylua5_4 = pkgs.lua5_4.override({
            packageOverrides =  luaself: luaprev: {
              luarocks = luaprev.luarocks-3_7;
            };
          });
          luaposix35 = mylua: mylua.pkgs.buildLuarocksPackage {
            pname = "luaposix";
            lua = mylua;
            version = "35.1-1";
            knownRockspec = (pkgs.fetchurl {
              url    = "https://luarocks.org/luaposix-35.1-1.rockspec";
              sha256 = "1n6c7qyabj2y95jmbhf8fxbrp9i73kphmwalsam07f9w9h995xh1";
            }).outPath;
            src = pkgs.fetchurl {
              url    = "http://github.com/luaposix/luaposix/archive/v35.1.zip";
              sha256 = "1c03chkzwr2p1wd0hs1bafl2890fqbrfc3qk0wxbd202gc6128zi";
            };

            #
            propagatedBuildInputs = [ mylua ];

            meta = {
              homepage = "http://github.com/luaposix/luaposix/";
              description = "Lua bindings for POSIX";
              license.fullName = "MIT/X11";
            };
          };

          buildTypes = { 
            lua5_1 = [pkgs.lua5_1 pkgs.lua51Packages.luaposix];
            lua5_2 = [pkgs.lua5_2 pkgs.lua52Packages.luaposix];
            lua5_3 = [pkgs.lua5_3 pkgs.lua53Packages.luaposix];
            lua5_4 = [pkgs.lua5_3 (luaposix35 mylua5_4)];
          };
        in
        let
           mkLsync = luaPackages: pkgs.stdenv.mkDerivation ({
            inherit version;
            name = "lsyncd";

            src = ./.;

            buildInputs = defaultDeps ++ luaPackages;
          });
          mkDev = packages: pkgs.mkShell {
            propagatedBuildInputs = defaultDeps ++ packages;
          };
        in
        {
          packages = {
              lsyncd = mkLsync buildTypes.lua5_3;
              lsyncd_lua5_1 = mkLsync buildTypes.lua5_1;
              lsyncd_lua5_2 = mkLsync buildTypes.lua5_2;
              lsyncd_lua5_3 = mkLsync buildTypes.lua5_3;
              lsyncd_lua5_4 = mkLsync buildTypes.lua5_4;
          };

          devShells = {
              lsyncd = mkDev buildTypes.lua5_3;
              lsyncd_lua5_1 = mkDev buildTypes.lua5_1;
              lsyncd_lua5_2 = mkDev buildTypes.lua5_2;
              lsyncd_lua5_3 = mkDev buildTypes.lua5_3;
              lsyncd_lua5_4 = mkDev buildTypes.lua5_4;
          };

          defaultPackage = self.packages.${system}.lsyncd;
        }
      );
}