# Preload-NG
Discord webhook test
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/gpl-2.0)
[![C](https://img.shields.io/badge/Language-C-blue.svg)](<https://en.wikipedia.org/wiki/C_(programming_language)>)
[![Linux](https://img.shields.io/badge/Platform-Linux-green.svg)](https://www.kernel.org/)

An adaptive **readahead daemon** that prefetches files to reduce application startup times on Linux systems.

> **Note:** This is a maintained fork of the original [preload](http://preload.sf.net/) project, discontinued since 2009.

---

## About

Preload monitors which applications you use and learns your usage patterns through **Markov chains**. It predicts which applications you're likely to run next and preloads their binaries and shared libraries into memory.

**For detailed documentation, configuration, and troubleshooting, see [doc/README.md](doc/README.md).**

---

## Quick Install

### Using Precompiled Binary

```bash
git clone https://github.com/miguel-b-p/preload-ng.git
cd preload-ng/scripts
sudo bash install.sh
```

### Building from Source

```bash
git clone https://github.com/miguel-b-p/preload-ng.git
cd preload-ng/scripts
bash build.sh
```

### Using Nix Flakes

```bash
# Build the package
nix build github:miguel-b-p/preload-ng

# Run directly
nix run github:miguel-b-p/preload-ng

# Enter development shell
nix develop github:miguel-b-p/preload-ng
```

#### NixOS Configuration

Add to your `flake.nix`:

```nix
{
  inputs.preload-ng.url = "github:miguel-b-p/preload-ng";

  outputs = { self, nixpkgs, preload-ng, ... }: {
    nixosConfigurations.your-hostname = nixpkgs.lib.nixosSystem {
      modules = [
        preload-ng.nixosModules.default
        {
          services.preload-ng.enable = true;
        }
      ];
    };
  };
}
```

> All settings from `preload.conf` are available as declarative options via `services.preload-ng.settings`. A `debug` option is also available to enable verbose output. See [doc/README.md](doc/README.md#nixos-declarative-configuration) for the complete configuration reference.

---

## Origin & Credits

**Original Author:** [Behdad Esfahbod](http://behdad.org/) — Created in 2005 as part of [Google Summer of Code](https://summerofcode.withgoogle.com/), mentored by [Fedora Project](https://fedoraproject.org/).

**Contributors:** Ziga Mahkovec, Soeren Sandmann, Arjan van de Ven, bert hubert, Elliot Lee

---

## About This Fork

The original project was last updated in **April 2009** (v0.6.4). **Preload-NG** aims to:

- Maintain compatibility with modern Linux kernels
- Fix bugs and memory leaks
- Implement originally planned features

See [changelogs/0.6.6.md](changelogs/0.6.6.md) for the detailed changelog.

---

## Project Structure

```
preload-ng/
├── preload-src/  # Source code
├── doc/          # Documentation
├── changelogs/   # Version changelogs
└── scripts/      # Utility scripts (build.sh)
```

---

## License

**GNU General Public License v2** — See [LICENSE](LICENSE)

---

## Links

- **Original Project:** http://preload.sf.net/
- **Original Author:** http://behdad.org/
