import sys
import tomlkit

def modify_cargo_toml(cargo_path, ref, repo=None):
    # Read Cargo.toml
    with open(cargo_path, 'r') as file:
        data = tomlkit.parse(file.read())

    # Modify the mmtk dependency
    mmtk_dependency = data['dependencies'].get('mmtk')
    if mmtk_dependency:
        if repo:
            mmtk_dependency['git'] = f'https://github.com/{repo}.git'
        mmtk_dependency['rev'] = ref

    # Write the modified Cargo.toml
    with open(cargo_path, 'w') as file:
        file.write(tomlkit.dumps(data))

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python script.py <manifest_path> <ref> [repo]")
        sys.exit(1)

    cargo_toml = sys.argv[1]
    ref = sys.argv[2]
    repo = sys.argv[3] if len(sys.argv) > 3 else None

    modify_cargo_toml(cargo_toml, ref, repo)
