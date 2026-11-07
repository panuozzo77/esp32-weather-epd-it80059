import sys
import os
import math
from PIL import Image, ImageDraw, ImageFont

def draw_layout(resolution="400x300", output_file="mock.png"):
    # Configurations
    if resolution == "400x300":
        width, height = 400, 300
        wi_col = 84
        wi_y0 = 104
        wi_dy = 30
        wi_sz = 24
        wi_loff = 24
        wi_ldy = 5
        wi_ddy = 17
        font_size_label = 9
        font_size_data = 12
        font_size_main = 28
        header_y = 5
        graph_rect = (160, 104, 395, 275)
        statusbar_rect = (0, 285, 400, 300)
        uv_label = "UV"
        aqi_label = "AQI"
    else: # 800x480
        width, height = 800, 480
        wi_col = 162
        wi_y0 = 204
        wi_dy = 56
        wi_sz = 48
        wi_loff = 48
        wi_ldy = 10
        wi_ddy = 32
        font_size_label = 16
        font_size_data = 24
        font_size_main = 48
        header_y = 10
        graph_rect = (350, 204, 780, 440)
        statusbar_rect = (0, 450, 800, 480)
        uv_label = "Indice UV"
        aqi_label = "Qualità dell'aria"

    img = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(img)

    try:
        # try to find a nice font, or fallback
        font_label = ImageFont.truetype("DejaVuSans.ttf", font_size_label)
        font_data = ImageFont.truetype("DejaVuSans-Bold.ttf", font_size_data)
        font_main = ImageFont.truetype("DejaVuSans-Bold.ttf", font_size_main)
        font_small = ImageFont.truetype("DejaVuSans.ttf", font_size_label - 2)
    except IOError:
        font_label = ImageFont.load_default()
        font_data = ImageFont.load_default()
        font_main = ImageFont.load_default()
        font_small = ImageFont.load_default()

    accent_color = (200, 0, 0) # Red accent

    # Draw Header
    draw.rectangle([10, header_y, 10+wi_sz*2, header_y+wi_sz*2], outline="grey")
    draw.text((10+wi_sz*2 + 10, header_y + 10), "25°C", fill="black", font=font_main)
    draw.text((10+wi_sz*2 + 10, header_y + 10 + font_size_main), "Percepiti 25°", fill="grey", font=font_label)

    # City & Date
    draw.text((width - 120, header_y), "New York", fill=accent_color, font=font_data)
    draw.text((width - 120, header_y + font_size_data), "2026-09-05", fill="black", font=font_label)

    # 5-day forecast placeholders
    fc_width = (width - 180) // 5
    for i, day in enumerate(["sab", "dom", "lun", "mar", "mer"]):
        fx = 160 + i * fc_width
        fy = header_y + 10
        draw.text((fx + 10, fy), day, fill="black", font=font_label)
        draw.rectangle([fx+10, fy+15, fx+34, fy+39], outline="grey") # icon
        draw.text((fx + 5, fy + 45), f"{28-i}°|{17-i}°", fill="black", font=font_small)

    # Left Current Conditions
    widgets = [
        {"name": "Alba", "val": "06:10"},
        {"name": "Tramonto", "val": "20:18"},
        {"name": "Vento", "val": "11 mph"},
        {"name": "Umidità", "val": "56%"},
        {"name": uv_label, "val": "5", "desc": "Medio"},
        {"name": "Pressione", "val": "29.9 inHg"},
        {"name": aqi_label, "val": "26", "desc": "Basso"},
        {"name": "Visibilità", "val": "> 6 mi"},
        {"name": "Temp. int.", "val": "30.6°"},
        {"name": "Umid. int.", "val": "71%"},
    ]

    for pos, w in enumerate(widgets):
        pos_x = pos % 2
        pos_y = pos // 2

        base_x = pos_x * wi_col
        base_y = wi_y0 + (pos_y * wi_dy)

        # Icon
        draw.rectangle([base_x, base_y, base_x + wi_sz, base_y + wi_sz], outline="grey")
        
        # Label
        label_x = base_x + wi_loff
        label_y = base_y + wi_ldy
        draw.text((label_x, label_y), w["name"], fill="black", font=font_label)

        # Data
        data_y = base_y + wi_ddy
        draw.text((label_x, data_y), w["val"], fill=accent_color, font=font_data)
        if "desc" in w:
            # draw desc slightly to the right or below depending on size
            draw.text((label_x + 20, data_y + font_size_data - font_size_label), w["desc"], fill=accent_color, font=font_small)

    # Outlook Graph Area
    gx, gy, gxw, gyw = graph_rect
    draw.rectangle(graph_rect, outline="white")
    
    # Grid lines
    for y in range(gy, gyw, (gyw-gy)//4):
        draw.line((gx, y, gxw, y), fill="#eeeeee", width=1)
        
    # Sine wave for temp
    points = []
    for x in range(gx, gxw):
        # 1 cycle
        freq = (x - gx) / (gxw - gx) * math.pi * 2
        y = gy + (gyw-gy)//2 - math.cos(freq) * ((gyw-gy)//3)
        points.append((x, y))
    draw.line(points, fill=accent_color, width=2)
    
    # X-axis ticks
    draw.line((gx, gyw-15, gxw, gyw-15), fill="black", width=2)
    for i, t in enumerate(["14", "17", "20", "23", "02", "05", "08", "11", "14"]):
        tx = gx + i * ((gxw-gx)//8)
        draw.line((tx, gyw-15, tx, gyw-10), fill="black", width=2)
        draw.text((tx-5, gyw-10), t, fill="black", font=font_label)
        
    # Y-axis labels
    for i, p in enumerate(["100%", "80%", "60%", "40%", "20%", "0%"]):
        draw.text((gxw - 30, gy + i*((gyw-15-gy)//5) - 5), p, fill="black", font=font_label)

    # Status bar
    draw.rectangle(statusbar_rect, fill="white", outline="white")
    draw.text((5, statusbar_rect[1] + 2), "(!) Using OpenWeather data", fill=accent_color, font=font_label)
    draw.text((statusbar_rect[2]//2, statusbar_rect[1] + 2), "05/09/26 14:30   Scarso", fill="black", font=font_label)
    draw.rectangle([statusbar_rect[2]-40, statusbar_rect[1]+2, statusbar_rect[2]-20, statusbar_rect[1]+12], fill="black")
    draw.text((statusbar_rect[2]-18, statusbar_rect[1] + 2), "99%", fill="black", font=font_label)

    img.save(output_file)
    print(f"Mock UI generated and saved to {output_file} at {width}x{height} resolution.")

if __name__ == "__main__":
    res = sys.argv[1] if len(sys.argv) > 1 else "400x300"
    out = sys.argv[2] if len(sys.argv) > 2 else f"mock_{res}.png"
    draw_layout(res, out)
