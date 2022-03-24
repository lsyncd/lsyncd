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

          buildExtensions = luapkgs: (
            let
              nucleo = luapkgs.buildLuarocksPackage {
                pname = "lua-nucleo";
                version = "1.1.0-1";
                knownRockspec = (pkgs.fetchurl {
                  url    = "https://luarocks.org/lua-nucleo-1.1.0-1.rockspec";
                  sha256 = "02ly51wav1pxiahf6lflr4vks550bisdq4ir9cy1lxn9v2zmcbim";
                }).outPath;
                src = pkgs.fetchgit ( removeAttrs (builtins.fromJSON ''{
                "url": "https://github.com/lua-nucleo/lua-nucleo.git",
                "rev": "76835968ff30f182367abd58637560402990e0b1",
                "date": "2021-04-26T11:51:34+03:00",
                "path": "/nix/store/3ycmrh0j64qxm4f04yxmn3y42imc8bv5-lua-nucleo",
                "sha256": "15kydmj64jhxv5ksayfgkwzmgzd7raj7xp636x8a7c3ybiirs90n",
                "fetchSubmodules": true,
                "deepClone": false,
                "leaveDotGit": false
              }
              '') ["date" "path"]) ;

                disabled = with luapkgs; (luaOlder "5.1");

                meta = {
                  homepage = "http://github.com/lua-nucleo/lua-nucleo";
                  description = "A random collection of core and utility level Lua libraries";
                  license.fullName = "MIT/X11";
                };
              };
            in
              luapkgs.buildLuarocksPackage {
                pname = "lua-crontab";
                version = "1.0.0-1";
                knownRockspec = (pkgs.fetchurl {
                  url    = "https://luarocks.org/lua-crontab-1.0.0-1.rockspec";
                  sha256 = "1aynwxq488sxd2lyng4wnswfkqna5n07sfmdainlqlhcb6jan161";
                }).outPath;
                src = pkgs.fetchgit ( removeAttrs (builtins.fromJSON ''{
                "url": "https://github.com/logiceditor-com/lua-crontab.git",
                "rev": "e3929a572e8164f968da4dcbdf1c4464a2870699",
                "date": "2021-07-29T14:12:08+03:00",
                "path": "/nix/store/rsc49m4f1mjqbffaq7axcf31rgxxfjb3-lua-crontab",
                "sha256": "0zkqslw3vg495k8g010cz931vlzfyynq4kcwi1jbbppia521z6rx",
                "fetchSubmodules": true,
                "deepClone": false,
                "leaveDotGit": false
              }
              '') ["date" "path"]) ;

                propagatedBuildInputs = [ nucleo ];

                meta = {
                  homepage = "http://github.com/logiceditor-com/lua-crontab";
                  description = "Stores crontab-like rules for events and calculates timestamps for their occurrences";
                  license.fullName = "MIT/X11";
                };
              }
          );

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
            lua5_1 = [pkgs.lua5_1 pkgs.lua51Packages.luaposix (buildExtensions pkgs.lua51Packages)];
            lua5_2 = [pkgs.lua5_2 pkgs.lua52Packages.luaposix (buildExtensions pkgs.lua52Packages)];
            lua5_3 = [pkgs.lua5_3 pkgs.lua53Packages.luaposix (buildExtensions pkgs.lua53Packages)];
            lua5_4 = [pkgs.lua5_4 (luaposix35 mylua5_4) (buildExtensions mylua5_4.pkgs)];
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