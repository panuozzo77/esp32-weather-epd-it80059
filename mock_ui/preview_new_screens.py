import os
import math
from PIL import Image, ImageDraw, ImageFont

W, H = 400, 300
BLACK = (20, 20, 20)
WHITE = (255, 255, 255)
RED = (210, 20, 20)
GREY = (180, 180, 180)

def get_fonts():
    # Use Liberation or DejaVu fonts if available, otherwise default
    candidates = [
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"
    ]
    bold_candidates = [
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"
    ]
    font_path = None
    bold_path = None
    for c in candidates:
        if os.path.exists(c):
            font_path = c
            break
    for c in bold_candidates:
        if os.path.exists(c):
            bold_path = c
            break

    def load(path, size, bold=False):
        p = bold_path if bold and bold_path else (path or font_path)
        if p and os.path.exists(p):
            return ImageFont.truetype(p, size)
        return ImageFont.load_default()

    return {
        "title": load(font_path, 15, bold=True),
        "large": load(font_path, 22, bold=True),
        "huge": load(font_path, 32, bold=True),
        "sub": load(font_path, 10, bold=False),
        "small": load(font_path, 9, bold=False),
        "tiny": load(font_path, 8, bold=False),
        "bold_sm": load(font_path, 10, bold=True),
        "bold_md": load(font_path, 12, bold=True),
    }

def draw_status_bar(draw, fonts, page=0, is_offline=False):
    draw.line([(4, H - 22), (W - 4, H - 22)], fill=BLACK, width=1)
    # Page dots
    for i in range(3):
        x = 10 + i * 12
        y = H - 11
        if i == page:
            draw.ellipse([x - 3, y - 3, x + 3, y + 3], fill=BLACK)
        else:
            draw.ellipse([x - 3, y - 3, x + 3, y + 3], outline=BLACK)

    draw.text((48, H - 16), f"P{page+1}/3 [USER: cambio]", fill=BLACK, font=fonts["small"])

    if is_offline:
        draw.text((W - 140, H - 16), "OFFLINE (Sync 12:40)", fill=RED, font=fonts["small"])
    else:
        draw.text((W - 140, H - 16), "Sync 14:20  [Wi-Fi]", fill=BLACK, font=fonts["small"])
    draw.text((W - 35, H - 16), "85%", fill=BLACK, font=fonts["small"])

def render_screen0():
    img = Image.new("RGB", (W, H), WHITE)
    draw = ImageDraw.Draw(img)
    fonts = get_fonts()

    # Header
    draw.text((8, 4), "TORRE DEL GRECO", fill=RED, font=fonts["title"])
    draw.text((8, 20), "Microclima & Meteo Odierno", fill=BLACK, font=fonts["small"])
    draw.text((W - 120, 10), "Sab 07 Nov  14:20", fill=BLACK, font=fonts["sub"])
    draw.line([(6, 33), (W - 6, 33)], fill=BLACK, width=1)

    # Left Column: Realtime microclimate
    draw.text((8, 38), "RILEVAMENTI REALTIME", fill=RED, font=fonts["tiny"])

    # Card 1: Casa
    draw.rounded_rectangle([6, 50, 194, 118], radius=3, outline=BLACK)
    draw.text((12, 54), "CASA (Interno AHT20)", fill=BLACK, font=fonts["bold_sm"])
    draw.text((12, 70), "21.4°C", fill=BLACK, font=fonts["large"])
    draw.text((12, 98), "Umidita': 58%  |  Ideale", fill=BLACK, font=fonts["small"])

    # Card 2: Collina
    draw.rounded_rectangle([6, 122, 194, 194], radius=3, outline=BLACK)
    draw.text((12, 126), "COLLINA (154m slm)", fill=RED, font=fonts["bold_sm"])
    draw.text((12, 142), "19.5°C", fill=RED, font=fonts["large"])
    draw.text((12, 170), "Umidita' 72%  |  1015 hPa", fill=BLACK, font=fonts["tiny"])
    draw.text((12, 181), "Vento: 5.2 kts SSE", fill=BLACK, font=fonts["tiny"])

    # Card 3: Porto
    draw.rounded_rectangle([6, 198, 194, 270], radius=3, outline=BLACK)
    draw.text((12, 202), "PORTO (Pontile)", fill=BLACK, font=fonts["bold_sm"])
    draw.text((12, 218), "19.2°C", fill=BLACK, font=fonts["large"])
    draw.text((12, 246), "Umidita' 76%  |  Pioggia: 0.0mm", fill=BLACK, font=fonts["tiny"])
    draw.text((12, 257), "Vento: 7.5 kts (Raff. 12)", fill=BLACK, font=fonts["tiny"])

    # Divider
    draw.line([(198, 38), (198, 273)], fill=BLACK, width=1)

    # Right Column: Today's Focus
    draw.text((204, 38), "METEO & PREVISIONE OGGI", fill=RED, font=fonts["tiny"])
    draw.rounded_rectangle([202, 50, 394, 270], radius=3, outline=BLACK)

    # Icon placeholder & title
    draw.rectangle([208, 56, 248, 96], outline=GREY)
    draw.text((214, 70), "[SOLE]", fill=BLACK, font=fonts["tiny"])
    draw.text((256, 62), "Poco nuvoloso", fill=BLACK, font=fonts["bold_sm"])
    draw.text((256, 78), "Previsione giornata", fill=RED, font=fonts["tiny"])

    draw.line([(206, 102), (390, 102)], fill=BLACK, width=1)

    # Temps
    draw.text((210, 108), "Temperature Previste:", fill=BLACK, font=fonts["small"])
    draw.text((210, 122), "Min 14.5°  |  Max 22.0°", fill=BLACK, font=fonts["bold_md"])

    draw.line([(206, 142), (390, 142)], fill=BLACK, width=1)

    # Rain & Wind
    draw.text((210, 148), "Probab. Pioggia: 10% (0.0 mm)", fill=BLACK, font=fonts["small"])
    draw.text((210, 166), "Raffica massima: fino a 14 kts", fill=BLACK, font=fonts["small"])

    draw.line([(206, 186), (390, 186)], fill=BLACK, width=1)

    # Sun
    draw.text((210, 194), "Alba: 06:42", fill=BLACK, font=fonts["sub"])
    draw.text((210, 212), "Tramonto: 16:55", fill=RED, font=fonts["sub"])
    draw.text((210, 234), "Radiazione Solare: 320 W/m²", fill=BLACK, font=fonts["tiny"])
    draw.text((210, 248), "Indice UV: 3 (Moderato)", fill=BLACK, font=fonts["tiny"])

    draw_status_bar(draw, fonts, page=0, is_offline=False)
    img.save("mock_ui/screen0_dashboard.png")
    print("Generated mock_ui/screen0_dashboard.png")

def render_screen1():
    img = Image.new("RGB", (W, H), WHITE)
    draw = ImageDraw.Draw(img)
    fonts = get_fonts()

    # Header
    draw.text((8, 4), "STORICO METEO & TREND 24H", fill=RED, font=fonts["title"])
    draw.text((8, 20), "Dati aggregati da scheda SD", fill=BLACK, font=fonts["small"])
    draw.text((W - 100, 12), "Torre del Greco", fill=BLACK, font=fonts["sub"])
    draw.line([(6, 33), (W - 6, 33)], fill=BLACK, width=1)

    # Table
    draw.rectangle([6, 37, 394, 55], outline=BLACK)
    draw.text((12, 41), "Giorno", fill=BLACK, font=fonts["bold_sm"])
    draw.text((105, 41), "T. Min", fill=BLACK, font=fonts["bold_sm"])
    draw.text((175, 41), "T. Max", fill=BLACK, font=fonts["bold_sm"])
    draw.text((250, 41), "Pioggia", fill=BLACK, font=fonts["bold_sm"])
    draw.text((330, 41), "Raffica", fill=BLACK, font=fonts["bold_sm"])

    rows = [
        ("Oggi (07/11)", "15.2°", "20.1°", "0.0 mm", "11 kts", True),
        ("Ieri (06/11)", "14.8°", "19.5°", "2.4 mm", "14 kts", False),
        ("Gio 05/11", "13.9°", "18.7°", "0.0 mm", "8 kts", False),
        ("Mer 04/11", "14.1°", "19.0°", "0.0 mm", "9 kts", False),
        ("Mar 03/11", "15.0°", "21.2°", "1.1 mm", "16 kts", False),
    ]

    y = 57
    for day, tmin, tmax, rain, wind, is_today in rows:
        draw.line([(6, y + 16), (394, y + 16)], fill=BLACK, width=1)
        draw.text((12, y + 2), day, fill=RED if is_today else BLACK, font=fonts["small"])
        draw.text((105, y + 2), tmin, fill=RED, font=fonts["small"])
        draw.text((175, y + 2), tmax, fill=BLACK, font=fonts["small"])
        draw.text((250, y + 2), rain, fill=RED if rain != "0.0 mm" else BLACK, font=fonts["small"])
        draw.text((330, y + 2), wind, fill=BLACK, font=fonts["small"])
        y += 17

    # 24h Graph
    draw.text((8, 148), "Curva Temp. 24h:", fill=BLACK, font=fonts["bold_sm"])
    draw.text((130, 148), "- Porto", fill=BLACK, font=fonts["small"])
    draw.text((190, 148), "- Collina", fill=RED, font=fonts["small"])
    draw.text((260, 148), "-- Casa", fill=BLACK, font=fonts["small"])

    gx0, gy0, gx1, gy1 = 36, 165, 390, 265
    draw.line([(gx0, gy1), (gx1, gy1)], fill=BLACK, width=1)
    draw.line([(gx0, gy0), (gx0, gy1)], fill=BLACK, width=1)

    draw.text((12, gy0), "22°", fill=BLACK, font=fonts["tiny"])
    draw.text((12, (gy0+gy1)//2 - 4), "18°", fill=BLACK, font=fonts["tiny"])
    draw.text((12, gy1 - 8), "14°", fill=BLACK, font=fonts["tiny"])

    # Dotted mid line
    mid_y = (gy0 + gy1) // 2
    for x in range(gx0, gx1, 6):
        draw.line([(x, mid_y), (x + 2, mid_y)], fill=BLACK, width=1)

    # Simulated curve
    pts_porto = [18.5, 18.0, 17.2, 16.5, 16.0, 15.8, 15.5, 15.2, 16.0, 17.5, 18.8, 19.5, 20.0, 19.8, 19.2]
    pts_collina = [18.0, 17.5, 16.8, 16.0, 15.2, 14.8, 14.5, 14.2, 15.0, 16.8, 18.2, 19.2, 19.8, 19.5, 19.0]
    n = len(pts_porto)
    step = (gx1 - gx0) / (n - 1)

    def to_y(val):
        return int(gy1 - ((val - 14.0) / (22.0 - 14.0)) * (gy1 - gy0))

    for i in range(n - 1):
        x_a = int(gx0 + i * step)
        x_b = int(gx0 + (i + 1) * step)
        draw.line([(x_a, to_y(pts_porto[i])), (x_b, to_y(pts_porto[i+1]))], fill=BLACK, width=2)
        draw.line([(x_a, to_y(pts_collina[i])), (x_b, to_y(pts_collina[i+1]))], fill=RED, width=2)

    draw_status_bar(draw, fonts, page=1, is_offline=False)
    img.save("mock_ui/screen1_history.png")
    print("Generated mock_ui/screen1_history.png")

def render_screen2():
    img = Image.new("RGB", (W, H), WHITE)
    draw = ImageDraw.Draw(img)
    fonts = get_fonts()

    # Header
    draw.text((8, 4), "LEGA NAVALE ITALIANA", fill=RED, font=fonts["title"])
    draw.text((8, 20), "Sezione Torre del Greco - Stazione Pontile", fill=BLACK, font=fonts["small"])
    draw.text((W - 120, 12), "40°47'N 14°21'E", fill=BLACK, font=fonts["sub"])
    draw.line([(6, 33), (W - 6, 33)], fill=BLACK, width=1)

    # Compass Rose on left
    cx, cy, r = 100, 140, 54
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=BLACK, width=2)
    draw.ellipse([cx - r + 4, cy - r + 4, cx + r - 4, cy + r - 4], outline=BLACK)
    draw.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=BLACK)

    draw.text((cx - 4, cy - r - 14), "N", fill=RED, font=fonts["bold_sm"])
    draw.text((cx + r + 6, cy - 6), "E", fill=BLACK, font=fonts["bold_sm"])
    draw.text((cx - 4, cy + r + 2), "S", fill=BLACK, font=fonts["bold_sm"])
    draw.text((cx - r - 14, cy - 6), "W", fill=BLACK, font=fonts["bold_sm"])

    # Arrow pointing SSE (155 deg)
    deg = 155
    rad = math.radians(deg)
    tip_x = cx + int(math.sin(rad) * (r - 8))
    tip_y = cy - int(math.cos(rad) * (r - 8))
    p_rad = rad + math.pi / 2
    wa_x = cx + int(math.sin(p_rad) * 6)
    wa_y = cy - int(math.cos(p_rad) * 6)
    wb_x = cx - int(math.sin(p_rad) * 6)
    wb_y = cy + int(math.cos(p_rad) * 6)
    draw.polygon([(tip_x, tip_y), (wa_x, wa_y), (wb_x, wb_y)], fill=RED)

    # Direction box
    draw.rounded_rectangle([20, 218, 180, 266], radius=3, outline=BLACK)
    draw.text((45, 224), "DIREZ: SSE (155°)", fill=RED, font=fonts["bold_md"])
    draw.text((45, 246), "Forza 3 Beaufort", fill=BLACK, font=fonts["small"])

    # Divider
    draw.line([(198, 38), (198, 273)], fill=BLACK, width=1)

    # Right: Nautical cards
    # Wind speed
    draw.rounded_rectangle([204, 38, 394, 110], radius=3, outline=BLACK)
    draw.text((212, 44), "VELOCITA' VENTO", fill=BLACK, font=fonts["small"])
    draw.text((212, 60), "7.5", fill=BLACK, font=fonts["huge"])
    draw.text((280, 68), "NODI (kts)", fill=BLACK, font=fonts["bold_sm"])
    draw.text((280, 84), "13.9 km/h", fill=BLACK, font=fonts["small"])

    # Gust
    draw.rounded_rectangle([204, 114, 394, 150], radius=3, outline=BLACK)
    draw.text((212, 122), "RAFFA: 12.0 kts", fill=RED, font=fonts["bold_md"])
    draw.text((320, 124), "(22.2 km/h)", fill=BLACK, font=fonts["small"])

    # Pier details
    draw.rounded_rectangle([204, 154, 394, 270], radius=3, outline=BLACK)
    draw.text((212, 160), "Temp. Pontile: 19.2°C", fill=BLACK, font=fonts["small"])
    draw.text((212, 178), "Umidita' Pontile: 76%", fill=BLACK, font=fonts["small"])
    draw.text((212, 196), "Pressione: 1015.4 hPa", fill=BLACK, font=fonts["small"])
    draw.text((212, 214), "Radiazione Solare: 325 W/m²", fill=BLACK, font=fonts["small"])
    draw.text((212, 232), "Indice UV: 3 (Moderato)", fill=BLACK, font=fonts["small"])
    draw.text((212, 250), "Pioggia oggi: 0.0 mm", fill=BLACK, font=fonts["small"])

    draw_status_bar(draw, fonts, page=2, is_offline=False)
    img.save("mock_ui/screen2_nautical.png")
    print("Generated mock_ui/screen2_nautical.png")

if __name__ == "__main__":
    render_screen0()
    render_screen1()
    render_screen2()
