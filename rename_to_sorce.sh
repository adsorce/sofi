#!/bin/bash

# Script to rename tofi to sorce
# This will create a new directory with the renamed project

set -e  # Exit on error

SOURCE_DIR="/home/alex/tofi"
TARGET_DIR="/home/alex/sorce"

echo "=== Renaming tofi to sorce ==="
echo

# Check if target exists
if [ -d "$TARGET_DIR" ]; then
    echo "Error: $TARGET_DIR already exists!"
    echo "Please remove it or choose a different name."
    exit 1
fi

# Create a copy of the project
echo "1. Copying project to $TARGET_DIR..."
cp -r "$SOURCE_DIR" "$TARGET_DIR"
cd "$TARGET_DIR"

# Clean build artifacts
echo "2. Cleaning build artifacts..."
rm -rf build builddir .cache

# Rename in C source and header files
echo "3. Renaming in C source and header files..."
find . -type f \( -name "*.c" -o -name "*.h" \) | while read -r file; do
    # Skip binary files
    if file "$file" | grep -q "text"; then
        # Rename tofi variations
        sed -i 's/\btofi\b/sorce/g' "$file"
        sed -i 's/\bTofi\b/Sorce/g' "$file"
        sed -i 's/\bTOFI\b/SORCE/g' "$file"
        
        # Update cache/config paths
        sed -i 's/tofi-compgen/sorce-compgen/g' "$file"
        sed -i 's/tofi-files/sorce-files/g' "$file"
        sed -i 's/tofi-drun/sorce-drun/g' "$file"
        sed -i 's/tofi-run/sorce-run/g' "$file"
        
        # Update log messages
        sed -i 's/"This is tofi"/"This is sorce"/g' "$file"
        sed -i 's/tofi-launch\.log/sorce-launch\.log/g' "$file"
        sed -i 's/tofi-focus\.log/sorce-focus\.log/g' "$file"
    fi
done

# Rename the main header file
echo "4. Renaming header files..."
if [ -f "src/tofi.h" ]; then
    mv src/tofi.h src/sorce.h
fi

# Update include statements
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/#include "tofi\.h"/#include "sorce.h"/g' {} \;

# Update meson.build
echo "5. Updating build system..."
sed -i "s/project('tofi'/project('sorce'/g" meson.build
sed -i "s/'tofi'/'sorce'/g" meson.build
sed -i "s/tofi_sources/sorce_sources/g" meson.build
sed -i "s/tofi_deps/sorce_deps/g" meson.build
sed -i "s/tofi_args/sorce_args/g" meson.build
sed -i "s/executable('tofi'/executable('sorce'/g" meson.build

# Update symlink creation in meson.build
sed -i "s/tofi-run/sorce-run/g" meson.build
sed -i "s/tofi-drun/sorce-drun/g" meson.build
sed -i "s/tofi-files/sorce-files/g" meson.build
sed -i "s/tofi-compgen/sorce-compgen/g" meson.build

# Update doc directory if it exists
if [ -d "doc" ]; then
    echo "6. Updating documentation..."
    find doc -type f -name "*.md" -o -name "*.1" -o -name "*.5" | while read -r file; do
        sed -i 's/\btofi\b/sorce/g' "$file"
        sed -i 's/\bTofi\b/Sorce/g' "$file"
        sed -i 's/\bTOFI\b/SORCE/g' "$file"
    done
    
    # Rename man pages if they exist
    for man in doc/tofi*.1 doc/tofi*.5; do
        if [ -f "$man" ]; then
            newname=$(echo "$man" | sed 's/tofi/sorce/')
            mv "$man" "$newname"
        fi
    done
fi

# Update README
echo "7. Updating README..."
if [ -f "README.md" ]; then
    # Replace tofi references
    sed -i 's/\btofi\b/sorce/g' README.md
    sed -i 's/\bTofi\b/Sorce/g' README.md
    sed -i 's/\bTOFI\b/SORCE/g' README.md
    
    # Add attribution if not already present
    if ! grep -q "Attribution" README.md; then
        cat >> README.md << 'EOF'

## Attribution

Sorce is based on [tofi](https://github.com/philj56/tofi) by Philip Jones, originally released under the MIT License.

### Original MIT License

Copyright (c) 2021-2023 Philip Jones

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### Modifications

Copyright (c) 2024 [Your Name]

The unified file search functionality and additional features were added by [Your Name].
EOF
    fi
fi

# Update LICENSE file to include both attributions
echo "8. Updating LICENSE..."
if [ -f "LICENSE" ]; then
    if ! grep -q "Sorce modifications" LICENSE; then
        cat >> LICENSE << 'EOF'

================================================================================
Sorce modifications and additions
Copyright (c) 2024 [Your Name]

The above MIT License applies to all modifications and additions made to create
Sorce from the original tofi codebase.
EOF
    fi
fi

# Create a new config example
echo "9. Creating example config..."
mkdir -p examples
cat > examples/config << 'EOF'
# Sorce configuration file
# Place this in ~/.config/sorce/config

# Window appearance
width = 100%
height = 100%
border-width = 0
outline-width = 0
padding-left = 35%
padding-top = 35%
result-spacing = 25
num-results = 5
font = monospace
background-color = #1B1D1E
selection-color = #F92672

# Behavior
hide-cursor = true
text-cursor = false
history = true
fuzzy-match = false
EOF

# Update test files if they exist
if [ -d "test" ]; then
    echo "10. Updating test files..."
    find test -type f -name "*.c" | while read -r file; do
        sed -i 's/\btofi\b/sorce/g' "$file"
        sed -i 's/\bTOFI\b/SORCE/g' "$file"
    done
fi

# Update future_enhancements.md if it exists
if [ -f "future_enhancements.md" ]; then
    echo "11. Updating future enhancements doc..."
    sed -i 's/Tofi Files Mode/Sorce Files Mode/g' future_enhancements.md
    sed -i 's/tofi/sorce/g' future_enhancements.md
fi

echo
echo "=== Renaming complete! ==="
echo
echo "Next steps:"
echo "1. Update the [Your Name] placeholders in README.md and LICENSE"
echo "2. Build the renamed project:"
echo "   cd $TARGET_DIR"
echo "   meson setup build"
echo "   cd build"
echo "   ninja"
echo
echo "3. Test the executables:"
echo "   ./sorce"
echo "   ./sorce-run"
echo "   ./sorce-drun"
echo "   ./sorce-files"
echo
echo "4. Create a git repository:"
echo "   cd $TARGET_DIR"
echo "   git init"
echo "   git add ."
echo "   git commit -m \"Initial commit: Sorce launcher based on tofi\""
echo
echo "5. Optional: Update the config path in your shell:"
echo "   mkdir -p ~/.config/sorce"
echo "   cp examples/config ~/.config/sorce/"