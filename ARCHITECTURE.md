# Architecture

## Domain model

The inventory is represented as a graph, not as a filesystem tree.

- `HostRecord` is a globally unique host object with YAML variables and a set of direct group memberships.
- `GroupRecord` is a globally unique group object with YAML variables, child groups, parent groups and direct hosts.
- `all` is represented explicitly in the editor, while its relationship to every host/group remains conceptually implicit.

This matches Ansible's model: a host may belong to multiple groups and a group may have multiple parents. Circular group relationships are rejected.

## YAML input

`yaml-cpp` parses the static YAML inventory source directly. The importer understands:

- `all`
- top-level group definitions
- `hosts`
- host variables
- group `vars`
- nested `children`
- repeated global host/group definitions used to express multiple memberships

The editor currently targets static YAML inventory files, not YAML configuration files for dynamic inventory plugins.

## YAML output

The writer emits a canonical representation:

1. `all` is emitted first.
2. Hosts that have no non-`all` group are stored under `all.hosts`.
3. Top-level groups are referenced by `all.children`.
4. Every concrete group is then emitted as a top-level global definition.
5. Child-group relationships are stored as references in each group's `children` map.
6. A host that belongs to several groups has its variables emitted once at a stable owner location; its other memberships are emitted as empty host mappings.

The result preserves inventory semantics but does not preserve comments, anchors' exact textual presentation, whitespace, or key ordering from the original file.

## UI

The Qt/QML UI is split into three areas:

- **Tree:** compact expandable projection of groups and direct host membership.
- **Graph:** layered view of group relationships and host membership, with pan/zoom/search.
- **Inspector:** rename objects, edit membership/parent relationships and edit variables.

The tree is intentionally only a projection. The domain graph remains the source of truth.

## Safety

- Writes use `QSaveFile` for atomic replacement.
- Group cycles are rejected on import and when editing.
- Deleting a group does not delete its hosts or child groups; they become members of their remaining groups or top-level/ungrouped objects.
