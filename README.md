# Terminal Markdown Editor (termineditor)

A simple, cross-platform-ish terminal-based Markdown editor (with live preview in split view or full preview mode). This version is adapted for Windows consoles, using Win32 APIs and ANSI escape sequences.

![image](https://github.com/user-attachments/assets/73783b8e-33cc-461e-8e07-118348ad3587)

Split view:
![image](https://github.com/user-attachments/assets/f55acfab-7110-4995-bbaa-4869491459a5)

## Features

- **Real-time preview**: Toggle between edit-only, split, and preview-only modes (`Ctrl+P`).
- **Basic Markdown support**: Renders headers, list items, horizontal rules, bold, italic, and inline code.
- **File I/O**: Load a file on startup and save (`Ctrl+S`) any changes made.
- **Minimal, single C file**: Just one `.c` source file.
- **Cursor-based editing**: Move around, insert text, backspace, and new lines.

## Requirements

- A **Windows** console that supports **ANSI escape sequences**:
  - Windows 10 or later typically has `ENABLE_VIRTUAL_TERMINAL_PROCESSING`.
  - If on older Windows, you may need a workaround or an updated console driver.
- A **C compiler** that can compile and link Win32 APIs (Visual Studio, MinGW, etc.).

## Building

1. **Install** a C compiler on Windows (e.g., MSVC, MinGW, etc.).
2. **Open** a terminal or developer command prompt in the project folder.
3. **Compile** the source:

   ```bash
   cl termineditor.c /Fe:termineditor.exe
   ```
   or, if using MinGW:
   ```bash
   gcc termineditor.c -o termineditor.exe
   ```

4. **Run** the generated `termineditor.exe` in a Windows console that supports ANSI sequences.

## Running

You can launch the editor with an optional filename. For example:

```bash
.\termineditor myfile.md
```

If the file does not exist, it will start editing a new (empty) file. If you omit the filename, it defaults to an unnamed buffer that you can later save as `untitled.md`.

## Keybindings

- **`Ctrl+Q`**  
  Quit the editor. If unsaved changes exist, it will prompt you to press `Ctrl+Q` again to confirm.
  
- **`Ctrl+S`**  
  Save the current file (writes the buffer contents to disk).

- **`Ctrl+P`**  
  Cycle preview modes: **edit-only** → **split** → **preview-only** → back to **edit-only**.

- **Arrow Keys**  
  Move cursor left, right, up, or down (if lines exist).

- **Backspace**  
  Deletes the character to the left of the cursor (or merges lines if at the beginning).

- **Enter (Return)**  
  Insert a new line at the cursor position.

- **Page Up / Page Down**  
  Move the cursor up or down one screen's worth of lines.

- **Home / End**  
  Jump to the beginning or end of the line.

*(If you originally had “vim-style movement” using `h/j/k/l`, note that you can reintroduce or remove those mappings as needed.)*

## Preview Mode Details

- **Edit-only**: You see just the text buffer.  
- **Split view**: Left side is the raw text, right side is the rendered Markdown preview.  
- **Preview-only**: You only see the rendered Markdown, no raw text.

## Markdown Rendering

Supported constructs:
- **Headers** using `#`, `##`, `###`, etc.
- **List items** with `-`, `*`, or `+`.
- **Horizontal rules** if you have three or more `-` (or `*` or `_`) on a line.
- **Code blocks** that begin with ````` ``` `````.
- **Inline bold** if surrounded by `**double asterisks**`.
- **Inline italic** if surrounded by a single `*`.
- **Inline code** if surrounded by `` ` `` characters.

## Known Limitations / Caveats

1. **Search** (`Ctrl+F`) is just a placeholder: “Search function not implemented yet.”
2. **Terminal size**: If the console is resized during use, we attempt to re-check dimensions, but some Windows consoles may require you to exit and relaunch for the changes to be recognized reliably.
3. **Older Windows**: On Windows older than 10, ANSI escape processing might not be available by default. You can either upgrade or install a third-party console (such as ConEmu) that supports ANSI sequences.
4. **Line wrapping**: This editor does not do automatic word wrapping or long-line soft wrapping. You must manually break lines.

## Contributing

Contributions or suggestions are welcome! Feel free to:

- **Open issues** if you spot bugs.
- **Submit PRs** (pull requests) if you have improvements or new features.
- Provide feedback on how to handle differences in console behavior on various Windows versions.

## License

MIT, do whatever you want.
