"""Package unretouched Blender renders as a motion preview and contact sheet."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
root=Path(__file__).resolve().parents[1]/'build/shoulder-rub-natural-20260916'
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)
frames=[]
for frame in range(1,197,3):
    canvas=Image.new('RGB',(480,586),(19,26,33))
    canvas.paste(Image.open(root/f'motion-{frame:03}.png').convert('RGB'),(0,42))
    t=(frame-1)/30
    label='BREATHE / REACH' if t<1.5 else 'SHOULDER RUB' if t<3.7 else 'EXHALE / NOD' if t<4.5 else 'RELEASE / SETTLE'
    ImageDraw.Draw(canvas).text((12,10),f'{label}   {t:.1f}s',font=font,fill='white')
    frames.append(canvas)
frames[0].save(root/'shoulder-rub-natural-preview.gif',save_all=True,append_images=frames[1:],duration=[100]*65+[600],loop=0,optimize=False)
sheet=Image.new('RGB',(1440,445),(19,26,33));d=ImageDraw.Draw(sheet)
for i,(name,label) in enumerate([('neutral','Breathe'),('reach','Reach'),('contact','Shoulder rub'),('nod','Exhale / nod')]):
    im=Image.open(root/(name+'.png')).convert('RGB');im.thumbnail((360,408))
    sheet.paste(im,(360*i,35));d.text((360*i+12,8),label,font=font,fill='white')
sheet.save(root/'motion-sheet.jpg',quality=95)
print(root/'shoulder-rub-natural-preview.gif')
