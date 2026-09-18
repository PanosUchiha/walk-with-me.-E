"""Build v3 motion and wrist-detail previews from untouched 30 FPS renders."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[1]/'build/shoulder-rub-orbit-v3'
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',16)
for stem,first,last,size,label in [('motion',1,196,(480,544),'SHOULDER RUB — V3'),('detail',43,129,(480,480),'WRIST / SHOULDER — V3')]:
    frames=[]
    for i in range(first,last+1):
        canvas=Image.new('RGB',(size[0],size[1]+36),(19,26,33))
        canvas.paste(Image.open(root/f'{stem}-{i:03}.png').convert('RGB'),(0,36))
        ImageDraw.Draw(canvas).text((12,8),label,font=font,fill='white')
        frames.append(canvas)
    duration=[30,30,40]*(len(frames)//3)+[30]*(len(frames)%3)
    if stem=='motion':duration[-1]=500
    frames[0].save(root/f'{stem}-preview.gif',save_all=True,append_images=frames[1:],duration=duration,loop=0,optimize=False)
sheet=Image.new('RGB',(1440,516),(19,26,33));draw=ImageDraw.Draw(sheet)
for i,frame in enumerate((53,63,73,83,93,103)):
    im=Image.open(root/f'detail-{frame:03}.png').convert('RGB');im.thumbnail((240,240))
    sheet.paste(im,(i*240,36));draw.text((i*240+8,10),f'{(frame-1)/30:.2f}s',font=font,fill='white')
    full=Image.open(root/f'motion-{frame:03}.png').convert('RGB');full.thumbnail((210,238));sheet.paste(full,(i*240+15,278))
sheet.save(root/'orbit-contact-sheet.jpg',quality=95)
print(root/'motion-preview.gif')
