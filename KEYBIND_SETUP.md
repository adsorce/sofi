# Setting up Keybinds for Sofi

## For Niri

Add to your `~/.config/niri/config.kdl`:

### Option 1: Replace tofi with sofi
```kdl
// Replace the existing Mod+A line with:
Mod+A hotkey-overlay-title="Unified Search: sofi" { spawn "bash" "-c" "sofi-files || pkill sofi-files"; }
```

### Option 2: Add a new keybind for sofi-files
```kdl
// Keep Mod+A for apps, add new keybind for unified search
Mod+Space hotkey-overlay-title="Unified Search: sofi" { spawn "bash" "-c" "sofi-files || pkill sofi-files"; }
```

### Option 3: Different modes on different keys
```kdl
Mod+A hotkey-overlay-title="Launch App" { spawn "bash" "-c" "sofi-drun --drun-launch=true || pkill sofi-drun"; }
Mod+R hotkey-overlay-title="Run Command" { spawn "bash" "-c" "sofi-run || pkill sofi-run"; }
Mod+F hotkey-overlay-title="Find Files & Apps" { spawn "bash" "-c" "sofi-files || pkill sofi-files"; }
```

## For Sway

Add to your `~/.config/sway/config`:

```
# Unified search
bindsym $mod+space exec sofi-files

# Or replace the default launcher
bindsym $mod+d exec sofi-files
```

## For Hyprland

Add to your `~/.config/hypr/hyprland.conf`:

```
bind = $mainMod, SPACE, exec, sofi-files
```

## Notes

- The `|| pkill` part ensures that if sofi is already running, pressing the key again will close it
- Make sure sofi is installed system-wide first (`makepkg -si` or `./install.sh`)
- Reload your compositor config after making changes