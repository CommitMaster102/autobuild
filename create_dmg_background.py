#!/usr/bin/env python3
"""
Create a simple DMG background image for macOS installer
This creates a 500x300 PNG image with a gradient background
"""

from PIL import Image, ImageDraw, ImageFont
import os

def create_dmg_background():
    # Create a 500x300 image with a gradient background
    width, height = 500, 300
    image = Image.new('RGB', (width, height), color='white')
    draw = ImageDraw.Draw(image)
    
    # Create a subtle gradient background
    for y in range(height):
        # Light blue to white gradient
        color_value = int(240 + (15 * y / height))
        color = (color_value, color_value + 10, color_value + 20)
        draw.line([(0, y), (width, y)], fill=color)
    
    # Add some text
    try:
        # Try to use a system font
        font = ImageFont.truetype("/System/Library/Fonts/Arial.ttf", 24)
    except:
        try:
            font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 24)
        except:
            font = ImageFont.load_default()
    
    # Add title
    title = "Autobuild"
    bbox = draw.textbbox((0, 0), title, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    
    x = (width - text_width) // 2
    y = height // 2 - text_height - 10
    
    # Draw text with shadow
    draw.text((x + 2, y + 2), title, fill=(100, 100, 100), font=font)
    draw.text((x, y), title, fill=(50, 50, 50), font=font)
    
    # Add subtitle
    subtitle = "Drag to Applications folder"
    try:
        small_font = ImageFont.truetype("/System/Library/Fonts/Arial.ttf", 16)
    except:
        small_font = ImageFont.load_default()
    
    bbox = draw.textbbox((0, 0), subtitle, font=small_font)
    text_width = bbox[2] - bbox[0]
    x = (width - text_width) // 2
    y = height // 2 + 10
    
    draw.text((x + 1, y + 1), subtitle, fill=(120, 120, 120), font=small_font)
    draw.text((x, y), subtitle, fill=(80, 80, 80), font=small_font)
    
    # Save the image
    output_path = "native/resources/dmg_background.png"
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    image.save(output_path, "PNG")
    print(f"DMG background image created: {output_path}")

if __name__ == "__main__":
    create_dmg_background()
