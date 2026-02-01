# UI Template

This directory contains a complete UI template for the installer GUI. You can customize this template to match your application's branding.

## Directory Structure

```
ui-template/
├── index.html          # Main HTML file (required)
├── styles/
│   └── main.css        # Stylesheet with customizable CSS variables
├── scripts/
│   └── main.js         # JavaScript logic for UI interactions
├── locales/            # Translation files
│   ├── en-US.json      # English
│   ├── zh-CN.json      # Chinese (Simplified)
│   ├── ja-JP.json      # Japanese
│   ├── ko-KR.json      # Korean
│   ├── de-DE.json      # German
│   ├── fr-FR.json      # French
│   └── es-ES.json      # Spanish
└── README.md           # This file
```

## Customization

### Colors and Branding

Edit the CSS variables in `styles/main.css` to match your brand:

```css
:root {
    /* Primary colors - customize these for your brand */
    --primary-color: #667eea;
    --primary-dark: #764ba2;
    
    /* Status colors */
    --success-color: #4caf50;
    --error-color: #f44336;
    --warning-color: #ff9800;
}
```

### Adding a Logo

Replace the `.logo-placeholder` div with your logo image:

```html
<div class="logo-container">
    <img src="images/logo.png" alt="Logo" class="logo">
</div>
```

### Adding New Languages

1. Create a new JSON file in `locales/` (e.g., `pt-BR.json`)
2. Copy the structure from `en-US.json`
3. Translate all strings
4. Add the language option to the selector in `index.html`:

```html
<select id="language-select">
    <!-- existing options -->
    <option value="pt-BR">Português</option>
</select>
```

## Translation Keys

| Key | Description |
|-----|-------------|
| `welcome.title` | Welcome page title |
| `welcome.description` | Welcome page description (supports `{appName}` variable) |
| `welcome.license` | License section header |
| `welcome.accept_license` | License acceptance checkbox label |
| `install.directory` | Directory selection page title |
| `install.directory_desc` | Directory selection description |
| `install.progress` | Progress page title |
| `install.complete` | Completion page title |
| `install.success` | Success message |
| `install.error` | Error page title |
| `button.*` | Button labels |
| `option.*` | Checkbox option labels |
| `error.*` | Error messages |
| `phase.*` | Installation phase descriptions |

## Tauri Integration

The UI communicates with the Rust backend through Tauri commands and events.

### Available Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `get_metadata` | `packagePath` | Get package metadata |
| `get_system_locale` | - | Get system language |
| `get_translations` | `locale` | Get translation strings |
| `browse_directory` | `defaultPath` | Open directory picker |
| `start_install` | `request` | Start installation |
| `cancel_install` | - | Cancel installation |

### Events

| Event | Payload | Description |
|-------|---------|-------------|
| `install_progress` | Progress object | Installation progress update |
| `install_complete` | Stats object | Installation completed |
| `install_error` | Error object | Installation failed |

## Usage

1. Copy this template to your project
2. Customize the styles and translations
3. Specify the UI directory in your packager configuration:

```json
{
  "ui_resources_dir": "./ui-template"
}
```

4. Build your installer package:

```bash
packager --input ./my-app --output ./installer.mti
```

## Requirements

- The `index.html` file is required
- The `locales/` directory should contain at least one translation file
- All paths in HTML should be relative to the UI root directory
