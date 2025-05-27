# PhotoRec Non-Interactive CLI Mode

This fork of PhotoRec adds a fully non-interactive command-line mode that allows PhotoRec to be run on headless systems without any user interaction.

## Building

```bash
cd testdisk-fork
./autogen.sh   # If configure doesn't exist
./configure --without-ncurses --disable-qt
make
```

## Usage

The non-interactive mode is activated with the `--auto` flag:

```bash
./src/photorec --auto [options] disk_or_image
```

### Options

- `--auto` - Run in non-interactive mode (required)
- `--disk PATH` - Disk or image file path
- `--destination DIR` - Destination directory for recovered files (default: recup_dir)
- `--partition NUM` - Partition number (0=whole disk, 1=first partition, etc.)
- `--formats LIST` - Comma-separated list of file formats (e.g., jpg,doc,pdf) or 'all'
- `--free-space-only` - Recover only from free space
- `--keep-corrupted` - Keep corrupted files
- `--paranoid` - Enable paranoid mode (more thorough but slower)
- `--expert` - Enable expert mode
- `--lowmem` - Enable low memory mode
- `--blocksize SIZE` - Set block size (default: auto-detect)
- `--verbose` - Increase verbosity
- `--json` - Output progress in JSON format
- `--list-formats` - List all available file formats
- `--help` - Show help

### Examples

#### Basic recovery from disk
```bash
./src/photorec --auto --disk /dev/sdb --destination /tmp/recovered
```

#### Recover only JPG and PDF from partition 1
```bash
./src/photorec --auto --disk /dev/sdb --partition 1 --formats jpg,pdf --destination /tmp/recovered
```

#### Recover from disk image, free space only
```bash
./src/photorec --auto --disk disk.img --free-space-only --destination /tmp/recovered
```

#### List available formats
```bash
./src/photorec --auto --list-formats
```

#### JSON output for integration
```bash
./src/photorec --auto --disk /dev/sdb --destination /tmp/recovered --json
```

## Integration with Skavenger

In your skavenger codebase, you can now launch PhotoRec as a subprocess:

```go
cmd := exec.Command("/path/to/photorec", 
    "--auto",
    "--disk", devicePath,
    "--destination", outputDir,
    "--formats", "jpg,png,doc,pdf",
    "--json")

// Parse JSON output for progress updates
scanner := bufio.NewScanner(cmd.Stdout)
for scanner.Scan() {
    line := scanner.Text()
    // Parse JSON progress updates
}
```

## JSON Output Format

When using `--json`, PhotoRec outputs progress in JSON format:

```json
{"type":"progress","current":1024,"total":10240,"percent":10.0,"status":"Scanning"}
{"type":"result","files_recovered":150,"destination":"/tmp/recovered","status":"completed"}
```

## Notes

- The non-interactive mode bypasses all menus and prompts
- Default values are used for all unspecified options
- Errors are reported to stderr
- Exit codes: 0 = success, 1 = error

## Source Code Changes

The implementation is in:
- `src/phcli_auto.c` - Main non-interactive mode implementation
- `src/phcli_auto.h` - Header file
- `src/phmain.c` - Modified to check for --auto flag
- `src/Makefile.am` - Updated to include new files

The changes are minimal and don't affect the existing interactive mode.