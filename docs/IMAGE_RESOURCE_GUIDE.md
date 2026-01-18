# Image Resource Replacement Guide

## Overview

This guide explains how to replace and customize image resources used in the installer's graphical interface. All images are stored as separate files, allowing easy customization without recompiling the application.

## Image Resource Location

All image resources are located in:
```
resources/images/
```

## Required Images

### Application Logo

**File**: `logo.png`

**Specifications**:
- **Size**: 64x64 pixels
- **Format**: PNG with transparency
- **Color Mode**: RGBA (32-bit)
- **Purpose**: Displayed on all three pages (Welcome, Progress, Completion)

**Design Guidelines**:
- Use transparent background
- Center the logo within 64x64 canvas
- Ensure logo is recognizable at small size
- Use high contrast for visibility
- Test on both light and dark backgrounds

**Creation Tips**:
- Start with vector format (SVG, AI) if available
- Export at 2x size (128x128) then downscale for better quality
- Use anti-aliasing for smooth edges
- Optimize file size (keep under 50 KB)

### Button Images

Buttons require three state images for visual feedback:

#### Normal State

**File**: `button_normal.png`

**Specifications**:
- **Recommended Size**: 100x35 pixels (or your button size)
- **Format**: PNG with transparency
- **Purpose**: Button appearance in default state

#### Hover State

**File**: `button_hover.png`

**Specifications**:
- **Size**: Same as normal state
- **Format**: PNG with transparency
- **Purpose**: Button appearance when mouse hovers over it

**Design Guidelines**:
- Slightly lighter or brighter than normal state
- Subtle change to indicate interactivity
- Maintain same dimensions as normal state

#### Pushed State

**File**: `button_pushed.png`

**Specifications**:
- **Size**: Same as normal state
- **Format**: PNG with transparency
- **Purpose**: Button appearance when clicked/pressed

**Design Guidelines**:
- Darker or more saturated than normal state
- Can include slight offset to simulate depth
- Maintain same dimensions as normal state

### Primary Button Images (Optional)

For emphasized buttons (like "Install"):

**Files**:
- `button_primary_normal.png`
- `button_primary_hover.png`
- `button_primary_pushed.png`

**Specifications**: Same as regular buttons

**Design Guidelines**:
- Use accent color (e.g., blue, green)
- Higher contrast than secondary buttons
- Clearly distinguishable from regular buttons

### Secondary Button Images (Optional)

For less emphasized buttons (like "Cancel"):

**Files**:
- `button_secondary_normal.png`
- `button_secondary_hover.png`
- `button_secondary_pushed.png`

**Specifications**: Same as regular buttons

**Design Guidelines**:
- Neutral colors (gray, white)
- Less prominent than primary buttons

### Progress Bar Images

#### Foreground (Filled Portion)

**File**: `progress_fore.png`

**Specifications**:
- **Recommended Size**: 200x20 pixels (width can be any, height should match bar)
- **Format**: PNG
- **Purpose**: Filled portion of progress bar

**Design Guidelines**:
- Use solid color or gradient
- Typically blue, green, or brand color
- Ensure good contrast with background
- Image will be tiled/stretched horizontally

#### Background (Empty Portion)

**File**: `progress_bk.png`

**Specifications**:
- **Size**: Same as foreground
- **Format**: PNG
- **Purpose**: Empty portion of progress bar

**Design Guidelines**:
- Light gray or neutral color
- Subtle texture or gradient acceptable
- Should not distract from foreground

### Title Bar Button Icons

#### Minimize Button

**File**: `min.png`

**Specifications**:
- **Size**: 28x22 pixels
- **Format**: PNG with transparency
- **Purpose**: Minimize window button icon

**Design Guidelines**:
- Simple line or dash symbol
- White or light color for dark title bar
- Dark color for light title bar
- Centered within canvas

#### Close Button

**File**: `close.png`

**Specifications**:
- **Size**: 28x22 pixels
- **Format**: PNG with transparency
- **Purpose**: Close window button icon

**Design Guidelines**:
- X or cross symbol
- Same color scheme as minimize button
- Slightly bolder than minimize for emphasis
- Centered within canvas

## Image Format Guidelines

### PNG Format

**Why PNG?**
- Supports transparency (alpha channel)
- Lossless compression
- Wide compatibility
- Good for UI elements

**Optimization**:
- Use PNG-8 for simple graphics (256 colors)
- Use PNG-24 for complex graphics or photos
- Use PNG-32 for images requiring transparency
- Optimize with tools like TinyPNG or OptiPNG

### Transparency

**Alpha Channel**:
- Use full transparency for backgrounds
- Use semi-transparency for shadows or glows
- Avoid semi-transparent edges on small icons (can look blurry)

**Testing**:
- Test on different background colors
- Verify no white/black halos around edges
- Check appearance at different DPI settings

### Color Depth

**Recommended**:
- 32-bit RGBA for images with transparency
- 24-bit RGB for opaque images
- 8-bit indexed for simple graphics (smaller file size)

## Creating Images

### Using Photoshop

1. **Create New Document**
   - File → New
   - Set dimensions (e.g., 64x64 for logo)
   - Resolution: 96 DPI (screen resolution)
   - Color Mode: RGB Color, 8 bit
   - Background: Transparent

2. **Design Your Image**
   - Use layers for organization
   - Keep design simple and clear
   - Test at actual size (100% zoom)

3. **Export**
   - File → Export → Export As
   - Format: PNG
   - Check "Transparency"
   - Click "Export"

### Using GIMP (Free Alternative)

1. **Create New Image**
   - File → New
   - Set dimensions
   - Advanced Options → Fill with: Transparency

2. **Design Your Image**
   - Use layers
   - Keep it simple

3. **Export**
   - File → Export As
   - Select PNG format
   - Check "Save background color"
   - Click "Export"

### Using Inkscape (Vector Graphics)

1. **Create Vector Design**
   - File → Document Properties
   - Set page size to desired dimensions
   - Design using vector tools

2. **Export to PNG**
   - File → Export PNG Image
   - Set export area to page
   - Set DPI to 96
   - Click "Export"

### Using Online Tools

**Canva**:
- Free online design tool
- Templates available
- Export as PNG with transparency

**Figma**:
- Professional design tool
- Free tier available
- Export at multiple resolutions

**Photopea**:
- Free online Photoshop alternative
- Supports PSD files
- Export as PNG

## Replacing Images

### Step-by-Step Process

1. **Backup Original Images**
   ```cmd
   mkdir resources\images\backup
   copy resources\images\*.png resources\images\backup\
   ```

2. **Create New Images**
   - Follow specifications above
   - Match original dimensions
   - Use same file names

3. **Replace Files**
   - Copy new images to `resources/images/`
   - Overwrite existing files
   - Keep same file names

4. **Test**
   - Run installer
   - Verify images appear correctly
   - Check all button states
   - Test on different DPI settings

5. **Adjust if Needed**
   - Refine images based on testing
   - Repeat until satisfied

### Maintaining Aspect Ratios

If changing image dimensions:

1. **Update XML Layout**
   
   Edit `resources/skins/welcome_page.xml` (and other pages):
   ```xml
   <Control name="app_logo" width="80" height="80" bkimage="logo.png"/>
   ```
   
   Change `width` and `height` to match new image size.

2. **Update Button Sizes**
   
   If changing button image sizes:
   ```xml
   <Button name="install_button" width="120" height="40"/>
   ```

3. **Test Layout**
   - Ensure controls don't overlap
   - Verify spacing is appropriate
   - Check on different screen sizes

## Image Optimization

### File Size Optimization

**Why Optimize?**
- Smaller installer package
- Faster loading
- Better performance

**Tools**:

1. **TinyPNG** (Online)
   - Visit tinypng.com
   - Upload PNG files
   - Download optimized versions
   - Can reduce size by 50-70%

2. **OptiPNG** (Command Line)
   ```cmd
   optipng -o7 logo.png
   ```
   - `-o7`: Maximum optimization

3. **PNGGauntlet** (Windows GUI)
   - Drag and drop PNG files
   - Automatically optimizes
   - Batch processing

4. **ImageOptim** (Mac)
   - Drag and drop images
   - Lossless optimization
   - Batch processing

### Quality vs. Size

**Guidelines**:
- Logo: Prioritize quality (< 50 KB acceptable)
- Buttons: Balance quality and size (< 20 KB each)
- Progress bars: Can be heavily optimized (< 5 KB)
- Icons: Optimize aggressively (< 2 KB)

## High DPI Support

### Creating High-DPI Images

For sharp display on high-DPI screens:

1. **Create @2x Images**
   - Design at 2x dimensions (e.g., 128x128 for 64x64 logo)
   - Save as `logo@2x.png`
   - DuiLib will automatically use on high-DPI displays

2. **Create @3x Images** (Optional)
   - For very high-DPI displays
   - 3x dimensions (e.g., 192x192)
   - Save as `logo@3x.png`

3. **Naming Convention**
   - Standard: `logo.png` (64x64)
   - High-DPI: `logo@2x.png` (128x128)
   - Very High-DPI: `logo@3x.png` (192x192)

**Note**: DuiLib support for @2x/@3x images depends on version. Check documentation or test.

### Alternative: Vector Graphics

Consider using vector formats (SVG) if supported:
- Scales perfectly to any size
- Smaller file size
- Single file for all DPI settings

**Note**: DuiLib may require plugin or custom rendering for SVG support.

## Design Guidelines

### Color Schemes

**Light Theme** (Recommended):
- Background: #FFFFFF or #F5F5F5
- Text: #333333
- Accent: #4A90E2 (blue) or brand color
- Borders: #CCCCCC

**Dark Theme** (Alternative):
- Background: #2D2D2D
- Text: #FFFFFF
- Accent: #4A90E2 or lighter brand color
- Borders: #555555

### Consistency

- Use same color palette across all images
- Maintain consistent style (flat, gradient, skeuomorphic)
- Keep button sizes uniform
- Use same corner radius for rounded elements

### Accessibility

- Ensure sufficient contrast (WCAG AA: 4.5:1 for text)
- Don't rely solely on color to convey information
- Test with color blindness simulators
- Provide text alternatives where possible

### Branding

- Use company/product colors
- Include logo on all pages
- Maintain brand identity
- Follow brand guidelines if available

## Common Image Sizes Reference

| Element | Recommended Size | Format | Notes |
|---------|-----------------|--------|-------|
| Logo | 64x64 | PNG | Transparent background |
| Button | 100x35 | PNG | Three states required |
| Progress Bar | 200x20 | PNG | Foreground and background |
| Title Bar Icon | 28x22 | PNG | Minimize and close |
| Window Icon | 32x32, 16x16 | ICO | Multiple sizes in one file |

## Testing Images

### Visual Testing Checklist

- [ ] Images load without errors
- [ ] Transparency renders correctly
- [ ] No white/black halos around edges
- [ ] Images sharp at 100% zoom
- [ ] Button states clearly distinguishable
- [ ] Progress bar animates smoothly
- [ ] Logo recognizable at actual size
- [ ] Colors match design specifications

### DPI Testing

Test on different DPI settings:

1. **100% Scaling (96 DPI)**
   - Standard desktop resolution
   - Images should be crisp

2. **125% Scaling (120 DPI)**
   - Common laptop setting
   - Check for blurriness

3. **150% Scaling (144 DPI)**
   - High-resolution laptops
   - Verify scaling quality

4. **200% Scaling (192 DPI)**
   - 4K displays
   - Ensure images don't pixelate

**Change DPI**:
- Windows Settings → System → Display → Scale and layout

### Browser Testing (for Reference)

While not directly applicable, you can preview images:
1. Create simple HTML file
2. Display images at actual size
3. Test on different devices/browsers
4. Verify appearance

## Troubleshooting

### Images Not Appearing

**Possible Causes**:
1. Incorrect file name
2. Wrong file location
3. Unsupported format
4. Corrupted file

**Solutions**:
- Verify file name matches XML reference (case-sensitive)
- Check file is in `resources/images/`
- Ensure PNG format
- Try opening file in image viewer to verify integrity

### Images Appear Stretched or Distorted

**Cause**: Image dimensions don't match control size.

**Solution**:
- Resize image to match control dimensions
- Or update XML to match image dimensions
- Maintain aspect ratio

### Images Appear Blurry

**Possible Causes**:
1. Low-resolution source image
2. Upscaling from smaller size
3. DPI scaling issues

**Solutions**:
- Create image at actual display size or larger
- Use @2x images for high-DPI displays
- Ensure image is sharp at 100% zoom in editor

### Transparency Not Working

**Possible Causes**:
1. Image saved without alpha channel
2. White/black background instead of transparent
3. Format doesn't support transparency (JPEG)

**Solutions**:
- Re-save as PNG-32 with transparency
- Use image editor to remove background
- Verify alpha channel exists

### Button States Not Changing

**Possible Causes**:
1. Missing state images
2. Incorrect file names
3. XML not referencing images

**Solutions**:
- Verify all three state images exist
- Check file names match XML attributes
- Ensure XML has `normalimage`, `hotimage`, `pushedimage` attributes

## Advanced Techniques

### Gradient Backgrounds

Create gradient images for backgrounds:

1. Create image in editor
2. Apply gradient (e.g., top to bottom)
3. Save as PNG
4. Reference in XML:
   ```xml
   <VerticalLayout bkimage="gradient_background.png">
   ```

### Nine-Patch Images

For scalable buttons/panels:

1. Create image with stretchable regions
2. Mark stretch zones (1px borders)
3. Save as PNG
4. Reference in XML with special attributes

**Note**: Check DuiLib documentation for nine-patch support.

### Animated Progress Bars

Create multiple frames:
- `progress_frame1.png`
- `progress_frame2.png`
- `progress_frame3.png`

Cycle through frames in C++ code for animation effect.

### Icon Fonts

Alternative to image icons:

1. Use icon font (e.g., Font Awesome)
2. Reference in XML as font
3. Use Unicode characters for icons

**Advantages**:
- Scalable to any size
- Single color easily changed
- Smaller file size

## Resources

### Design Tools

**Free**:
- GIMP: https://www.gimp.org/
- Inkscape: https://inkscape.org/
- Paint.NET: https://www.getpaint.net/
- Photopea: https://www.photopea.com/

**Paid**:
- Adobe Photoshop
- Adobe Illustrator
- Affinity Designer
- Sketch (Mac)

### Optimization Tools

- TinyPNG: https://tinypng.com/
- OptiPNG: http://optipng.sourceforge.net/
- PNGGauntlet: https://pnggauntlet.com/
- ImageOptim: https://imageoptim.com/ (Mac)

### Stock Images

**Free**:
- Unsplash: https://unsplash.com/
- Pexels: https://www.pexels.com/
- Pixabay: https://pixabay.com/

**Icons**:
- Font Awesome: https://fontawesome.com/
- Material Icons: https://material.io/icons/
- Feather Icons: https://feathericons.com/

### Color Tools

- Adobe Color: https://color.adobe.com/
- Coolors: https://coolors.co/
- Color Hunt: https://colorhunt.co/

## Best Practices Summary

1. **Always backup original images before replacing**
2. **Use PNG format with transparency**
3. **Match original dimensions or update XML**
4. **Optimize file sizes**
5. **Test on multiple DPI settings**
6. **Maintain consistent style across all images**
7. **Follow brand guidelines**
8. **Ensure sufficient contrast for accessibility**
9. **Create @2x versions for high-DPI displays**
10. **Document any custom image specifications**

## Support

For image-related issues:
1. Check this guide
2. Verify image specifications
3. Test with original images
4. Review XML layout references
5. Contact development team with specific questions and sample images
