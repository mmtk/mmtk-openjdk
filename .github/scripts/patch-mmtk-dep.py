import argparse
import os
import tomlkit

def modify_cargo_toml(cargo_path, ref=None, repo=None, path=None):
    # Read Cargo.toml
    with open(cargo_path, 'r') as file:
        data = tomlkit.parse(file.read())

    # Modify the mmtk dependency
    mmtk_dependency = data['dependencies'].get('mmtk')
    if mmtk_dependency is None:
        raise ValueError(f"Cannot find the mmtk dependency in {cargo_path}")

    if path:
        # Point to a local mmtk-core repo. Remove anything that would specify the source or the version.
        for key in ['git', 'branch', 'rev', 'tag', 'registry', 'version', 'path']:
            if key in mmtk_dependency:
                del mmtk_dependency[key]
        mmtk_dependency['path'] = os.path.realpath(path)
    else:
        if repo:
            mmtk_dependency['git'] = f'https://github.com/{repo}.git'
        mmtk_dependency['rev'] = ref

    # Write the modified Cargo.toml
    with open(cargo_path, 'w') as file:
        file.write(tomlkit.dumps(data))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Patch the mmtk-core dependency in Cargo.toml')
    parser.add_argument('manifest_path', help='Path to Cargo.toml')
    parser.add_argument('ref', nargs='?', help='The git revision of mmtk-core')
    parser.add_argument('repo', nargs='?', help='The mmtk-core repo on GitHub, such as mmtk/mmtk-core')
    parser.add_argument('--path', help='Path to a local mmtk-core repo. If given, ref and repo are ignored.')
    args = parser.parse_args()

    if args.path is None and args.ref is None:
        parser.error('Either ref or --path is required')

    modify_cargo_toml(args.manifest_path, ref=args.ref, repo=args.repo, path=args.path)
