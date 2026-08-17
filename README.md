# Ansible Inventory Studio

A native Qt 6 / C++ / QML editor for static Ansible YAML inventories.

The goal is simple: open an existing inventory, see hosts and groups as an interactive graph, edit the structure without hand-editing the whole YAML file, and save a valid inventory back to disk.

## Current MVP

- Open `.yml` / `.yaml` inventories.
- Parse `all`, `hosts`, `vars`, and nested `children`.
- Preserve hosts that belong to several groups.
- Preserve groups that have several parents.
- Interactive graph with pan, zoom, fit-to-view, search/highlight and clickable nodes.
- Expandable inventory tree.
- Create, rename and delete hosts and groups.
- Add/remove host membership in groups.
- Add/remove parent relationships between groups with cycle prevention.
- Edit host/group variables as a focused YAML mapping.
- Save through `QSaveFile` so the destination is replaced atomically.
- Canonical YAML output: `all` first, then global group definitions and group relationships.

## Dependencies

On Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake qt6-base qt6-declarative yaml-cpp
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/ansible-inventory-studio examples/inventory.yml
```

## Inventory model

This application does **not** treat an Ansible inventory as a real filesystem tree.
Hosts can be members of multiple groups and groups can have multiple parents, so the internal model is a graph. The left-hand tree is only a convenient projection of that graph.

The writer intentionally emits a canonical form instead of trying to preserve comments or the exact original formatting. Semantic inventory data is preserved; YAML presentation is not.

## Scope

This MVP targets the static YAML inventory plugin. Dynamic inventory plugin configuration files are not edited as if they were static inventories.

## Next useful steps

- Drag-and-drop membership/reparenting in the tree and graph.
- Typed key/value variable editor instead of the YAML mapping pane.
- Undo/redo stack.
- `ansible-inventory --list` validation action.
- Filter by group and hide/show host edges.
- Better layered graph layout for very large inventories.
- Package for Arch/AUR.

## CI

`.github/workflows/build.yml` configures and builds the project on Ubuntu for every push and pull request.

## License

GNU General Public License v3.0. See `LICENSE` in the repository.
