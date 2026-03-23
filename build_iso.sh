#!/bin/bash
set -e

mkdir -p BuildIso/EFI/BOOT
cp UefiCalculator.efi BuildIso/
cp Shell*.efi BuildIso/EFI/BOOT/BOOTX64.EFI
cp startup.nsh BuildIso/

xorriso -as mkisofs -R -J -V "UEFI-CALCULATOR" -o UefiCalculator.iso ./BuildIso

echo "Done!"
rm -rf BuildIso