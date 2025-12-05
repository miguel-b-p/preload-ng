{
  description = "Preload-NG - Adaptive readahead daemon for Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        preload-ng = pkgs.stdenv.mkDerivation rec {
          pname = "preload-ng";
          version = "0.6.6";

          src = ./preload-src;

          # Remove installation of files to /var directories (not allowed in Nix sandbox)
          # and run bootstrap to generate configure script
          postPatch = ''
            patchShebangs bootstrap

            # Remove log_DATA and pkglocalstate_DATA from Makefile.am
            # These try to install to /var which is not allowed in Nix
            sed -i '/^log_DATA/d' Makefile.am
            sed -i '/^pkglocalstate_DATA/d' Makefile.am

            ./bootstrap
          '';

          nativeBuildInputs = with pkgs; [
            autoconf
            automake
            pkg-config
          ];

          buildInputs = with pkgs; [ glib ];

          configureFlags = [ "--localstatedir=/var" ];

          postInstall = ''
            make sysconfigdir=$out/etc/conf.d install
          '';

          meta = with pkgs.lib; {
            description = "Makes applications run faster by prefetching binaries and shared objects";
            homepage = "https://github.com/miguel-b-p/preload-ng";
            license = licenses.gpl2Only;
            platforms = platforms.linux;
            mainProgram = "preload";
            maintainers = [ ];
          };
        };
      in
      {
        packages = {
          default = preload-ng;
          inherit preload-ng;
        };

        # Development shell with build dependencies
        devShells.default = pkgs.mkShell {
          inputsFrom = [ preload-ng ];
          packages = with pkgs; [
            gdb
            valgrind
          ];
        };
      }
    )
    // {
      # NixOS module
      nixosModules.default =
        {
          config,
          lib,
          pkgs,
          ...
        }:
        let
          cfg = config.services.preload;
          preload-pkg = self.packages.${pkgs.system}.preload-ng;
        in
        {
          options.services.preload = {
            enable = lib.mkEnableOption "preload daemon";

            package = lib.mkOption {
              type = lib.types.package;
              default = preload-pkg;
              description = "The preload package to use.";
            };
          };

          config = lib.mkIf cfg.enable {
            systemd.services.preload = {
              description = "Preload Daemon";
              wantedBy = [ "multi-user.target" ];
              after = [ "local-fs.target" ];

              serviceConfig = {
                Type = "simple";
                ExecStart = "${cfg.package}/bin/preload --foreground";
                Restart = "on-failure";

                # Hardening
                ProtectSystem = "strict";
                ProtectHome = true;
                PrivateTmp = true;
                NoNewPrivileges = true;
                ReadWritePaths = [
                  "/var/lib/preload"
                  "/var/log"
                ];
              };

              preStart = ''
                mkdir -p /var/lib/preload
                mkdir -p /var/log
              '';
            };

            environment.systemPackages = [ cfg.package ];
          };
        };

      # Overlay for use in other flakes
      overlays.default = final: prev: {
        preload-ng = self.packages.${prev.system}.preload-ng;
      };
    };
}
