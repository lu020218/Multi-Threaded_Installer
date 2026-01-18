# XML Layout Modification Guide

## Overview

This guide explains how to modify the installer's user interface by editing XML layout files. The installer uses DuiLib framework with XML-based UI definitions, allowing you to customize the appearance without recompiling C++ code.

## Prerequisites

- Text editor (VS Code, Notepad++, or any XML editor)
- Basic understanding of XML syntax
- Familiarity with UI layout concepts
- DuiLib documentation (optional, for advanced features)

## XML File Structure

### Location

All XML layout files are located in:
```
resources/skins/
├── main.xml              # Main window framework
├── welcome_page.xml      # Welcome page layout
├── progress_page.xml     # Progress page layout
├── completion_page.xml   # Completion page layout
├── license.xml           # License dialog layout
└── styles.xml            # Shared styles (optional)
```

### File Organization

**main.xml**: Contains the window frame, title bar, and TabLayout container that hosts the three pages.

**Page XML files**: Define individual page layouts (welcome, progress, completion).

**license.xml**: Defines the modal license agreement dialog.

**styles.xml**: Optional file for shared style definitions.

## Basic XML Structure

### Window Definition

```xml
<?xml version="1.0" encoding="utf-8"?>
<Window size="600,450" caption="0,0,0,35" roundcorner="5,5">
    <!-- Window content -->
</Window>
```

**Attributes**:
- `size`: Window dimensions (width,height) in pixels
- `caption`: Caption bar area (left,top,right,bottom)
- `roundcorner`: Corner radius (x,y) for rounded corners

### Font Definitions

```xml
<Font id="0" name="微软雅黑" size="12"/>
<Font id="1" name="微软雅黑" size="16" bold="true"/>
<Font id="2" name="微软雅黑" size="20" bold="true"/>
```

**Attributes**:
- `id`: Numeric identifier (0, 1, 2, ...)
- `name`: Font family name
- `size`: Font size in points
- `bold`: "true" or "false"
- `italic`: "true" or "false" (optional)
- `underline`: "true" or "false" (optional)

### Default Control Styles

```xml
<Default name="Button" 
         normalimage="button_normal.png" 
         hotimage="button_hover.png" 
         pushedimage="button_pushed.png" 
         textcolor="#FFFFFFFF" 
         font="0"/>
```

This sets default properties for all Button controls.

## Layout Containers

### VerticalLayout

Arranges child controls vertically (top to bottom).

```xml
<VerticalLayout padding="20,20,20,20">
    <Label text="First item" height="30"/>
    <Label text="Second item" height="30"/>
    <Label text="Third item" height="30"/>
</VerticalLayout>
```

**Attributes**:
- `padding`: Inner spacing (left,top,right,bottom)
- `bkcolor`: Background color (#AARRGGBB)
- `bkimage`: Background image file

### HorizontalLayout

Arranges child controls horizontally (left to right).

```xml
<HorizontalLayout height="40">
    <Button text="OK" width="80"/>
    <Control width="10"/>  <!-- Spacer -->
    <Button text="Cancel" width="80"/>
</HorizontalLayout>
```

**Attributes**:
- `height`: Fixed height in pixels
- `padding`: Inner spacing
- `childpadding`: Spacing between children

### TabLayout

Container for multiple pages (only one visible at a time).

```xml
<TabLayout name="pages">
    <Include source="welcome_page.xml"/>
    <Include source="progress_page.xml"/>
    <Include source="completion_page.xml"/>
</TabLayout>
```

**Usage**: Pages are switched programmatically by index (0, 1, 2).

## Common Controls

### Label

Displays static text.

```xml
<Label name="app_name" 
       text="My Application" 
       font="2" 
       textcolor="#FF333333" 
       align="center" 
       height="30"/>
```

**Attributes**:
- `name`: Unique identifier for C++ code
- `text`: Display text
- `font`: Font ID (from Font definitions)
- `textcolor`: Text color (#AARRGGBB)
- `align`: "left", "center", or "right"
- `height`: Fixed height in pixels
- `width`: Fixed width in pixels

### Button

Clickable button control.

```xml
<Button name="install_button" 
        text="Install" 
        width="100" 
        height="35" 
        enabled="true"
        normalimage="button_normal.png"
        hotimage="button_hover.png"
        pushedimage="button_pushed.png"/>
```

**Attributes**:
- `name`: Unique identifier (used in C++ event handlers)
- `text`: Button label
- `enabled`: "true" or "false"
- `normalimage`: Image for normal state
- `hotimage`: Image for hover state
- `pushedimage`: Image for pressed state
- `textcolor`: Text color

**Important**: The `name` attribute must match the name used in C++ code for event handling.

### Edit

Single-line text input box.

```xml
<Edit name="install_path" 
      text="C:\Program Files\MyApp"
      bkcolor="#FFF5F5F5"
      bordercolor="#FFCCCCCC"
      bordersize="1"
      textpadding="5,5,5,5"/>
```

**Attributes**:
- `name`: Unique identifier
- `text`: Default text
- `readonly`: "true" or "false"
- `password`: "true" for password field
- `bkcolor`: Background color
- `bordercolor`: Border color
- `bordersize`: Border width in pixels
- `textpadding`: Text padding (left,top,right,bottom)

### CheckBox

Checkbox with label.

```xml
<CheckBox name="license_checkbox" 
          text="I agree to the license terms" 
          height="25"
          selected="false"/>
```

**Attributes**:
- `name`: Unique identifier
- `text`: Label text
- `selected`: "true" or "false" (default state)
- `normalimage`: Unchecked image
- `selectedimage`: Checked image

### Progress

Progress bar control.

```xml
<Progress name="progress_bar" 
          value="0" 
          height="25"
          foreimage="progress_fore.png"
          bkimage="progress_bk.png"
          min="0"
          max="100"/>
```

**Attributes**:
- `name`: Unique identifier
- `value`: Current value (0-100)
- `min`: Minimum value (default 0)
- `max`: Maximum value (default 100)
- `foreimage`: Foreground (filled) image
- `bkimage`: Background (empty) image
- `hor`: "true" for horizontal, "false" for vertical

### RichEdit

Multi-line text box with rich text support.

```xml
<RichEdit name="license_text" 
          vscrollbar="true" 
          readonly="true"
          bkcolor="#FFF5F5F5"
          bordercolor="#FFCCCCCC"
          bordersize="1"/>
```

**Attributes**:
- `name`: Unique identifier
- `vscrollbar`: "true" to show vertical scrollbar
- `hscrollbar`: "true" to show horizontal scrollbar
- `readonly`: "true" or "false"
- `multiline`: "true" or "false"
- `wanttab`: "true" to allow tab character
- `wantreturn`: "true" to allow enter key

### Control

Empty control used as spacer or container.

```xml
<Control/>  <!-- Flexible spacer (expands to fill) -->
<Control width="10"/>  <!-- Fixed 10px spacer -->
<Control height="20"/>  <!-- Fixed 20px spacer -->
```

**Usage**: 
- Empty `<Control/>` acts as flexible spacer (like CSS flex-grow)
- With fixed width/height, acts as fixed spacer

### Image Control

Displays an image.

```xml
<Control name="app_logo" 
         width="64" 
         height="64" 
         bkimage="logo.png"/>
```

**Attributes**:
- `bkimage`: Image file name (from resources/images/)
- `width`: Image width
- `height`: Image height

## Color Format

Colors use ARGB format: `#AARRGGBB`

- `AA`: Alpha (transparency) - 00 (transparent) to FF (opaque)
- `RR`: Red component - 00 to FF
- `GG`: Green component - 00 to FF
- `BB`: Blue component - 00 to FF

**Examples**:
- `#FFFFFFFF`: Opaque white
- `#FF000000`: Opaque black
- `#FF4A90E2`: Opaque blue
- `#80FF0000`: 50% transparent red
- `#00000000`: Fully transparent

## Common Modifications

### Changing Window Size

Edit `main.xml`:

```xml
<Window size="800,600" caption="0,0,0,35">
```

Change `size="800,600"` to desired width and height.

**Note**: Also update page layouts if needed to accommodate new size.

### Changing Application Name and Version

Edit each page XML file (welcome_page.xml, progress_page.xml, completion_page.xml):

```xml
<Label name="app_name" text="My New Application" font="2"/>
<Label name="app_version" text="Version 2.0.0" font="0"/>
```

**Alternative**: These can be set dynamically from C++ code using the InstallConfig structure.

### Changing Colors

**Background Color**:
```xml
<VerticalLayout bkcolor="#FFF5F5F5">
```

**Text Color**:
```xml
<Label text="Hello" textcolor="#FF333333"/>
```

**Button Colors**: Use custom images or modify existing button images.

### Changing Fonts

**Add New Font**:
```xml
<Font id="3" name="Arial" size="14" bold="false"/>
```

**Use Font**:
```xml
<Label text="Text" font="3"/>
```

**Available Fonts**: Any font installed on Windows system.

### Changing Button Text

```xml
<Button name="install_button" text="开始安装"/>
<Button name="cancel_button" text="取消"/>
<Button name="finish_button" text="完成"/>
```

**Important**: If changing button names (the `name` attribute), you must also update the C++ code that references these names.

### Adding New Controls

**Example**: Add a new checkbox to welcome page.

Edit `welcome_page.xml`:

```xml
<CheckBox name="create_shortcut_checkbox" 
          text="Create desktop shortcut" 
          height="25"/>
```

**Then update C++ code** in `welcome_page_controller.cpp` to handle the new control:

```cpp
CCheckBoxUI* m_pCreateShortcutCheckbox = 
    static_cast<CCheckBoxUI*>(pManager->FindControl(L"create_shortcut_checkbox"));
```

### Changing Layout Spacing

**Padding** (space inside container):
```xml
<VerticalLayout padding="40,30,40,30">
```
Format: left, top, right, bottom

**Child Padding** (space between children):
```xml
<VerticalLayout childpadding="10">
```

**Individual Control Spacing**:
```xml
<Control height="20"/>  <!-- 20px vertical space -->
<Control width="10"/>   <!-- 10px horizontal space -->
```

### Changing Control Sizes

**Fixed Size**:
```xml
<Button width="120" height="40"/>
```

**Flexible Size** (fills available space):
```xml
<Button/>  <!-- No width/height specified -->
```

**Percentage-Based** (not directly supported, use flexible sizing):
```xml
<HorizontalLayout>
    <Control/>  <!-- Takes remaining space -->
    <Button width="100"/>  <!-- Fixed 100px -->
</HorizontalLayout>
```

## Advanced Techniques

### Using Include for Modularity

In `main.xml`:

```xml
<TabLayout name="pages">
    <Include source="welcome_page.xml"/>
    <Include source="progress_page.xml"/>
    <Include source="completion_page.xml"/>
</TabLayout>
```

This loads external XML files, keeping layouts organized.

### Shared Styles

Create `styles.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Styles>
    <Style name="primary_button" 
           normalimage="button_primary_normal.png" 
           hotimage="button_primary_hover.png" 
           textcolor="#FFFFFFFF"/>
</Styles>
```

Apply style:
```xml
<Button name="install_button" style="primary_button" text="Install"/>
```

**Note**: Requires loading styles.xml in main window initialization.

### Conditional Visibility

Controls can be shown/hidden from C++ code:

```xml
<Label name="error_message" text="Error occurred" visible="false"/>
```

In C++:
```cpp
CLabelUI* pLabel = static_cast<CLabelUI*>(pManager->FindControl(L"error_message"));
pLabel->SetVisible(true);  // Show
pLabel->SetVisible(false); // Hide
```

### Custom Attributes

DuiLib supports custom attributes that can be read in C++ code:

```xml
<Button name="my_button" text="Click" customdata="some_value"/>
```

In C++:
```cpp
CDuiString customData = pButton->GetUserData();
```

## Testing XML Changes

### 1. Edit XML File

Make your changes in a text editor.

### 2. Save File

Save to `resources/skins/` directory.

### 3. Rebuild (if necessary)

If using embedded resources:
```cmd
cmake --build build --config Release
```

If using external resources (recommended for testing):
- No rebuild needed
- Just restart installer

### 4. Test

Run the installer and verify changes:
```cmd
build\Release\installer.exe
```

### 5. Iterate

Repeat until satisfied with layout.

## Common Mistakes and Solutions

### Mistake: Control Not Appearing

**Possible Causes**:
1. Missing `height` or `width` attribute
2. Parent container too small
3. Control outside visible area
4. Typo in XML syntax

**Solution**:
- Add explicit size: `height="30"`
- Check parent container size
- Verify XML is well-formed

### Mistake: Text Truncated

**Cause**: Control too small for text.

**Solution**:
- Increase `width` or `height`
- Use smaller font
- Shorten text

### Mistake: Colors Not Showing

**Cause**: Invalid color format.

**Solution**:
- Use `#AARRGGBB` format
- Ensure alpha channel is FF for opaque: `#FFFFFFFF`

### Mistake: Images Not Loading

**Possible Causes**:
1. Image file not in `resources/images/`
2. Incorrect file name
3. Unsupported image format

**Solution**:
- Verify image file exists
- Check file name spelling (case-sensitive)
- Use PNG format (recommended)

### Mistake: Layout Broken After Changes

**Cause**: Invalid XML syntax.

**Solution**:
- Validate XML syntax
- Check for unclosed tags
- Verify attribute quotes
- Use XML validator tool

## Best Practices

### 1. Use Descriptive Names

**Good**:
```xml
<Button name="install_button" text="Install"/>
<Label name="disk_space_info" text="Space: 100 MB"/>
```

**Bad**:
```xml
<Button name="btn1" text="Install"/>
<Label name="lbl2" text="Space: 100 MB"/>
```

### 2. Consistent Spacing

Use consistent padding values throughout:
```xml
<VerticalLayout padding="20,20,20,20">
<HorizontalLayout padding="20,10,20,10">
```

### 3. Flexible Layouts

Use flexible spacers for responsive layouts:
```xml
<HorizontalLayout>
    <Control/>  <!-- Pushes button to right -->
    <Button name="ok_button" width="80"/>
</HorizontalLayout>
```

### 4. Comment Your Changes

```xml
<!-- Custom modification: Increased button size for better visibility -->
<Button name="install_button" width="120" height="40"/>
```

### 5. Keep Backups

Before making changes:
```cmd
copy resources\skins\welcome_page.xml resources\skins\welcome_page.xml.backup
```

### 6. Test on Different DPI Settings

Test your layout on:
- 100% scaling (96 DPI)
- 125% scaling (120 DPI)
- 150% scaling (144 DPI)
- 200% scaling (192 DPI)

### 7. Use Version Control

Track XML changes in Git:
```cmd
git add resources/skins/welcome_page.xml
git commit -m "Increased button sizes for better accessibility"
```

## Localization

### Approach 1: Multiple XML Files

Create language-specific XML files:
```
resources/skins/
├── en/
│   ├── welcome_page.xml
│   └── ...
├── zh/
│   ├── welcome_page.xml
│   └── ...
```

Load appropriate file based on system language.

### Approach 2: Dynamic Text (Recommended)

Keep layout in XML, set text from C++ code:

```xml
<Label name="welcome_message" text="" height="30"/>
```

In C++:
```cpp
CLabelUI* pLabel = static_cast<CLabelUI*>(pManager->FindControl(L"welcome_message"));
pLabel->SetText(GetLocalizedString("WELCOME_MESSAGE"));
```

## Troubleshooting

### XML Parse Errors

**Symptom**: Installer fails to start or shows blank window.

**Solution**:
1. Check XML syntax
2. Validate with XML validator
3. Look for:
   - Unclosed tags
   - Missing quotes
   - Invalid characters
   - Incorrect nesting

### Control Not Found in C++ Code

**Symptom**: `FindControl()` returns NULL.

**Solution**:
1. Verify `name` attribute matches exactly (case-sensitive)
2. Ensure control is in loaded XML file
3. Check control is not inside unloaded Include

### Layout Not Updating

**Symptom**: Changes to XML don't appear.

**Solution**:
1. Verify XML file saved
2. Check file location correct
3. Rebuild if using embedded resources
4. Clear any XML cache
5. Restart installer

## Reference

### Control Types

- `Label`: Static text
- `Button`: Clickable button
- `Edit`: Single-line text input
- `RichEdit`: Multi-line text input
- `CheckBox`: Checkbox with label
- `Option`: Radio button
- `Combo`: Dropdown list
- `List`: List box
- `Progress`: Progress bar
- `Slider`: Slider control
- `Control`: Generic container/spacer

### Layout Types

- `VerticalLayout`: Vertical stacking
- `HorizontalLayout`: Horizontal arrangement
- `TileLayout`: Grid layout
- `TabLayout`: Tabbed pages
- `ChildLayout`: Child window

### Common Attributes

- `name`: Unique identifier
- `text`: Display text
- `width`: Fixed width
- `height`: Fixed height
- `padding`: Inner spacing
- `bkcolor`: Background color
- `bkimage`: Background image
- `textcolor`: Text color
- `font`: Font ID
- `visible`: Visibility (true/false)
- `enabled`: Enabled state (true/false)

## Additional Resources

- DuiLib Documentation: Check official DuiLib repository
- XML Tutorial: W3Schools XML Tutorial
- Color Picker: Use online ARGB color picker tools
- XML Validator: Use online XML validation tools

## Support

For XML layout issues:
1. Check this guide
2. Review example XML files
3. Validate XML syntax
4. Test incrementally
5. Contact development team with specific questions
