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

          src = ./bin;

          # No build required - using precompiled binary
          dontBuild = true;
          dontConfigure = true;

          nativeBuildInputs = with pkgs; [ autoPatchelfHook ];
          buildInputs = with pkgs; [ glib ];

          installPhase = ''
            runHook preInstall

            # Install binary
            install -Dm755 preload $out/bin/preload

            # Install config
            install -Dm644 preload.conf $out/etc/conf.d/preload.conf

            runHook postInstall
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
          cfg = config.services.preload-ng;
          preload-pkg = self.packages.${pkgs.system}.preload-ng;
        in
        {
          options.services.preload-ng = {
            enable = lib.mkEnableOption "preload-ng daemon";

            package = lib.mkOption {
              type = lib.types.package;
              default = preload-pkg;
              description = "The preload package to use.";
            };
          };

          config = lib.mkIf cfg.enable {
            systemd.services.preload-ng = {
              description = "Preload-NG Daemon";
              wantedBy = [ "multi-user.target" ];
              after = [ "local-fs.target" ];

              serviceConfig = {
                Type = "simple";
                ExecStart = "${cfg.package}/bin/preload --foreground --conffile ${cfg.package}/etc/conf.d/preload.conf --statefile /var/lib/preload/preload.state --logfile ''";
                Restart = "on-failure";

                # Hardening
                ProtectSystem = "strict";
                ProtectHome = true;
                PrivateTmp = true;
                NoNewPrivileges = true;

                # Automatically create /var/lib/preload and /var/log/preload
                StateDirectory = "preload";
                #LogsDirectory = "preload"; # preload-ng might write directly to /var/log/preload.log, so we let it access /var/log for now or check config

                ReadWritePaths = [
                  "/var/log"
                ];
              };

              preStart = ''
                # /var/lib/preload is created by StateDirectory
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
