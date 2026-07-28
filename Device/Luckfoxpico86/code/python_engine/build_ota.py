#!/usr/bin/env python3
"""
Build OTA package for Python BMS Engine

Usage:
    python3 build_ota.py <version>
    
Example:
    python3 build_ota.py 1.1.0
"""

import os
import sys
import tarfile
import hashlib
import json
import shutil
from pathlib import Path


def calculate_sha256(file_path: Path) -> str:
    """
    Calculate SHA256 hash of file
    
    Args:
        file_path: Path to file
        
    Returns:
        SHA256 hash as hex string
    """
    sha256_hash = hashlib.sha256()
    with open(file_path, 'rb') as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()


def build_ota_package(version: str, source_dir: Path, output_dir: Path):
    """
    Build OTA package
    
    Args:
        version: Version string (e.g., "1.1.0")
        source_dir: Source directory
        output_dir: Output directory for package
    """
    print(f"Building OTA package version {version}...")
    
    # Create package directory
    package_name = f"bms_v{version}"
    package_dir = output_dir / package_name
    
    if package_dir.exists():
        print(f"Removing existing package directory: {package_dir}")
        shutil.rmtree(package_dir)
    
    # Copy source files
    print(f"Copying source files from {source_dir} to {package_dir}")
    shutil.copytree(source_dir, package_dir)
    
    # Remove unnecessary files
    remove_patterns = ['__pycache__', '*.pyc', '.git', '.gitignore', 'ota']
    for pattern in remove_patterns:
        for item in package_dir.rglob(pattern):
            if item.is_dir():
                shutil.rmtree(item)
            elif item.is_file():
                item.unlink()
    
    # Create VERSION file
    version_file = package_dir / 'VERSION'
    version_file.write_text(version)
    print(f"Created VERSION file: {version}")
    
    # Create tarball
    tarball_name = f"{package_name}.tar.gz"
    tarball_path = output_dir / tarball_name
    
    print(f"Creating tarball: {tarball_name}")
    with tarfile.open(tarball_path, 'w:gz') as tar:
        tar.add(package_dir, arcname=package_name)
    
    # Calculate SHA256
    sha256 = calculate_sha256(tarball_path)
    file_size = tarball_path.stat().st_size
    print(f"SHA256: {sha256}")
    print(f"Size: {file_size:,} bytes ({file_size / 1024:.1f} KB)")
    
    # Create check.json
    check_json = {
        'version': version,
        'filename': tarball_name,
        'sha256': sha256,
        'url': f'http://192.168.1.171/ota/bms/{tarball_name}',
        'release_notes': f'Version {version}',
        'min_version': '1.0.0',
        'force_update': False
    }
    
    check_json_path = output_dir / 'check.json'
    with open(check_json_path, 'w') as f:
        json.dump(check_json, f, indent=2)
    
    print(f"Created manifest: {check_json_path}")
    
    # Cleanup package directory
    print(f"Cleaning up package directory: {package_dir}")
    shutil.rmtree(package_dir)
    
    print("\n" + "="*60)
    print("OTA package built successfully!")
    print("="*60)
    print(f"Package: {tarball_path}")
    print(f"Manifest: {check_json_path}")
    print(f"Version: {version}")
    print(f"SHA256: {sha256}")
    print("="*60)
    print("\nNext steps:")
    print(f"1. Deploy to OTA server: ./deploy_ota.sh {version}")
    print(f"2. Or test locally: sudo cp {tarball_path} /var/www/ota_root/bms/")
    print(f"   sudo cp {check_json_path} /var/www/ota_root/bms/")


def main():
    """Main entry point"""
    if len(sys.argv) != 2:
        print("Usage: python3 build_ota.py <version>")
        print("Example: python3 build_ota.py 1.1.0")
        sys.exit(1)
    
    version = sys.argv[1]
    
    # Validate version format
    if not version.replace('.', '').isdigit():
        print(f"Error: Invalid version format: {version}")
        print("Version should be in format: X.Y.Z (e.g., 1.1.0)")
        sys.exit(1)
    
    source_dir = Path(__file__).parent
    output_dir = source_dir / 'ota'
    output_dir.mkdir(exist_ok=True)
    
    build_ota_package(version, source_dir, output_dir)


if __name__ == '__main__':
    main()
