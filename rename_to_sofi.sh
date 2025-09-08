#!/bin/bash

# Script to rename sorce to sofi throughout the codebase
# This maintains all attribution to the original tofi project

set -e

echo "Renaming sorce to sofi..."

# Rename binary references in meson.build
sed -i "s/'sorce'/'sofi'/g" meson.build
sed -i "s/'sorce-run'/'sofi-run'/g" meson.build
sed -i "s/'sorce-drun'/'sofi-drun'/g" meson.build
sed -i "s/'sorce-files'/'sofi-files'/g" meson.build

# Update man pages
find doc -name "*.md" -o -name "*.scd" | xargs sed -i 's/sorce/sofi/g'
find doc -name "*.md" -o -name "*.scd" | xargs sed -i 's/Sorce/Sofi/g'
find doc -name "*.md" -o -name "*.scd" | xargs sed -i 's/SORCE/SOFI/g'

# Rename man page files
mv doc/sorce.1.scd doc/sofi.1.scd 2>/dev/null || true
mv doc/sorce.5.scd doc/sofi.5.scd 2>/dev/null || true

# Update source files
find src -name "*.c" -o -name "*.h" | xargs sed -i 's/sorce/sofi/g'
find src -name "*.c" -o -name "*.h" | xargs sed -i 's/Sorce/Sofi/g'
find src -name "*.c" -o -name "*.h" | xargs sed -i 's/SORCE/SOFI/g'

# Update README
sed -i 's/sorce/sofi/g' README.md
sed -i 's/Sorce/Sofi/g' README.md
sed -i 's/SORCE/SOFI/g' README.md
sed -i 's|github.com/adsorce/sorce|github.com/adsofi/sofi|g' README.md
sed -i 's|adsorce/sorce|adsofi/sofi|g' README.md

# Update installation script
sed -i 's/sorce/sofi/g' install.sh
sed -i 's/Sorce/Sofi/g' install.sh
sed -i 's/SORCE/SOFI/g' install.sh

# Update PKGBUILD
sed -i "s/pkgname=sorce/pkgname=sofi/g" PKGBUILD
sed -i 's|github.com/adsorce/sorce|github.com/adsofi/sofi|g' PKGBUILD
sed -i 's/"sorce-/"sofi-/g' PKGBUILD
sed -i 's/Sorce/Sofi/g' PKGBUILD

# Update .SRCINFO
sed -i "s/pkgbase = sorce/pkgbase = sofi/g" .SRCINFO
sed -i "s/pkgname = sorce/pkgname = sofi/g" .SRCINFO
sed -i 's|github.com/adsorce/sorce|github.com/adsofi/sofi|g' .SRCINFO
sed -i 's/sorce-/sofi-/g' .SRCINFO
sed -i 's/Sorce/Sofi/g' .SRCINFO

# Update AUR submission guide
sed -i 's/sorce/sofi/g' AUR_SUBMISSION.md
sed -i 's/Sorce/Sofi/g' AUR_SUBMISSION.md
sed -i 's|github.com/adsorce/sorce|github.com/adsofi/sofi|g' AUR_SUBMISSION.md

# Update keybind setup
sed -i 's/sorce/sofi/g' KEYBIND_SETUP.md
sed -i 's/Sorce/Sofi/g' KEYBIND_SETUP.md

# Update examples if they exist
if [ -d "examples" ]; then
    find examples -type f | xargs sed -i 's/sorce/sofi/g' 2>/dev/null || true
    find examples -type f | xargs sed -i 's/Sorce/Sofi/g' 2>/dev/null || true
fi

# Update config directory references
find . -name "*.c" -o -name "*.h" -o -name "*.md" | xargs sed -i 's|\.config/sorce|.config/sofi|g'

# Rename any sorce-specific files
for file in sorce*; do
    if [ -f "$file" ]; then
        newname=$(echo "$file" | sed 's/sorce/sofi/')
        mv "$file" "$newname"
        echo "Renamed $file to $newname"
    fi
done

echo ""
echo "✅ Renaming complete!"
echo ""
echo "Next steps:"
echo "1. Clean and rebuild: rm -rf build && meson setup build && cd build && ninja"
echo "2. Test the binaries work with new names"
echo "3. Update GitHub repo name to 'sofi'"
echo "4. Update git remote: git remote set-url origin git@github.com:adsofi/sofi.git"
echo "5. Commit and push changes"
echo ""
echo "The name 'Sofi' continues the tradition (rofi → tofi → sofi)"
echo "and Sofia means 'wisdom' in Greek - perfect for an all-knowing search tool!"