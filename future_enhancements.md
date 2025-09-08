# Future Enhancements for Sorce Files Mode

## Priority 1 - Quick UX Wins
These improvements provide immediate user experience benefits with minimal implementation complexity:

### 1. Visual Type Indicators
- Add Unicode symbols to distinguish result types at a glance
  - `▶` or `⚡` for applications
  - `📄` for documents/files
  - `📁` for directories (if we add folder browsing)
  - `🖼️` for images, `🎵` for audio, `🎬` for video
- Position icons at the start of each line for easy scanning

### 2. Improved Path Display
- Replace `/home/username/` with `~/` for cleaner, shorter paths
- Truncate long paths with ellipsis: `/home/.../deeply/nested/`
- Consider right-aligning paths or using subtle separators like `•` or `│`
- Use dimmed/grey color for paths (already partially implemented)

### 3. Smart File Filtering
- Exclude common build artifacts and temporary files:
  - Object files: `.o`, `.obj`, `.class`
  - Python cache: `.pyc`, `__pycache__`
  - Editor backups: `~`, `.swp`, `.swo`
  - Dependencies: `node_modules/`, `vendor/`, `.venv/`
- Add these to the existing exclude_dirs array

### 4. Better Result Ordering
- Sort files by last modified time (most recent first)
- Prioritize user directories over system directories
- Weight by file type (documents before binaries)

## Priority 2 - Performance Improvements

### 5. Incremental Cache Updates
- Implement inotify-based file watching for real-time updates
- Update cache incrementally instead of full regeneration
- Separate caches for apps and files to update independently
- Background refresh while showing stale results

### 6. Progressive Search
- Start with smaller scope and expand if needed:
  1. Recently accessed files (last 24h)
  2. Priority directories (Downloads, Documents)
  3. Full home directory
  4. System directories
- Show results as soon as first batch is ready

### 7. Parallel Scanning
- Use threading to scan multiple directories simultaneously
- Prioritize likely directories based on usage patterns
- Implement work-stealing queue for better load balancing

## Priority 3 - Advanced Features

### 8. Search Operators
- `ext:pdf` - search only PDF files
- `in:downloads` - search specific directory
- `app:` - search only applications
- `file:` - search only files
- `size:>10mb` - filter by file size
- `/regex/` - regular expression support
- `modified:today` - filter by modification date

### 9. Frecency Ranking
- Track file selection history like apps
- Combine frequency + recency for scoring
- Boost frequently accessed files in results
- Show "Recent Files" section when search is empty

### 10. Keyboard Shortcuts
- `Ctrl+C` - copy selected path to clipboard
- `Ctrl+Shift+Enter` - open containing folder
- `Ctrl+D` - delete selected file (with confirmation)
- `Ctrl+R` - rename selected file
- `Tab` - cycle between apps/files sections
- `Ctrl+1-9` - quick select result by number

### 11. Actions Menu
- Right-click or long-press for context menu
- Actions: Open with..., Copy path, Move to trash, Properties
- Quick preview for text/image files
- Terminal here for directories

## Priority 4 - Visual Polish

### 12. Rich File Information
- File size indicators (human-readable: 1.2 MB)
- Modification date for recent changes (2 hours ago, yesterday)
- File type badges or color coding
- Permission indicators for executables/read-only

### 13. Syntax Highlighting
- Detect programming languages for code files
- Show first line or function name for context
- Highlight search matches in results
- Preview snippets for text files

### 14. Responsive Layout
- Multi-column layout for wide screens
- Adjust result count based on window size
- Compact mode with smaller fonts/spacing
- Grid view for image-heavy results

## Priority 5 - Extended Integration

### 15. Additional Search Sources
- Browser bookmarks and history
- Shell command history
- System settings and preferences
- Git repositories and branches
- SSH known hosts
- Docker containers/images
- Systemd services

### 16. Plugin System
- Allow custom search providers
- Python/Shell script plugins
- JSON-RPC or D-Bus interface
- Hot-reload plugin changes

### 17. Smart Actions
- Calculate math expressions
- Unit conversions
- Quick translations
- System commands (shutdown, lock, etc.)
- URL shortening/expanding

## Technical Improvements

### 18. Configuration
- User-configurable exclude patterns
- Adjustable cache size/location
- Customizable shortcuts
- Theme selection for colors/fonts

### 19. Accessibility
- Screen reader support
- High contrast mode
- Larger text options
- Keyboard-only navigation

### 20. Performance Metrics
- Add timing logs for optimization
- Memory usage monitoring
- Cache hit/miss statistics
- Search performance analytics

## Implementation Notes

- Many features can be implemented incrementally
- Start with Priority 1 items for immediate impact
- Consider creating a plugin API early to enable community contributions
- Maintain backwards compatibility with existing sorce configurations
- Test performance impact of each feature, especially for large file sets
- Consider making advanced features opt-in to keep the tool lightweight

## Current Implementation Status

### Completed
- ✅ Unified app and file search
- ✅ Basic file system scanning
- ✅ Cache system for performance
- ✅ Special formatting for file display
- ✅ Separation between apps and files in results
- ✅ Prompt text customization

### Known Limitations
- Fixed 5000 file limit
- Limited to 3 directory depth
- No incremental updates
- No file type filtering beyond basic excludes
- No frecency tracking for files
- Basic substring matching only