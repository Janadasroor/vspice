#!/bin/bash

# Get the absolute path of the current directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$( dirname "$DIR" )"

echo "Installing viospice file associations..."

# 1. Update the .desktop file with correct paths
SED_EXEC="s|Exec=.*|Exec=$PROJECT_DIR/build/viospice %f|"
SED_ICON="s|Icon=.*|Icon=$PROJECT_DIR/resources/icons/app_icon.svg|"
sed -i "$SED_EXEC" "$DIR/viospice.desktop"
sed -i "$SED_ICON" "$DIR/viospice.desktop"

# 2. Copy the .desktop file to the local applications folder
mkdir -p ~/.local/share/applications
cp "$DIR/viospice.desktop" ~/.local/share/applications/

# 3. Install MIME types
mkdir -p ~/.local/share/mime/packages
cp "$DIR/viospice-mime.xml" ~/.local/share/mime/packages/
update-mime-database ~/.local/share/mime

# 4. Update desktop database
update-desktop-database ~/.local/share/applications

echo "Done! You can now open .sch, .flux, and .cir files with viospice."
