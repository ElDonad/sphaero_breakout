import numpy as np
import matplotlib.pyplot as plt

def generate_gist_rainbow_colors(n):
    # Create a colormap object
    cmap = plt.get_cmap("gist_rainbow")
    
    # Generate n colors equally spaced on the colormap
    colors = [cmap(i / (n - 0.99)) for i in range(n)]
    
    # Convert the colors to the format 0xffbbggrr
    formatted_colors = []
    for color in colors:
        # Extract RGBA and convert RGB to 8-bit integer (0-255)
        r, g, b = [int(255 * c) for c in color[:3]]
        
        # Format as 0xffbbggrr
        hex_color = f"0xff{b:02x}{g:02x}{r:02x}"
        formatted_colors.append(hex_color)
    
    return formatted_colors

for c in generate_gist_rainbow_colors(13):
    print(c+ ",")