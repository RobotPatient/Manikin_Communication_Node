#!/bin/bash
# Script to reset the repository to a known good state

# Confirm with user
echo "WARNING: This will reset the repository to commit bff84ae (Minor enhancements)"
echo "All local changes will be lost!"
read -p "Are you sure you want to continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]
then
    echo "Operation cancelled"
    exit 1
fi

# Reset to the specified commit
echo "Resetting to commit bff84ae..."
git reset --hard bff84ae

# Clean untracked files
echo "Removing untracked files..."
git clean -fd

echo "Repository has been reset to a known good state (bff84ae)."
echo "You can now build the project with './build.sh'"