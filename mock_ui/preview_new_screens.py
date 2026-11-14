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

def _trend_line(draw, x0, y0, x1, y1, color, dash_on=0, dash_off=0, phase=0):
    """2 px line, optionally dashed, mirroring drawTrendLine() in renderer.cpp."""
    dx, dy = x1 - x0, y1 - y0
    steps = max(abs(dx), abs(dy))
    for k in range(steps + 1):
        x = x0 + (dx * k) // steps if steps else x0
        y = y0 + (dy * k) // steps if steps else y0
        on = True
        if dash_on > 0:
            on = (phase % (dash_on + dash_off)) < dash_on
            if k < steps:
                phase += 1
        if on:
            draw.point([(x, y), (x, y + 1)], fill=color)
    return phase

def render_screen1():
    """History page as drawn by drawScreen1_History(): no header or status
    bar, table on top, full-width 24 h chart with an hour axis."""
    img = Image.new("RGB", (W, H), WHITE)
    draw = ImageDraw.Draw(img)
    fonts = get_fonts()
    f6, f5 = fonts["small"], fonts["tiny"]

    # 1. Table (y = 2 to 107); text y values are baselines, as in the firmware
    draw.rectangle([6, 2, 393, 19], outline=BLACK)
    draw.text((12, 15), "Giorno", fill=BLACK, font=f6, anchor="ls")
    for x, t in ((110, "T. Min"), (180, "T. Max"), (260, "Pioggia"), (345, "Raffica Max")):
        draw.text((x, 15), t, fill=BLACK, font=f6, anchor="ms")

    rows = [
        ("Oggi (07/11)", 13.1, 20.5, 0.0, 11),
        ("Ieri (06/11)", 14.8, 19.5, 2.4, 14),
        ("Gio 05/11", 13.9, 18.7, 0.0, 8),
        ("Mer 04/11", 14.1, 19.0, 0.0, 9),
        ("Mar 03/11", 15.0, 21.2, 1.1, 16),
    ]
    row_y = 20
    for i, (day, tmin, tmax, rain, gust) in enumerate(rows):
        by = row_y + 12
        draw.text((12, by), day, fill=RED if i == 0 else BLACK, font=f6, anchor="ls")
        draw.text((110, by), f"{tmin:.1f}°C", fill=RED, font=f6, anchor="ms")
        draw.text((180, by), f"{tmax:.1f}°C", fill=BLACK, font=f6, anchor="ms")
        draw.text((260, by), f"{rain:.1f} mm", fill=RED if rain > 0 else BLACK, font=f6, anchor="ms")
        draw.text((345, by), f"{gust} kts", fill=BLACK, font=f6, anchor="ms")
        draw.line([(6, row_y + 17), (394, row_y + 17)], fill=BLACK)
        row_y += 17

    # 2. Legend + sync time (the status bar is hidden on this page)
    leg_y = 121
    lx = 8
    draw.text((lx, leg_y), "Temp. 24h", fill=BLACK, font=f6, anchor="ls")
    lx += 70
    _trend_line(draw, lx, leg_y - 4, lx + 18, leg_y - 4, BLACK)
    draw.text((lx + 22, leg_y), "Porto", fill=BLACK, font=f6, anchor="ls")
    lx += 64
    _trend_line(draw, lx, leg_y - 4, lx + 18, leg_y - 4, RED)
    draw.text((lx + 22, leg_y), "Collina", fill=RED, font=f6, anchor="ls")
    lx += 72
    _trend_line(draw, lx, leg_y - 4, lx + 18, leg_y - 4, BLACK, 4, 3)
    draw.text((lx + 22, leg_y), "Casa", fill=BLACK, font=f6, anchor="ls")
    draw.text((W - 8, leg_y), "Agg. 14:20", fill=BLACK, font=f5, anchor="rs")

    # 3. 24 h chart: 72 samples every 20 min, ending at 14:20
    gx0, gx1, gy0, gy1 = 32, 390, 130, 282
    end_min = 14 * 60 + 20
    start_min = end_min - 24 * 60
    times = [start_min + 20 * (i + 1) for i in range(72)]

    def diurnal(t, mean, amp, peak_h=14.5):
        h = (t / 60.0) % 24
        return mean + amp * math.cos((h - peak_h) / 24.0 * 2 * math.pi)

    porto = [diurnal(t, 17.6, 2.4) + 0.15 * math.sin(t / 47.0) for t in times]
    collina = [diurnal(t, 16.8, 3.6, 14.0) + 0.2 * math.sin(t / 31.0) for t in times]
    casa = [diurnal(t, 20.6, 0.6, 18.0) for t in times]
    series = [(porto, BLACK, 0, 0), (collina, RED, 0, 0), (casa, BLACK, 4, 3)]

    vals = porto + collina + casa
    v_min, v_max = min(vals), max(vals)
    span = v_max - v_min
    step = 1 if span <= 5 else 2 if span <= 12 else 5 if span <= 28 else 10
    g_min = math.floor((v_min - 0.5) / step) * step
    g_max = math.ceil((v_max + 0.5) / step) * step

    def x_of(t):
        return gx0 + int((t - start_min) * (gx1 - gx0) / (end_min - start_min))

    def y_of(v):
        y = gy1 - round((v - g_min) / (g_max - g_min) * (gy1 - gy0))
        return max(gy0, min(gy1, y))

    # Horizontal grid + temperature labels
    v = g_min
    while v <= g_max + 0.01:
        y = y_of(v)
        draw.text((gx0 - 4, y + 3), f"{v:.0f}°", fill=BLACK, font=f5, anchor="rs")
        if y != gy1:
            for x in range(gx0 + 2, gx1, 4):
                draw.point((x, y), fill=BLACK)
        v += step

    # Vertical grid + hour labels every 3 hours, midnight in red
    for hm in range((start_min // 60 + 1) * 60, end_min + 1, 60):
        hour = (hm // 60) % 24
        if hour % 3:
            continue
        x = x_of(hm)
        for y in range(gy0, gy1, 4):
            draw.point((x, y), fill=BLACK)
        draw.line([(x, gy1), (x, gy1 + 3)], fill=BLACK)
        lbl = f"{hour:02d}:00"
        lbl_x = min(x, W - 2 - int(draw.textlength(lbl, font=f5)) // 2)
        draw.text((lbl_x, H - 6), lbl, fill=RED if hour == 0 else BLACK, font=f5, anchor="ms")

    # Axes
    draw.line([(gx0, gy1), (gx1, gy1)], fill=BLACK)
    draw.line([(gx0, gy0), (gx0, gy1)], fill=BLACK)

    for data, color, d_on, d_off in series:
        ph = 0
        for i in range(1, len(times)):
            ph = _trend_line(draw, x_of(times[i - 1]), y_of(data[i - 1]),
                             x_of(times[i]), y_of(data[i]), color, d_on, d_off, ph)

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
