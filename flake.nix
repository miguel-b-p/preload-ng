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

        preload-ng-bin = pkgs.stdenv.mkDerivation rec {
          pname = "preload-ng";
          version = "0.6.7";

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

        preload-ng-src = pkgs.stdenv.mkDerivation rec {
          pname = "preload-ng";
          version = "0.6.7";

          src = ./preload-src;

          nativeBuildInputs = with pkgs; [
            autoreconfHook
            pkg-config
          ];
          buildInputs = with pkgs; [ glib ];

          meta = with pkgs.lib; {
            description = "Adaptive readahead daemon for Linux";
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
          default = preload-ng-bin;
          inherit preload-ng-bin preload-ng-src;
        };

        # Development shell with build dependencies
        devShells.default = pkgs.mkShell {
          inputsFrom = [ preload-ng-src ];
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
          preload-pkg =
            if cfg.usePrecompiled then
              self.packages.${pkgs.system}.preload-ng-bin
            else
              self.packages.${pkgs.system}.preload-ng-src;

          # Generate preload.conf content from options
          configContent = ''
            [model]

            # Quantum of time for preload (seconds)
            cycle = ${toString cfg.settings.cycle}

            # Use correlation coefficient in prediction algorithm
            usecorrelation = ${lib.boolToString cfg.settings.useCorrelation}

            # Minimum sum of the length of maps to track (bytes)
            minsize = ${toString cfg.settings.minSize}

            # Memory usage percentages for prefetching
            memtotal = ${toString cfg.settings.memTotal}
            memfree = ${toString cfg.settings.memFree}
            memcached = ${toString cfg.settings.memCached}
            membuffers = ${toString cfg.settings.memBuffers}

            [system]

            # Monitor running processes
            doscan = ${lib.boolToString cfg.settings.doScan}

            # Make predictions and prefetch
            dopredict = ${lib.boolToString cfg.settings.doPredict}

            # Auto-save period (seconds)
            autosave = ${toString cfg.settings.autoSave}

            # Path prefixes for mapped files
            mapprefix = ${cfg.settings.mapPrefix}

            # Path prefixes for executables
            exeprefix = ${cfg.settings.exePrefix}

            # Prediction algorithm
            prediction_algorithm = ${cfg.settings.predictionAlgorithm}

            # Number of parallel readahead processes
            processes = ${toString cfg.settings.processes}

            # I/O sorting strategy (0=none, 1=path, 2=inode, 3=block)
            sortstrategy = ${toString cfg.settings.sortStrategy}
          '';

          # Create a wrapped package with the custom config in the same store path
          configuredPackage = pkgs.symlinkJoin {
            name = "preload-ng-configured-${cfg.package.version}";
            paths = [ cfg.package ];
            postBuild = ''
                # Remove the default config symlink and replace with custom one
                rm -rf $out/etc
                mkdir -p $out/etc/conf.d
                cat > $out/etc/conf.d/preload.conf << 'EOF'
              ${configContent}
              EOF
            '';
          };
        in
        {
          options.services.preload-ng = {
            enable = lib.mkEnableOption "preload-ng daemon";

            usePrecompiled = lib.mkOption {
              type = lib.types.bool;
              default = true;
              description = "Whether to use the precompiled binary (true) or compile from source (false).";
            };

            debug = lib.mkOption {
              type = lib.types.bool;
              default = false;
              description = "Enable debug mode. Outputs every debug messages.";
            };

            package = lib.mkOption {
              type = lib.types.package;
              default = preload-pkg;
              description = "The preload package to use.";
            };

            settings = {
              cycle = lib.mkOption {
                type = lib.types.int;
                default = 20;
                description = "Quantum of time for preload in seconds. Use an even number.";
              };

              useCorrelation = lib.mkOption {
                type = lib.types.bool;
                default = true;
                description = "Whether to use correlation coefficient in prediction algorithm.";
              };

              minSize = lib.mkOption {
                type = lib.types.int;
                default = 2000000;
                description = "Minimum sum of the length of maps (in bytes) for preload to track.";
              };

              memTotal = lib.mkOption {
                type = lib.types.int;
                default = -10;
                description = "Percentage of total memory to use for prefetching (-100 to 100).";
              };

              memFree = lib.mkOption {
                type = lib.types.int;
                default = 50;
                description = "Percentage of free memory to use for prefetching (-100 to 100).";
              };

              memCached = lib.mkOption {
                type = lib.types.int;
                default = 0;
                description = "Percentage of cached memory to use for prefetching (-100 to 100).";
              };

              memBuffers = lib.mkOption {
                type = lib.types.int;
                default = 50;
                description = "Percentage of buffer memory to use for prefetching (-100 to 100).";
              };

              doScan = lib.mkOption {
                type = lib.types.bool;
                default = true;
                description = "Whether preload should monitor running processes.";
              };

              doPredict = lib.mkOption {
                type = lib.types.bool;
                default = true;
                description = "Whether preload should make predictions and prefetch.";
              };

              autoSave = lib.mkOption {
                type = lib.types.int;
                default = 3600;
                description = "Auto-save period in seconds.";
              };

              mapPrefix = lib.mkOption {
                type = lib.types.str;
                default = "/nix/store/;/run/current-system/;!/";
                description = ''
                  Path prefixes for mapped files. Items separated by semicolons.
                  Prefix with ! to reject. Example: !/lib/modules;/
                '';
              };

              exePrefix = lib.mkOption {
                type = lib.types.str;
                default = "/nix/store/;/run/current-system/;!/";
                description = ''
                  Path prefixes for executables. Same syntax as mapPrefix.
                '';
              };

              predictionAlgorithm = lib.mkOption {
                type = lib.types.enum [
                  "Markov"
                  "VOMM"
                ];
                default = "VOMM";
                description = ''
                  The prediction algorithm to use.
                  "Markov" = Classic Markov chain prediction.
                  "VOMM" = Variable Order Markov Model (experimental).
                '';
              };

              processes = lib.mkOption {
                type = lib.types.int;
                default = 30;
                description = "Maximum number of parallel readahead processes (0 = no parallel).";
              };

              sortStrategy = lib.mkOption {
                type = lib.types.enum [
                  0
                  1
                  2
                  3
                ];
                default = 3;
                description = ''
                  I/O sorting strategy:
                  0 = SORT_NONE (useful for Flash memory)
                  1 = SORT_PATH (useful for network filesystems)
                  2 = SORT_INODE (less house-keeping I/O)
                  3 = SORT_BLOCK (most sophisticated, best for most filesystems)
                '';
              };

            };
          };

          config = lib.mkIf cfg.enable {
            systemd.services.preload-ng = {
              description = "Preload-NG Daemon";
              wantedBy = [ "basic.target" ];
              after = [ "local-fs.target" ];
              before = [ "systemd-user-sessions.service" ];

              serviceConfig = {
                Type = "simple";
                ExecStart = "${configuredPackage}/bin/preload --foreground --conffile ${configuredPackage}/etc/conf.d/preload.conf --statefile /var/lib/preload/preload.state ${
                  if cfg.debug then "-d" else "--logfile ''"
                }";
                Restart = "on-failure";

                # Hardening
                ProtectSystem = "strict";
                ProtectHome = true;
                PrivateTmp = true;
                NoNewPrivileges = true;

                # Automatically create /var/lib/preload
                StateDirectory = "preload";

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
        preload-ng = self.packages.${prev.system}.default;
        preload-ng-bin = self.packages.${prev.system}.preload-ng-bin;
        preload-ng-src = self.packages.${prev.system}.preload-ng-src;
      };
    };
}
