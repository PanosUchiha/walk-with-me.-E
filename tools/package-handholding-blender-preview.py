"""Package Blender renders without changing or retouching the rendered pose."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[1]/'build/handholding-blender-20260915'
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',22)
small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)
sheet=Image.new('RGB',(1440,810),(22,28,35));draw=ImageDraw.Draw(sheet)
for x,stem,label in ((0,'current','CURRENT SOLVER — reference recreation'),(720,'soft','BLENDER DRAFT — softer clasp')):
    sheet.paste(Image.open(root/f'{stem}-three-quarter.png').convert('RGB'),(x,50))
    draw.text((x+20,13),label,font=font,fill='white')
draw.text((20,782),'Imported reference body. Blender draft has not been installed or tested in Skyrim.',font=small,fill=(195,204,217))
sheet.save(root/'comparison.jpg',quality=94)
frames=[]
for frame in range(1,152,3):
    canvas=Image.new('RGB',(480,524),(22,28,35))
    canvas.paste(Image.open(root/f'preview-{frame:03}.png').convert('RGB'),(0,44))
    label='REACH' if frame<31 else 'HOLD' if frame<=121 else 'RELEASE'
    ImageDraw.Draw(canvas).text((14,11),f'Blender draft · {label} · {(frame-1)/30:.1f}s',font=small,fill='white')
    frames.append(canvas)
frames[0].save(root/'soft-clasp-preview.gif',save_all=True,append_images=frames[1:],duration=[100]*50+[700],loop=0,optimize=False)
print(root/'comparison.jpg');print(root/'soft-clasp-preview.gif')
