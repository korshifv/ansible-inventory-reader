# Ansible Inventory Studio

A native Qt 6 / C++ / QML editor and graph viewer for static Ansible YAML inventories.

Open an existing inventory, inspect hosts and groups as a graph, edit memberships and variables without hand-editing the entire file, run Ansible ping checks, and save the result back as valid YAML.

## Features

- Open `.yml` / `.yaml` static inventories.
- Parse `all`, `hosts`, `vars`, and nested `children`.
- Preserve hosts that belong to several groups.
- Preserve groups that have several parents.
- Interactive graph with pan, zoom, fit-to-view, search/highlight, and clickable nodes.
- Expandable inventory tree.
- Create, rename, and delete hosts and groups.
- Add/remove host membership in groups.
- Add/remove parent relationships between groups with cycle prevention.
- Edit host/group variables as YAML mappings.
- Preserve editable leading YAML comments on host variable blocks.
- Run `ansible ... -m ping` checks for all hosts or a selected host, with reachable/unreachable/failed states and raw output.
- Save through `QSaveFile`, so replacement of the destination inventory is atomic.
- Canonical YAML output with `all` first, followed by group definitions and relationships.

## Download prebuilt packages

Prebuilt x86_64 packages are attached to each GitHub Release:

https://github.com/korshifv/ansible-inventory-reader/releases/latest

### Ubuntu 24.04+

Download the asset ending in `ubuntu24.04-amd64.deb`, then install it with APT:

```bash
sudo apt install ./ansible-inventory-studio-*-ubuntu24.04-amd64.deb
```

APT will resolve the Qt 6 and yaml-cpp runtime dependencies declared by the package.

### Debian 12+

Download the asset ending in `debian12-amd64.deb`, then:

```bash
sudo apt install ./ansible-inventory-studio-*-debian12-amd64.deb
```

The Debian package is built on Debian 12 specifically instead of reusing the Ubuntu package, so distro-specific Qt package dependencies stay correct.

### Fedora / RHEL-family

Download the `.rpm` asset and install it with DNF:

```bash
sudo dnf install ./ansible-inventory-studio-*-fedora-x86_64.rpm
```

The release RPM is built and smoke-tested on Fedora. It requires Qt 6.4 or newer. On RHEL-compatible systems whose enabled repositories only provide an older Qt 6, use a repository that provides Qt >= 6.4 or build from source using the instructions below.

### Arch Linux

There is no pacman/AUR package yet. The generic tarball contains the installed application tree and uses system Qt/yaml-cpp libraries.

Install runtime dependencies:

```bash
sudo pacman -S --needed qt6-base qt6-declarative yaml-cpp
```

Then extract the `linux-x86_64.tar.gz` release asset and run:

```bash
tar -xzf ansible-inventory-studio-*-linux-x86_64.tar.gz
./ansible-inventory-studio-*/bin/ansible-inventory-studio
```

You can also copy the binary into `/usr/local/bin` if you want it system-wide.

## Optional Ansible integration

The editor itself does not require Ansible to open or edit inventories. The **Ping** actions do require `ansible`/`ansible-core` to be available in `PATH`.

Examples:

```bash
# Ubuntu / Debian
sudo apt install ansible-core

# Fedora / RHEL-family
sudo dnf install ansible-core

# Arch Linux
sudo pacman -S ansible-core
```

## Build from source

The project requires:

- CMake >= 3.24
- a C++20 compiler
- Qt >= 6.4 with Quick / Quick Controls 2
- yaml-cpp

### Ubuntu / Debian build dependencies

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick \
  qml6-module-qtquick-controls \
  qml6-module-qtquick-dialogs \
  qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates \
  qml6-module-qtquick-window \
  qml6-module-qtqml-workerscript \
  libyaml-cpp-dev
```

### Fedora / RHEL-family build dependencies

```bash
sudo dnf install -y \
  gcc-c++ cmake ninja-build \
  qt6-qtbase-devel qt6-qtdeclarative-devel \
  yaml-cpp-devel
```

Qt >= 6.4 is required. If your RHEL-compatible release ships an older Qt, enable an appropriate newer Qt repository/toolchain first.

### Arch Linux build dependencies

```bash
sudo pacman -S --needed \
  base-devel cmake ninja \
  qt6-base qt6-declarative yaml-cpp
```

### Configure and build

The build commands are the same on all distributions:

```bash
git clone https://github.com/korshifv/ansible-inventory-reader.git
cd ansible-inventory-reader
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/ansible-inventory-studio
```

To install from a source build:

```bash
sudo cmake --install build --prefix /usr/local
```

You can pass an inventory path as the first argument:

```bash
./build/ansible-inventory-studio examples/inventory.yml
```

## Inventory model

An Ansible inventory is not treated as a filesystem tree. Hosts may belong to multiple groups and groups may have multiple parents, so the internal representation is a graph. The left-hand tree is only a convenient projection of that graph.

The writer emits a canonical YAML representation rather than preserving every formatting detail. Semantic inventory data is preserved. Leading comments attached to host variable blocks are preserved and editable; arbitrary inline/trailing comments and exact whitespace formatting are not guaranteed to round-trip.

## Scope

The application targets the static YAML inventory plugin. Dynamic inventory plugin configuration files are not edited as if they were static inventories.

## CI and releases

`.github/workflows/build.yml` configures, builds, and smoke-tests the application on every push and pull request.

`.github/workflows/release.yml` builds release artifacts for Ubuntu, Debian, Fedora, and generic Linux x86_64. When the project version in `CMakeLists.txt` changes on `main`, the workflow creates/updates the matching `vX.Y.Z` GitHub Release and uploads the generated packages.

## License

GNU General Public License v3.0. See `LICENSE` in the repository.
